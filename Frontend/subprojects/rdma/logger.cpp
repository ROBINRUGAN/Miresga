#include "logger.hpp"

DEFINE_string(dev_name, "", "The device name to use, e.g. mlx5_0.");
DEFINE_string(local_ip, "", "The IP address to listen on or connect to.");
DEFINE_int32(local_port, 9999, "The port to listen on or connect to. Default: 9999");
DEFINE_bool(is_sender, false, "Whether to start as sender. Default: false");
DEFINE_string(remote_ip, "", "The IP address to connect to. Only used when is_sender is set to true.");
DEFINE_int32(remote_port, 9999, "The port to connect to. Only used when is_sender is set to true. Default: 9999");
DEFINE_int32(seq_length, 1024, "The sequence length. Default: 1024");
DEFINE_int32(batch_size, 8, "The batch size. Default: 8");
DEFINE_int32(head_dim, 1536, "The head dimension. Default: 1536");
DEFINE_int32(top, 8, "Simulate chose top-k. Default: 8");
DEFINE_bool(mulit_qp, false, "Whether to use multiple queue pairs. Default: false");
DEFINE_int32(num_iter, 100, "The number of iterations. Default: 100");
DEFINE_string(dtype, "FP16", "The data type. Options: [FP32, FP16, BF16, FP8]. Default: FP16");

void init_logger(int argc, char *argv[]) {
    gflags::SetUsageMessage("Usage: --dev_name --local_ip [--local_port] [--is_sender] [--remote_ip] [--remote_port] [--seq_length] [--batch_size] [--head_dim] [--top] [--multi_qp] [--num_iter] [--dtype]");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);
    CHECK(!FLAGS_dev_name.empty()) << "Please specify the device name";
    CHECK(!FLAGS_local_ip.empty()) << "Please specify the local IP address";
    if (FLAGS_is_sender) {
        CHECK(!FLAGS_remote_ip.empty()) << "Please specify the remote IP address";
    }
}

void shutdown_logger() {
    gflags::ShutDownCommandLineFlags();
    google::ShutdownGoogleLogging();
}