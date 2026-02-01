cmake_minimum_required(VERSION 3.15)
project(tui-windows)

set(CMAKE_CXX_STANDARD 17)

# 添加这行：让MSVC使用UTF-8编码 ---- 代码中有些uft-8的字符
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# Protobuf ---- protobuf的release没有提供库文件, 所以要自己编译, 我用的vcpkg(windows msvc那一套玩意儿)来编译protobuf, 用mingw虽然接近linux习惯但也有超多坑例如Protobuf还依赖什么abseil我去.
find_package(Protobuf REQUIRED)

# FTXUI（预编译版本） ---- FTXUI的release提供了库文件, 不用自己编译了, 直接用, Protobuf能给我提供这个库我就不需要这么辛苦了.
set(FTXUI_ROOT "C:/sdk/others/ftxui-6.1.9-win64")
include_directories(${FTXUI_ROOT}/include)
link_directories(${FTXUI_ROOT}/lib)

# monitor.proto是数据格式, KrpcHeader.proto是处理粘包的首部格式定义
add_executable(tui-windows 
    tui-windows.cc
    monitor.pb.cc
    Krpcheader.pb.cc
)

target_link_libraries(tui-windows 
    protobuf::libprotobuf
    ws2_32
    ftxui-screen
    ftxui-dom
    ftxui-component
)

# windows上的编译是真不习惯
# Remove-Item * -Recurse -Force
# cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/sdk/vcpkg/scripts/buildsystems/vcpkg.cmake
# cmake --build . --config Debug
