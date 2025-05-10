#ifndef ENGINE_HPP
#define ENGINE_HPP
#define SENDER true
#define RECEIVER false
#include "buffer.hpp"
#include <linux/socket.h>
#include <arpa/inet.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <atomic>
#include "libcuckoo/cuckoohash_map.hh"
#include "flow_data.hpp"


namespace rdma {

struct crc_buffers_t {
    uint64_t remote_buffer_addr;
    uint32_t remote_rkey;
};

struct rdma_info_t {
    ibv_gid remote_gid;
    uint32_t valid;
    uint32_t remote_qpn;
    uint64_t heartbeat_addr;
    uint32_t heartbeat_rkey;
    crc_buffers_t crc_buffers[256];
};
 
class Engine {
private:

    ibv_context* _ctx;
    ibv_pd* _pd;
    ibv_cq* _cq;
    ibv_gid _gid;
    int switch_fd;
    rdma_info_t rdma_info[1024];
    int rdma_info_nums = 1;
    Buffer* buffer[256];
    Buffer* heartbeat;

    ibv_qp *qp[1024];
    uint32_t local_qpn[1024];

public:

    Engine() = delete;
    Engine(Engine&&) = delete;
    Engine(const Engine&) = delete;
    Engine(const char *dev_name);

    ~Engine();

    Engine& operator=(Engine&&) = delete;
    Engine& operator=(const Engine&) = delete;

    void connect_to_tofino(int sock_fd);
    void send_rdma_heartbeat(std::string my_name);
    uint32_t create_qp(u_int32_t index);
    void ready_to_send_remote_qpn(uint32_t src_index, uint32_t dst_index);
    void ready_to_receive_remote_qpn(char *buffer);
    void new_frontend_rdma_launched(char *buffer, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map);
    void frontend_rdma_offline(libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map);
    void send_rdma_buffer(uint8_t crc, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map);
    void serialize_crc_buffer(uint8_t crc, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map);
    void update_from_crc_buffer(uint8_t crc, libcuckoo::cuckoohash_map<uint64_t, flow_data_t *> *flow_hash_map);
};  

}

#endif