# 获取collector的源文件, GLOB表示通配符!!! glob pattern(通配符模式), 而不是global全局变量的意思.
file(GLOB CENTER_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# 获取protobuf生成的.cc文件
file(GLOB PROTO_SRCS ${CMAKE_SOURCE_DIR}/common/*.pb.cc)

# 创建可执行文件
add_executable(collector ${CENTER_SRCS} ${PROTO_SRCS})

# 链接库（LIBS在主CMakeLists定义）
target_link_libraries(collector ${LIBS})

# 一开始我觉得这个没der用, 这样配置是因为日本的libgcc和libstdc++与这里的版本不同.
# target_link_options(collector PRIVATE -static-libgcc -static-libstdc++)
# target_link_options(collector PRIVATE -static)