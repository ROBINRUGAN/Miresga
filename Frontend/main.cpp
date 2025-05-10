#include <nlohmann/json.hpp>  // JSON 解析库
#include <sys/epoll.h>        // epoll 用于监听多路 IO 事件，用于同时监控多个文件描述符（socket、timer 等）的可读写状态
#include <sys/timerfd.h>      // timerfd 用于定时器文件描述符
#include <numa.h>             // NUMA 相关操作
#include <iostream>
#include <fstream>
#include "pkt_processor.hpp"
#include "entry_controller.hpp"
#include "my_config.hpp"
#include "concurrentqueue/concurrentqueue.h"
#include "rdma/include/engine.hpp"
using json = nlohmann::json; //xx JSON 解析库的别名
int main(int argc, char *argv[]) {

    std::ifstream total_config(CONFIG_PATH);
    json config;
    total_config >> config; 

    std::string local_ip = config["local_ip"];
    uint32_t local_switch_port = config["local_switch_port"];
    std::string switch_ip = config["switch_ip"];
    uint32_t switch_port = config["switch_port"];
    std::string rdma_dev = config["rdma_dev"];
    std::string my_name = config["name"];

    std::vector<int> pkt_processor_core_ids;

    // 4. 检查并设置 NUMA 节点（若系统支持 NUMA）
    if (config.contains("numa_node")) {
        std::string numa_node = config["numa_node"];
        if(numa_available() == -1) {
            std::cerr << "NUMA is not available" << std::endl;
            return -1;
        }
        // 设置 NUMA 策略
        numa_set_bind_policy(1); // 1 表示使用 NUMA 策略
        numa_bind(numa_parse_nodestring(numa_node.c_str())); // 绑定 NUMA 节点
    }
 
    // 5. 从配置文件中读取 pkt_processor_core_ids,支持单个处理器核心或多个处理器核心
    auto res = config["pkt_processor_core_ids"];
    if(res.is_array()) {
        int size = res.size();
        int i = 0;
        for(auto core_id : res) {
            if(i == size) {
                break;
            }
            pkt_processor_core_ids.push_back(core_id.get<int>());
            ++i;
        }
    }
    else if(!res.is_null()) {
        pkt_processor_core_ids.push_back(res.get<int>());
    }
    else {
        std::cerr << "pkt_processor_core_ids is not an array or an integer" << std::endl;
        return -1;
    }

    total_config.close();// 关闭配置文件
    
    // 6. 读取 DPDK 配置文件，初始化 DPDK 配置参数，
    // 包括网卡地址、接收队列大小、发送队列大小、mbuf 数量、mbuf 缓存大小、mbuf 数据区大小、burst 大小
    std::ifstream dpdk_config(DPDK_CONFIG_PATH);
    json dpdk_config_json;
    dpdk_config >> dpdk_config_json;

    dpdk_config_t dpdk_config_args;
    dpdk_config_args.pci_addr = (char *)dpdk_config_json["pci_addr"].get<std::string>().c_str();
    dpdk_config_args.rx_ring_size = dpdk_config_json["rx_ring_size"];
    dpdk_config_args.tx_ring_size = dpdk_config_json["tx_ring_size"];
    dpdk_config_args.num_mbufs = dpdk_config_json["num_mbufs"]; 
    dpdk_config_args.mbuf_cache_size = dpdk_config_json["mbuf_cache_size"]; 
    dpdk_config_args.mbuf_data_room_size = dpdk_config_json["mbuf_data_room_size"]; 
    dpdk_config_args.burst_size = dpdk_config_json["burst_size"];   

    dpdk_config_args.queue_size = pkt_processor_core_ids.size();
    std::cout << dpdk_config_args.queue_size << std::endl;
    dpdk_config.close();

    int epoll_fd = epoll_create1(0); // 创建 epoll 文件描述符，填写0表示使用默认参数，返回值为 epoll 文件描述符
    if(epoll_fd == -1) {
        perror("epoll_create1");
        return -1;
    }

    libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> flow_hash_map[256];
    rdma::Engine* engine = new rdma::Engine(rdma_dev.c_str()); // 创建 RDMA 引擎对象，传入 RDMA 设备名称

    
    std::cout << "Rule controller initializing" << std::endl;
    rule_controller_t *rule_controller = new rule_controller_t;
    std::cout << "Rule controller initialized" << std::endl;

    std::cout << "Entry controller initializing" << std::endl;
    moodycamel::ConcurrentQueue<my_pair_t> *add_queue = new moodycamel::ConcurrentQueue<my_pair_t>;
    moodycamel::ConcurrentQueue<my_key_t> *del_queue = new moodycamel::ConcurrentQueue<my_key_t>;
    std::shared_ptr<entry_controller_t> entry_controller = std::make_shared<entry_controller_t>(add_queue, del_queue);
    std::cout << "Entry controller initialized" << std::endl;

    std::cout << "Pkt processor initializing" << std::endl;
    pkt_processor_t::init_static_variable(argc, argv, &dpdk_config_args, rule_controller, add_queue, del_queue, flow_hash_map, engine);
    
    // 12. 创建 pkt_processor 对象，用于处理数据包
    std::vector<std::shared_ptr<pkt_processor_t>> pkt_processors;
    for(int i = 0; i < dpdk_config_args.queue_size; i++) {
        std::shared_ptr<pkt_processor_t> pkt_processor = std::make_shared<pkt_processor_t>(i); // 创建 pkt_processor 对象，i 为队列编号
        pkt_processors.push_back(pkt_processor);
    }
    std::cout << "Pkt processor initialized" << std::endl;

    // 13. 初始化两个 sockaddr_in，用于绑定本地端口和连接交换机
    sockaddr_in controller_addr, switch_addr;
    memset(&controller_addr, 0, sizeof(controller_addr));
    controller_addr.sin_family = AF_INET; // AF_INET 表示使用 IPv4 协议，sin_family 为地址族
    controller_addr.sin_port = htons(local_switch_port); // htons 将主机字节序转换为网络字节序，sin_port 为端口号
    controller_addr.sin_addr.s_addr = inet_addr(local_ip.c_str()); // inet_addr 将点分十进制的 IP 地址转换为网络字节序，sin_addr 为 IP 地址
    memset(&switch_addr, 0, sizeof(switch_addr));
    switch_addr.sin_family = AF_INET;
    switch_addr.sin_port = htons(switch_port);
    switch_addr.sin_addr.s_addr = inet_addr(switch_ip.c_str());
    std::cout << "socketin成功了" << std::endl;


    // 14. 创建 socket，用于连接交换机，并绑定本地端口
    int switch_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(switch_fd == -1) {
        perror("socket");
        return -1;
    }
    std::cout << "socket成功了" << std::endl;


    //bind的作用是将socket与本地地址绑定，这样socket就可以接收到发送到本地地址的数据包
    if(bind(switch_fd, (struct sockaddr *)&controller_addr, sizeof(controller_addr)) == -1) {
        perror("bind");
        return -1;
    }
    std::cout << "bind成功了" << std::endl;

    // 15. 启动各个数据包处理器线程
    for(int i = 0; i < pkt_processor_core_ids.size(); ++i) {

        std::cout << "Pkt processor " << i << " starting" << std::endl;
        pkt_processors[i]->run(pkt_processor_core_ids[i]);
        std::cout << "Pkt processor " << i << " started" << std::endl;
    }
    std::cout << "Pkt processor started" << std::endl;

    // 16. 连接交换机
    if(connect(switch_fd, (struct sockaddr *)&switch_addr, sizeof(switch_addr)) == -1) {
        std::cout<<switch_fd<<std::endl;
        std::cout<<"连接有问题"<<std::endl;
        perror("connect");
        return -1;
    }

    std::cout << "Connection established" << std::endl;


    // 17. 将 switch_fd 添加到 epoll 监听事件中，用于监听交换机的事件
    epoll_event sock_ev;
    sock_ev.events = EPOLLIN;
    sock_ev.data.fd = switch_fd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, switch_fd, &sock_ev) == -1) {
        perror("epoll_ctl");
        return -1;
    }

    // 18. 创建第一个 timer_fd (timer_fd)，周期为 100 微秒，用于将离线数据发送到交换机
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(timer_fd == -1) {
        perror("timerfd_create");
        return -1;
    }

    itimerspec timer_spec;
    timer_spec.it_interval.tv_nsec = 100000; // 重复间隔
    timer_spec.it_value.tv_nsec = 100000;    // 初始启动延迟
    timer_spec.it_interval.tv_sec = 0;
    timer_spec.it_value.tv_sec = 0;
    if(timerfd_settime(timer_fd, 0, &timer_spec, NULL) == -1) {
        perror("timerfd_settime");
        return -1;
    }

    // 19. 将 timer_fd 加入 epoll 监听
    epoll_event timer_ev;
    timer_ev.events = EPOLLIN;
    timer_ev.data.fd = timer_fd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &timer_ev) == -1) {
        perror("epoll_ctl");
        return -1;
    }

    // 20. 创建第二个定时器 (timer_fd_2)，每 1 秒触发一次，用于定期查询并打印 DPDK 网卡统计信息
    itimerspec timer_spec_2;
    timer_spec_2.it_interval.tv_nsec = 0;
    timer_spec_2.it_value.tv_nsec = 0;
    timer_spec_2.it_interval.tv_sec = 1;
    timer_spec_2.it_value.tv_sec = 1;
    int timer_fd_2 = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(timer_fd_2 == -1) {
        perror("timerfd_create");
        return -1;
    }
    if(timerfd_settime(timer_fd_2, 0, &timer_spec_2, NULL) == -1) {
        perror("timerfd_settime");
        return -1;
    }

    epoll_event timer_ev_2;
    timer_ev_2.events = EPOLLIN;
    timer_ev_2.data.fd = timer_fd_2;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd_2, &timer_ev_2) == -1) {
        perror("epoll_ctl");
        return -1;
    }

    engine->connect_to_tofino(switch_fd);


    // 21. 进入主循环，通过 epoll_wait 同时监听交换机 FD 和两个定时器 FD
    // 如果有数据到达 switch_fd，就根据协议进行规则更新，或虚拟服务器信息处理
    // 如果 timer_fd 到期，就将离线队列中的数据（offloaded entries）发送给交换机
    // 如果 timer_fd_2 到期，就查询 DPDK 端口统计数据并打印
    char send_buffer[4096], recv_buffer[4096];
    my_pair_t add_pairs[1024];
    my_key_t del_keys[1024];
    epoll_event events[2];
    while(true) {
        int nfds = epoll_wait(epoll_fd, events, 2, -1); //nfds 表示就绪事件的数量，-1 表示阻塞直到有事件发生
        if(nfds == -1) {
            perror("epoll_wait");
            return -1;
        }
        if(nfds > 0) {
            for(int i = 0; i < nfds; i++) {
                if(events[i].data.fd == switch_fd) {
                    ssize_t recv_size = recv(switch_fd, recv_buffer, 1, 0);
                    if (recv_size == -1) {
                        perror("recv");
                        return -1;
                    }
                    if(recv_size == 0) {
                        std::cerr << "Connection closed by switch" << std::endl;
                        return -1;
                    }
                    operation_type_t op = (operation_type_t)recv_buffer[0];
                    std::cout << "Operation type:" << (int)op << std::endl;
                    switch(op) {
                        case RDMA_ONLINE: {
                            while (recv_size < 3109) {
                                recv_size += recv(switch_fd, recv_buffer + recv_size, 3109 - recv_size, 0);
                            }
                            engine->new_frontend_rdma_launched(recv_buffer + 1, flow_hash_map);
                            break;
                        }
                        case RDMA_OFFLINE: {
                            engine->frontend_rdma_offline(flow_hash_map);
                            break;
                        }
                        case QPN_FROM_REMOTE: {
                            // QPN_FROM_REMOTE + src_index + dst_index + remote_qpn
                            // 1 + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t)
                            while(recv_size < 13) {
                                recv_size += recv(switch_fd, recv_buffer + recv_size, 13 - recv_size, 0);
                            }
                            engine->ready_to_receive_remote_qpn(recv_buffer + 1);
                            break;
                        }
                        case UPDATE_RULE: {
                            // recv_buffer[0]           recv_buffer[1]           recv_buffer[2]
                            // [ 操作类型(op) ]      [新增规则个数(add_size)]    [删除规则个数(del_size)]
                            // [新增规则1的d_index]  [规则1数据: mac][规则1数据: ip][规则1数据: port]
                            // [新增规则2的d_index]  [规则2数据: mac][规则2数据: ip][规则2数据: port]
                            // ...
                            // [删除规则1的d_index]
                            // [删除规则2的d_index]
                            // ...
                            while(recv_size < 180) {
                                recv_size += recv(switch_fd, recv_buffer + recv_size, 180 - recv_size, 0);
                            }
                        
                            uint8_t add_size = recv_buffer[1];
                            uint8_t del_size = recv_buffer[2];
                            size_t now_bytes = 3;
                            for(int i = 0; i < add_size; ++i) {
                                char *key = recv_buffer + now_bytes;
                                now_bytes += strlen(key) + 1;
                                my_data_t *data = new my_data_t;
                                data->d_index = recv_buffer[now_bytes];
                                now_bytes += 1;
                                data->offload_flag = recv_buffer[now_bytes];
                                now_bytes += 1;
                                rule_controller->add_balancing_rule(key, data);
                            }
                            for(int i = 0; i < del_size; ++i) {
                                char *key = recv_buffer + now_bytes;
                                rule_controller->del_balancing_rule(key);
                                now_bytes += strlen(key) + 1;
                            }
                            break;
                        }
                        case UPDATE_V_INFO: {
                            while(recv_size < sizeof(server_info_t) + 1) {
                                recv_size += recv(switch_fd, recv_buffer + recv_size, sizeof(server_info_t) + 1 - recv_size, 0);
                            }
                            rule_controller->update_virtual_server_info((server_info_t *)(recv_buffer + 1));
                            break;
                        }
                        case UPDATE_D_INDEX: {
                        // recv_buffer[0]           recv_buffer[1]           recv_buffer[2]
                        // [ 操作类型(op) ]      [新增规则个数(add_size)]    [删除规则个数(del_size)]
                        // [新增规则1的key字符串][规则1数据: d_index][规则1数据: offload_flag]
                        // [新增规则2的key字符串][规则2数据: d_index][规则2数据: offload_flag]
                        // ...
                        // [删除规则1的key字符串]
                        // [删除规则2的key字符串]
                        // ...
                            recv(switch_fd, recv_buffer + recv_size, 2, 0);
                            recv_size += 2;
                            uint8_t add_size = recv_buffer[1];
                            uint8_t del_size = recv_buffer[2];
                            size_t now_bytes = 3;
                            size_t total_size = 3 + add_size * (sizeof(server_info_t) + 1) + del_size;
                            while(recv_size < total_size) {
                                recv_size += recv(switch_fd, recv_buffer + recv_size, total_size - recv_size, 0);
                            }
                            for(int i = 0; i < add_size; ++i) {
                                uint8_t index = recv_buffer[now_bytes];
                                now_bytes += 1;
                                server_info_t *server_info = new server_info_t;
                                memcpy(server_info, recv_buffer + now_bytes, sizeof(server_info_t));
                                rule_controller->add_d_index_rule(index, server_info);
                                now_bytes += sizeof(server_info_t);
                            }
                            for(int i = 0; i < del_size; ++i) {
                                uint8_t index = recv_buffer[now_bytes];
                                rule_controller->del_d_index_rule(index);
                                now_bytes += 1;
                            }
                            break;
                        }
                        default:
                            std::cerr << "Unknown operation type:" << (int)op << std::endl;
                            break;
                    }
                }
                else if(events[i].data.fd == timer_fd) {
                    uint64_t exp;
                    ssize_t s = read(timer_fd, &exp, 8);
                    if(s == -1) {
                        perror("read");
                        return -1;
                    }
                    ssize_t send_size = entry_controller->get_offloaded_entries(send_buffer);
                    #ifdef DEBUG
                    if(send_size != 0)
                        std::cout << "Send size:" << send_size << std::endl;
                    #endif
                    if(send_size == -1) {
                        return -1;
                    }
                    if(send_size == 0) {
                        continue;
                    }
                    
                    if(send(switch_fd, send_buffer, send_size, 0) == -1) {
                        perror("send");
                        return -1;
                    }
                    
                }
                else if(events[i].data.fd == timer_fd_2) {
                    uint64_t exp;
                    ssize_t s = read(timer_fd_2, &exp, 8);
                    if(s == -1) {
                        perror("read");
                        return -1;
                    }
                    rte_eth_stats stats;
                    if(rte_eth_stats_get(pkt_processor_t::port_id, &stats) == -1) {
                        perror("rte_eth_stats_get");
                        return -1;
                    }

                    engine->send_rdma_heartbeat(my_name);

                    continue;

                    std::cout<<"目前的规则内容："<<std::endl;
                    for (auto it = rule_controller->rule_map.begin(); it != rule_controller->rule_map.end(); ++it) {
                        std::cout << it->first << " " << it->second->d_index << " " << it->second->offload_flag << std::endl;
                    }
                    std::cout<<"后端服务器信息："<<std::endl;
                    for (auto it = rule_controller->d_index_map.begin(); it != rule_controller->d_index_map.end(); ++it) {
                        std::cout << it->first << " " << it->second->ip << " " << it->second->port << std::endl;
                    }
                    std::cout << "虚拟服务器信息"<< std::endl;
                    std::cout << rule_controller->get_virtual_server_info()->ip <<std::endl;
                    std::cout << rule_controller->get_virtual_server_info()->port <<std::endl;
                    
                    std::cout << "Received packets:" << stats.ipackets << std::endl;
                    std::cout << "Sent packets:" << stats.opackets << std::endl;
                    std::cout << "Received bytes:" << stats.ibytes << std::endl;
                    std::cout << "Sent bytes:" << stats.obytes << std::endl;
                    std::cout << "Received errors:" << stats.ierrors << std::endl;
                    std::cout << "Sent errors:" << stats.oerrors << std::endl;
                    std::cout << "Drop:" << stats.imissed << std::endl;
                    std::cout << "Received mbuf allocation errors:" << stats.rx_nombuf << std::endl;
                    // for(int i = 0; i < dpdk_config_args.queue_size; ++i) {
                    //     std::cout << "Queue " << i << " received packets:" << stats.q_ipackets[i] << std::endl;
                    //     std::cout << "Queue " << i << " sent packets:" << stats.q_opackets[i] << std::endl;
                    //     std::cout << "Queue " << i << " received bytes:" << stats.q_ibytes[i] << std::endl;
                    //     std::cout << "Queue " << i << " sent bytes:" << stats.q_obytes[i] << std::endl;
                    //     std::cout << "Queue " << i << " received errors:" << stats.q_errors[i] << std::endl;
                    // }
                }
                
            }
        }
    }
    return 0;
}