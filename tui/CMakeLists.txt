# 获取tui的源文件
file(GLOB TUI_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# 获取protobuf生成的.cc文件
file(GLOB PROTO_SRCS ${CMAKE_SOURCE_DIR}/common/*.pb.cc)

# FTXUI头文件和库路径
include_directories(${CMAKE_SOURCE_DIR}/third_party/FTXUI/include)
link_directories(${CMAKE_SOURCE_DIR}/third_party/FTXUI/build)

# 创建可执行文件
add_executable(tui ${TUI_SRCS} ${PROTO_SRCS})

# 链接库（LIBS在主CMakeLists定义 + FTXUI库）
target_link_libraries(tui 
    ${LIBS}
    ftxui-component
    ftxui-dom
    ftxui-screen
)