#ifndef _Krpcprovider_H__
#define _Krpcprovider_H__
#include "google/protobuf/service.h"
#include "zookeeperutil.h"
// #include<muduo/net/TcpServer.h>
// #include<muduo/net/EventLoop.h>
// #include<muduo/net/InetAddress.h>
// #include<muduo/net/TcpConnection.h>
#include "TcpServer.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "TcpConnection.h"

#include<google/protobuf/descriptor.h>
#include<functional>
#include<string>
#include<unordered_map>

class KrpcProvider
{
public:
    //这里是提供给外部使用的，可以发布rpc方法的函数接口。
    void NotifyService(google::protobuf::Service* service);
      ~KrpcProvider();
    //启动rpc服务节点，开始提供rpc远程网络调用服务
    void Run();
private:
    EventLoop event_loop; // muduo::net::TcpServer写到run里面去了, 但event_loop不行, 结束的时候要处理
    struct ServiceInfo
    {
        google::protobuf::Service* service;
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> method_map;
    };
    std::unordered_map<std::string, ServiceInfo>service_map;//保存服务对象和rpc方法
    
    void OnConnection(const TcpConnectionPtr& conn);
    void OnMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp receive_time);
    void SendRpcResponse(const TcpConnectionPtr& conn, google::protobuf::Message* response);
};
#endif 







