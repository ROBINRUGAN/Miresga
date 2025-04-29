#include "buffer.hpp"

rdma::Buffer::Buffer(size_t seq_length, size_t batch_size, size_t head_dim, size_t repeat_time, size_t element_size, ibv_pd* pd) {
    // _size = seq_length * batch_size * head_dim * repeat_time * element_size;
    _size = 4096;
    _addr = malloc(_size);
    if (_addr == nullptr) {
        std::cout << "Failed to allocate memory for buffer" <<std::endl;
    }
    _mr = ibv_reg_mr(pd, _addr, _size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (_mr == nullptr) {
        std::cout << "Failed to register memory region" <<std::endl;
    }
}

rdma::Buffer::~Buffer() {
    if (_mr != nullptr) {
        ibv_dereg_mr(_mr);
    }
    if (_addr != nullptr) {
        free(_addr);
    }
}

void* rdma::Buffer::get_addr() const {
    return _addr;
}

const size_t rdma::Buffer::get_size() const {
    return _size;
}

const uint32_t rdma::Buffer::get_lkey() const {
    return _mr->lkey;
}

const uint32_t rdma::Buffer::get_rkey() const {
    return _mr->rkey;
}

