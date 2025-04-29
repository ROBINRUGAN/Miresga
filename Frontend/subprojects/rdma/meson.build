project('rdma', 'cpp',
    version: '1.0.0',
    default_options: ['cpp_std=c++17', 'optimization=3', 'warning_level=3'])

cmake = import('cmake')
rdma_inc = include_directories(['include', '../..'])

src_files = files(
    'buffer.cpp',
    'engine.cpp',
    'logger.cpp'
)

glog_dep = dependency('libglog', required: true)
gflags_dep = dependency('gflags', required: true)
ibverbs_dep = dependency('libibverbs', required: true)
pthread_dep = dependency('threads', required: true)
libcuckoo_pro = cmake.subproject('libcuckoo')
libcuckoo_dep = libcuckoo_pro.dependency('libcuckoo')

rdma_lib = static_library('rdma', src_files,
    install: true,
    include_directories: rdma_inc,
    dependencies: [glog_dep, gflags_dep, ibverbs_dep, pthread_dep, libcuckoo_dep]
)

rdma_dep = declare_dependency(
    include_directories: rdma_inc,
    link_with: rdma_lib
)
