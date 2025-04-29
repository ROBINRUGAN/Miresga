#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <gflags/gflags.h>
#include <glog/logging.h>

DECLARE_string(dev_name);
DECLARE_string(local_ip);
DECLARE_int32(local_port);
DECLARE_bool(is_sender);
DECLARE_string(remote_ip);
DECLARE_int32(remote_port);
DECLARE_int32(seq_length);
DECLARE_int32(batch_size);
DECLARE_int32(head_dim);
DECLARE_int32(top);
DECLARE_bool(mulit_qp);
DECLARE_int32(num_iter);
DECLARE_string(dtype);

void init_logger(int argc, char *argv[]);
void shutdown_logger();

#endif