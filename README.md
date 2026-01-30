# 项目描述: 分布式Linux监控 distributed-monitor
## 架构
三个模块: center, collector, tui
center是中心服务器, 提供rpc远程调用.
collector部署在被监控的服务器上, 调用rpc服务, 传递给center服务器.
tui可以部署在任意地方, 甚至windows terminal, 调用rpc服务器, 从center服务器获取所有linux服务器监控信息, 并用FTXUI终端库展示.

