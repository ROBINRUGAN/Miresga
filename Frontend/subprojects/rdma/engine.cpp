#include "engine.hpp"

// init rdma engine，and get gid，pd, cq
rdma::Engine::Engine(const char *dev_name)
{
    ibv_device **dev_list;
    int num_devices;
    dev_list = ibv_get_device_list(&num_devices);
    if (dev_list == nullptr)
    {
        std::cout << "Failed to get device list" << std::endl;
    }
    ibv_device *dev = nullptr;
    for (int i = 0; i < num_devices; i++)
    {
        if (strcmp(ibv_get_device_name(dev_list[i]), dev_name) == 0)
        {
            dev = dev_list[i];
            break;
        }
    }
    if (dev == nullptr)
    {
        std::cout << "Failed to find device" << std::endl;
    }

    _ctx = ibv_open_device(dev);
    if (_ctx == nullptr)
    {
        std::cout << "Failed to open device" << std::endl;
    }
    std::cout << "Using device: " << dev_name << std::endl;

    ibv_free_device_list(dev_list);
    _pd = ibv_alloc_pd(_ctx);
    if (_pd == nullptr)
    {
        std::cout << "Failed to allocate protection domain" << std::endl;
    }
    std::cout << "Allocated protection domain" << std::endl;

    _cq = ibv_create_cq(_ctx, 10, nullptr, nullptr, 0);
    if (_cq == nullptr)
    {
        std::cout << "Failed to create completion queue" << std::endl;
    }
    std::cout << "Created completion queue" << std::endl;

    if (ibv_query_gid(_ctx, 1, 3, &_gid) )
    {
        std::cout << "Failed to query gid" << std::endl;
    }
    std::cout << "Local gid:" << std::hex << _gid.global.interface_id << std::dec << std::endl;
}

void rdma::Engine::serialize_crc_buffer(uint8_t crc, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map)
{

    uint8_t *buf_ptr = static_cast<uint8_t *>(buffer[crc]->get_addr());
    size_t offset = sizeof(uint32_t); // 4 Byte for valid_len
    auto &map = flow_hash_map[crc];

    auto lt = map.lock_table();
    for (auto it = lt.begin(); it != lt.end(); ++it)
    {
        const uint64_t &key = it->first;
        flow_data_t *value = it->second;
        if (!value || !value->recv_pkt)
            continue;

        size_t entry_size = sizeof(flow_entry_serialized) + value->pkt_size;
        if (offset + entry_size > buffer[crc]->get_size())
            break;

        flow_entry_serialized entry;
        entry.key = key;
        entry.state = value->state;
        entry.entry_data = value->entry_data;
        entry.pkt_size = value->pkt_size;

        memcpy(buf_ptr + offset, &entry, sizeof(entry));
        offset += sizeof(entry);
        memcpy(buf_ptr + offset, value->recv_pkt, value->pkt_size);
        offset += value->pkt_size;
    }

    uint32_t valid_len = offset - sizeof(uint32_t);
    memcpy(buf_ptr, &valid_len, sizeof(uint32_t));
    // if (valid_len > 0)
        std::cout << "Serialized buffer size: " << valid_len << std::endl;
}

// valid len  + entry(s) + pkt
void rdma::Engine::update_from_crc_buffer(uint8_t crc, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map)
{
    uint8_t *buf_ptr = static_cast<uint8_t *>(buffer[crc]->get_addr());
    size_t offset = 0;

    uint32_t valid_len = 0;
    memcpy(&valid_len, buf_ptr, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // at least have one flow to extract
    while (offset + sizeof(flow_entry_serialized) <= valid_len + sizeof(uint32_t))
    {
        flow_entry_serialized entry;
        memcpy(&entry, buf_ptr + offset, sizeof(entry));
        offset += sizeof(entry);

        if (offset + entry.pkt_size > valid_len + sizeof(uint32_t))
            break;

        flow_data_t *flow_data = (flow_data_t *)rte_malloc_socket("flow_data", sizeof(flow_data_t), 0, 3);
        flow_data->state = entry.state;
        flow_data->entry_data = entry.entry_data;
        flow_data->pkt_size = entry.pkt_size;
        flow_data->recv_pkt = (char *)rte_malloc_socket("recv_pkt", entry.pkt_size, 0, 3);
        memcpy(flow_data->recv_pkt, buf_ptr + offset, entry.pkt_size);
        offset += entry.pkt_size;

        flow_hash_map[crc].insert_or_assign(entry.key, flow_data);
    }
    printf("Updated flow_hash_map[%d] with %d entries\n", crc, valid_len);
}

void rdma::Engine::frontend_rdma_offline(libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map)
{
    for (int i = 0; i < 256; i++)
    {
        update_from_crc_buffer(i, flow_hash_map);
    }
}

void rdma::Engine::send_rdma_heartbeat(std::string my_name)
{
    // 读取heartbeat的字符串内容
    // std::string heartbeat_str(static_cast<char *>(heartbeat->get_addr()));
    // std::cout << "Heartbeat string: " << heartbeat_str << std::endl;

    // return;
    for (int k = 0; k < rdma_info_nums; k++)
    {
        if (rdma_info[k].valid == 0)
            continue;
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr;
        memset(&attr, 0, sizeof(attr));
        memset(&init_attr, 0, sizeof(init_attr));
        if (ibv_query_qp(qp[k], &attr, IBV_QP_STATE, &init_attr) == 0) {
            std::cout << "QP current state: " << attr.qp_state << std::endl;
            if(attr.qp_state != IBV_QPS_RTS) {
                std::cout << "QP state is not prepared, skipping heartbeat send" << std::endl;
                continue;
            }
        } else {
            std::cerr << "Failed to query QP state" << std::endl;
        }
        
        // std::cout << "my lkey: " << heartbeat->get_lkey() << std::endl;
        // std::cout << "my addr: " << (uint64_t)heartbeat->get_addr() << std::endl;
        // std::cout << "my size: " << heartbeat->get_size() << std::endl;

        // // 将string my_name写入heartbeat
        // char* hb_buf = static_cast<char*>(heartbeat->get_addr());
        // memcpy(hb_buf, my_name.data(), my_name.size());
        // hb_buf[my_name.size()] = '\0'; // 添加字符串结束符
        

        // std::cout << "remote rkey: " << rdma_info[k].heartbeat_rkey << std::endl;
        // std::cout << "remote addr: " << rdma_info[k].heartbeat_addr << std::endl;
        // std::cout << "remote size: " << heartbeat->get_size() << std::endl;

        struct ibv_sge sge;
        memset(&sge, 0, sizeof(sge));
        sge.addr = (uint64_t)heartbeat->get_addr();
        sge.length = heartbeat->get_size();
        sge.lkey = heartbeat->get_lkey();

        struct ibv_send_wr send_wr;
        memset(&send_wr, 0, sizeof(send_wr));
        send_wr.wr_id = ((uint64_t)k << 16); // 正确标识
        send_wr.sg_list = &sge;
        send_wr.num_sge = 1;
        send_wr.opcode = IBV_WR_RDMA_WRITE;
        send_wr.send_flags = IBV_SEND_SIGNALED;
        send_wr.wr.rdma.remote_addr = rdma_info[k].heartbeat_addr;
        send_wr.wr.rdma.rkey = rdma_info[k].heartbeat_rkey;

        struct ibv_send_wr *bad_wr;
        int ret = ibv_post_send(qp[k], &send_wr, &bad_wr);
        if (ret)
        {
            std::cerr << "Failed to post heartbeat to node " << k
                      << ", error: " << strerror(errno) << std::endl;
            continue;
        }

        // std::cout << "sent heartbeats " << k << std::endl;
    }
    struct ibv_wc wc_array[256]; // 拉取用的数组
    int n = ibv_poll_cq(_cq, 256, wc_array);
    if (n < 0)
    {
        std::cerr << "Error polling CQ: " << strerror(errno) << std::endl;
        return;
    }

    for (int i = 0; i < n; i++)
    {
        struct ibv_wc &wc = wc_array[i];
        if (wc.status != IBV_WC_SUCCESS)
        {
            int wrid = wc.wr_id;
            int failed_k = (wrid >> 16) & 0xFFFF; 
            // int failed_crc = wrid & 0xFFFF;     
            std::cerr << "Heartbeat send failed for node " << failed_k
                      << ", wc.status = " << wc.status << "vendor_err= "<< wc.vendor_err << std::endl;
            
            rdma_info[failed_k].valid = 0;
            // rdma_info_nums--;
            // struct ibv_qp_attr attr;
            // attr.qp_state = IBV_QPS_RESET;

            // if(ibv_modify_qp(qp[i], &attr, IBV_QP_STATE))
            // {
            //     std::cerr << "Failed to modify queue pair to RESET: errno=" << errno
            //     << " (" << strerror(errno) << ")" << std::endl;
            // }
            
        }
        else
        {
            std::cout << "SUCCESSFUL heartbeat send for node " << (wc.wr_id >> 16) << std::endl;
        }
    }
}

void rdma::Engine::send_rdma_buffer(uint8_t crc, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map)
{
    for (int k = 0; k < rdma_info_nums; k++)
    {
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr;
        memset(&attr, 0, sizeof(attr));
        memset(&init_attr, 0, sizeof(init_attr));
        if (ibv_query_qp(qp[k], &attr, IBV_QP_STATE, &init_attr) == 0) {
            std::cout << "QP current state: " << attr.qp_state << std::endl;
        } else {
            std::cerr << "Failed to query QP state" << std::endl;
        }

        std::cout << "my lkey: " << buffer[crc]->get_lkey() << std::endl;
        std::cout << "my addr: " << (uint64_t)buffer[crc]->get_addr() << std::endl;
        std::cout << "my size: " << buffer[crc]->get_size() << std::endl;
        std::cout << "my rkey: " << buffer[crc]->get_rkey() << std::endl;
        std::cout << "remote rkey: " << rdma_info[k].crc_buffers[crc].remote_rkey << std::endl;
        std::cout << "remote addr: " << rdma_info[k].crc_buffers[crc].remote_buffer_addr << std::endl;
        std::cout << "remote size: " << buffer[crc]->get_size() << std::endl;

        ibv_sge sge;
        memset(&sge, 0, sizeof(sge));
        sge.addr = (uint64_t)buffer[crc]->get_addr();
        sge.length = buffer[crc]->get_size();
        sge.lkey = buffer[crc]->get_lkey();
        ibv_send_wr send_wr;
        memset(&send_wr, 0, sizeof(send_wr));
        send_wr.wr_id = ((uint64_t)k << 16) | crc; // 正确标识
        send_wr.sg_list = &sge;
        send_wr.num_sge = 1;
        send_wr.opcode = IBV_WR_RDMA_WRITE;
        send_wr.send_flags = IBV_SEND_SIGNALED;
        send_wr.wr.rdma.remote_addr = rdma_info[k].crc_buffers[crc].remote_buffer_addr;
        send_wr.wr.rdma.rkey = rdma_info[k].crc_buffers[crc].remote_rkey;
        ibv_send_wr *bad_wr;
        int ret = ibv_post_send(qp[k], &send_wr, &bad_wr);
        if (ret)
        {
            std::cerr << "Failed to post buffer to node " << k
                      << ", error: " << strerror(errno) << std::endl;
            continue;
        }
    }
    struct ibv_wc wc_array[256]; // 拉取用的数组
    int n = ibv_poll_cq(_cq, 256, wc_array);
    if (n < 0)
    {
        std::cerr << "Error polling CQ: " << strerror(errno) << std::endl;
        return;
    }
    for (int i = 0; i < n; i++)
    {
        struct ibv_wc &wc = wc_array[i];
        if (wc.status != IBV_WC_SUCCESS)
        {
            int wrid = wc.wr_id;
            int failed_k = (wrid >> 16) & 0xFFFF; // 正确提取node id
            int failed_crc = wrid & 0xFFFF;       // 正确提取crc id
            std::cerr << "Buffer send failed for node " << failed_k
                      << ", crc " << failed_crc
                      << ", wc.status = " << wc.status << std::endl;
        }
        else
        {
            std::cout << "SUCCESSFUL Buffer send for node " << (wc.wr_id >> 16)
                      << ", crc " << (wc.wr_id & 0xFFFF) << std::endl;
        }
    }
}

void rdma::Engine::new_frontend_rdma_launched(char *buf, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map)
{
    // src is remote server, dst is me
    uint32_t src_index;
    uint32_t dst_index;
    uint32_t invalid;
    ibv_gid remote_gid;
    uint64_t heartbeat_addr;
    uint32_t heartbeat_rkey;
    // FRONTEND_ONLINE(4) + src_index + dst_index + data
    memcpy(&src_index, buf, sizeof(uint32_t));
    memcpy(&dst_index, buf + sizeof(uint32_t), sizeof(uint32_t));
    memcpy(&remote_gid, buf + sizeof(uint32_t) + sizeof(uint32_t), sizeof(ibv_gid));
    memcpy(&heartbeat_addr, buf + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(ibv_gid), sizeof(uint64_t));
    memcpy(&heartbeat_rkey, buf + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(ibv_gid) + sizeof(uint64_t), sizeof(uint32_t));

    rdma_info[src_index].remote_gid = remote_gid;
    rdma_info[src_index].heartbeat_addr = heartbeat_addr;
    rdma_info[src_index].heartbeat_rkey = heartbeat_rkey;
    rdma_info[src_index].valid = 1;

    int offset =  sizeof(uint32_t) + sizeof(uint32_t) + sizeof(ibv_gid) + sizeof(uint64_t) + sizeof(uint32_t);
    for (int i = 0; i < 256; i++)
    {
        uint64_t buffer_addr;
        uint32_t buffer_rkey;
        memcpy(&buffer_addr, buf + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        memcpy(&buffer_rkey, buf + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        rdma_info[src_index].crc_buffers[i].remote_buffer_addr = buffer_addr;
        rdma_info[src_index].crc_buffers[i].remote_rkey = buffer_rkey;
        std::cout << "crc buffer " << i << " addr:" << std::hex << buffer_addr << std::dec << std::endl;
        std::cout << "crc buffer " << i << " rkey:" << std::hex << buffer_rkey << std::dec << std::endl;
    }

    rdma_info_nums++;
    std::cout << "Local gid:" << std::hex << _gid.global.interface_id  << std::dec << std::endl;
    std::cout << "ready to connect to remote gid:" << std::hex << remote_gid.global.interface_id << std::dec << std::endl;
    
    ready_to_send_remote_qpn(dst_index, src_index);
}

uint32_t rdma::Engine::create_qp(u_int32_t index)
{
    qp[index] = new ibv_qp;
    // init queue pairs params!
    ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = _cq;
    qp_init_attr.recv_cq = _cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap = {
        .max_send_wr = 10,
        .max_recv_wr = 10,
        .max_send_sge = 1,
        .max_recv_sge = 1};
    qp_init_attr.sq_sig_all = 1;
    ibv_qp_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.port_num = 1;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    // create queue pairs

    qp[index] = ibv_create_qp(_pd, &qp_init_attr);
    if (qp == nullptr)
    {
        std::cout << "Failed to create queue pair";
    }
    if (ibv_modify_qp(qp[index], &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) )
    {
        std::cout << "Failed to modify queue pair to INIT";
    }
    local_qpn[index] = qp[index]->qp_num;
    return local_qpn[index];
}

void rdma::Engine::ready_to_send_remote_qpn(u_int32_t src_index, uint32_t dst_index)
{
    // now src is me, created qpn for dst server, ready to send to dst
    if(qp[dst_index] == nullptr) create_qp(dst_index);
    // src_index + dst_index + local_qpn
    char send_buffer[1024];
    send_buffer[0] = 5; //QPN_SEND
    memcpy(send_buffer + 1, &src_index, sizeof(uint32_t));
    memcpy(send_buffer + 1 + sizeof(uint32_t), &dst_index, sizeof(uint32_t));
    memcpy(send_buffer + 1 + sizeof(uint32_t) + sizeof(uint32_t), &local_qpn[dst_index], sizeof(uint32_t));
    if(send(switch_fd, send_buffer, sizeof(uint32_t) * 2 + 1 + sizeof(uint32_t), 0) < 0)
    {
        std::cout << "Failed to send local qpn" << std::endl;
    }
    else
    {
        std::cout << "Send local qpn: " << local_qpn[dst_index] << std::endl;
    }

}


void rdma::Engine::ready_to_receive_remote_qpn(char *buffer)
{
    // src_index + dst_index + remote_qpn
    // now is src server's qpn sends to me(dst)
    uint32_t src_index;
    uint32_t dst_index;
    uint32_t remote_qpn;
    memcpy(&src_index, buffer, sizeof(uint32_t));
    memcpy(&dst_index, buffer + sizeof(uint32_t), sizeof(uint32_t));
    memcpy(&remote_qpn, buffer + sizeof(uint32_t) + sizeof(uint32_t), sizeof(uint32_t));
    std::cout << "src_index:" << src_index << std::endl;
    std::cout << "dst_index:" << dst_index << std::endl;
    std::cout << "remote_qpn:" << remote_qpn << std::endl;
    rdma_info[src_index].remote_qpn = remote_qpn;

    // return;
    if(qp[src_index] == nullptr) create_qp(src_index);

    ibv_qp_attr qp_attr;
    qp_attr.port_num = 1;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    // memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IBV_QPS_RTR;
    qp_attr.path_mtu = IBV_MTU_1024;
    qp_attr.dest_qp_num = remote_qpn;
    qp_attr.max_dest_rd_atomic = 1;
    qp_attr.rq_psn = 0;
    qp_attr.min_rnr_timer = 12;
    qp_attr.ah_attr.is_global = 1;
    qp_attr.ah_attr.grh.dgid = rdma_info[src_index].remote_gid;
    qp_attr.ah_attr.grh.sgid_index = 3; // 3 for rocev2!!!!!!!!!!!!!!!!!!!!!
    qp_attr.ah_attr.grh.hop_limit = 64;
    qp_attr.ah_attr.port_num = 1;
    if (ibv_modify_qp(qp[src_index], &qp_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC) )
    {
        std::cerr << "Failed to modify queue pair to RTR: errno=" << errno
        << " (" << strerror(errno) << ")" << std::endl;
    }
    else std::cout << "Modified queue pair to RTR" << std::endl;
    // memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.qp_state = IBV_QPS_RTS;
    qp_attr.timeout = 14;
    qp_attr.retry_cnt = 7;
    qp_attr.rnr_retry = 7;
    qp_attr.sq_psn = 0;
    qp_attr.max_rd_atomic = 1;
    if (ibv_modify_qp(qp[src_index], &qp_attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC) )
    {
        std::cout << "Failed to modify queue pair to RTS" << std::endl;
    }
    else std::cout << "Modified queue pair to RTS" << std::endl;

            struct ibv_qp_attr attr;
            struct ibv_qp_init_attr init_attr;
            memset(&attr, 0, sizeof(attr));
            memset(&init_attr, 0, sizeof(init_attr));
            if (ibv_query_qp(qp[src_index], &attr, IBV_QP_STATE, &init_attr) == 0) {
                std::cout << "QP current state: " << attr.qp_state << std::endl;
            } else {
                std::cerr << "Failed to query QP state" << std::endl;
            }
    std::cout << "Ready to send data" << std::endl;
}

void rdma::Engine::connect_to_tofino(int sock_fd)
{
    switch_fd = sock_fd;
    std::cout << "Connect to all servers" << std::endl;
    for (int i = 0; i < 256; i++)
    {
        buffer[i] = new Buffer(1024, 8, 1536, 1, 2, _pd);
    }
    heartbeat = new Buffer(1024, 8, 1536, 1, 2, _pd);

    std::cout << "heartbeat addr:" << (uint64_t)heartbeat->get_addr() << std::endl;
    std::cout << "heartbeat rkey:" << heartbeat->get_rkey() << std::endl;
    std::cout << "heartbeat size:" << heartbeat->get_size() << std::endl;
    std::cout << "heartbeat lkey:" << heartbeat->get_lkey() << std::endl;


    char sock_buf[ETH_FRAME_LEN * 3];
    // gid + heartbeat addr + heartbeat rkey + 256 * (buffer addr + buffer rkey)
    size_t send_size = sizeof(ibv_gid) + sizeof(uint32_t) + sizeof(uint64_t) + 256 * (sizeof(uint64_t) + sizeof(uint32_t));
    memcpy(sock_buf, &_gid, sizeof(ibv_gid));
    uint64_t heartbeat_addr = (uint64_t)heartbeat->get_addr();
    uint32_t heartbeat_rkey = heartbeat->get_rkey();
    memcpy(sock_buf + sizeof(ibv_gid), &heartbeat_addr, sizeof(uint64_t));
    memcpy(sock_buf + sizeof(ibv_gid) + sizeof(uint64_t), &heartbeat_rkey, sizeof(uint32_t));
    int offset = sizeof(ibv_gid) + sizeof(uint64_t) + sizeof(uint32_t);

    for (int i = 0; i < 256; i++)
    {
        uint64_t buffer_addr = (uint64_t)buffer[i]->get_addr();
        uint32_t buffer_rkey = buffer[i]->get_rkey();
        memcpy(sock_buf + offset, &buffer_addr, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        memcpy(sock_buf + offset, &buffer_rkey, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        // std::cout << "gen buffer " << i << " addr:" << std::hex << buffer_addr << std::dec << std::endl;
        // std::cout << "gen buffer " << i << " rkey:" << std::hex << buffer_rkey << std::dec << std::endl;
    }

    if (send(sock_fd, sock_buf, send_size, 0) < 0)
    {
        std::cout << "Failed to send local rdmainfos to tofino" << std::endl;
    }
}

rdma::Engine::~Engine()
{
    if (_cq != nullptr)
    {
        ibv_destroy_cq(_cq);
    }
    if (_pd != nullptr)
    {
        ibv_dealloc_pd(_pd);
    }
    if (_ctx != nullptr)
    {
        ibv_close_device(_ctx);
    }
}