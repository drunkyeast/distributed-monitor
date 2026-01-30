# 设置最低版本和项目名称
cmake_minimum_required(VERSION 3.10)
project(distributed-monitor)

# 设置全局的c++标准
set (CMAKE_CXX_STANDARD 17) 
set (CMAKE_CXX_STANDARD_REQUIRED True)
# 设置LSP和Debug相关
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fno-inline")

# 全局头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/third_party/krpc_core/include
    ${CMAKE_SOURCE_DIR}/third_party/muduo_core/include
    ${CMAKE_SOURCE_DIR}/common  # proto头文件
)

# 全局库路径
link_directories(
    ${CMAKE_SOURCE_DIR}/third_party/krpc_core
    ${CMAKE_SOURCE_DIR}/third_party/muduo_core
)

# 全局链接库, LIBS是自定义的变量.
set(LIBS
    krpc_core
    muduo_core
    protobuf
    pthread
    glog
    gflags
    zookeeper_mt
)

# 可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
# 注释掉了, 看看默认输出位置吧. 还是要指定一下, 和配置文件放在一起

# 子目录
add_subdirectory(center)
add_subdirectory(collector)
add_subdirectory(tui)