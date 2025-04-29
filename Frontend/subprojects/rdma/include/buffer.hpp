#ifndef BUFFER_HPP
#define BUFFER_HPP

#include "logger.hpp"
#include <cstdlib>
#include <infiniband/verbs.h>
#include <iostream>

namespace rdma {
class Buffer {
private:
    
    void* _addr;
    size_t _size;
public:
    ibv_mr* _mr;
    Buffer() = delete;
    Buffer(Buffer&&) = delete;
    Buffer(const Buffer&) = delete;
    Buffer(size_t seq_length, size_t batch_size, size_t head_dim, size_t repeat_time, size_t element_size, ibv_pd* pd);

    ~Buffer();

    Buffer& operator=(Buffer&&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    
    void* get_addr() const;
    const size_t get_size() const;
    const uint32_t get_lkey() const;
    const uint32_t get_rkey() const;
};
}

#endif