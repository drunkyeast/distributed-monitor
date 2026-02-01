# windows的TUI版本
之前的项目是在linux, 实现了center/collector/tui这三个模块.
这里是我只实现了windows上的tui模块, 因为Krpc框架和muduo-core都是linux版本, windows用不了一点(例如windows没有epoll). 
所以我用不了Krpc框架, 但为了实现RPC的调用, 我在tui-windows-测试网络通信.cc代码中, 写死了IP+Port, 写死了服务名和方法名, 去掉了zookeeper. 测试能够调用RPC服务后, 然后再实现tui-windows.cc.

# 构建的坑
代码写好了, 在编译的过程中又有很多坑, 例如我需要依赖protobuf和FTXUI, FTXUI的release有库文件, 但protobuf的release没有库文件好坑啊, 所以我要自己编译protobuf. 我用的vcpkg这个包管理器, 我一开始以为他能直接下载Protobuf的库, 结果他是拉取源码然后用mscv进行编译, 还强制用msvc. 
没用的知识: 除了mscv, 还可以用mingw去编译, 它与linux的使用习惯更接近, 但也有很多坑, 例如protobuf编译后还依赖什么abseil, 所以放弃了mingw了.

假设protobuf已经构建好了, 库文件路径`C:\sdk\vcpkg\installed\x64-windows\tools\protobuf`
FTXUI下载好了, 库的路径在`C:\sdk\others\ftxui-6.1.9-win64`
然后就可以开始编译了, 反正与linux的习惯不一样, 记也记不住, 到此为止吧.

```sh
cd C:\Users\hp\CodeField2\distributed-monitor\tui-windows
C:\sdk\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe --cpp_out=. .\monitor.proto
C:\sdk\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe --cpp_out=. .\Krpcheader.proto

# windows上的编译是真不习惯
cd C:\Users\hp\CodeField2\distributed-monitor\tui-windows\build
Remove-Item * -Recurse -Force
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/sdk/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Debug

cd Debug
.\tui-windows.exe
```