#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

// Protobuf头文件
#include "monitor.pb.h"
#include "Krpcheader.pb.h"

// 链接Winsock库
#pragma comment(lib, "ws2_32.lib")

// RPC客户端类
class RpcClient {
public:
    RpcClient(const std::string& ip, uint16_t port) 
        : server_ip_(ip), server_port_(port), socket_(INVALID_SOCKET) {
        // 初始化Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed!" << std::endl;
            exit(1);
        }
    }

    ~RpcClient() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
        }
        WSACleanup();
    }

    // 连接到服务器
    bool Connect() {
        // 创建socket
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            std::cerr << "Create socket failed: " << WSAGetLastError() << std::endl;
            return false;
        }

        // 设置服务器地址
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port_);
        inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr);

        // 连接
        if (connect(socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            std::cerr << "Connect failed: " << WSAGetLastError() << std::endl;
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }

        std::cout << "Connected to " << server_ip_ << ":" << server_port_ << std::endl;
        return true;
    }

    // 发送Query请求
    bool Query(const dmonitor::QueryRequest& request, dmonitor::QueryResponse& response) {
        if (socket_ == INVALID_SOCKET) {
            std::cerr << "Not connected to server!" << std::endl;
            return false;
        }

        // 1. 构造RpcHeader
        Krpc::RpcHeader rpc_header;
        rpc_header.set_service_name("MonitorQueryServiceRpc");
        rpc_header.set_method_name("Query");
        
        // 2. 序列化请求和头部
        std::string args_str;
        if (!request.SerializeToString(&args_str)) {
            std::cerr << "Serialize request failed!" << std::endl;
            return false;
        }
        
        rpc_header.set_args_size(args_str.size());
        
        std::string rpc_header_str;
        if (!rpc_header.SerializeToString(&rpc_header_str)) {
            std::cerr << "Serialize header failed!" << std::endl;
            return false;
        }

        // 3. 打包数据
        // 格式：[4B Total Len] + [4B Header Len] + [Header] + [Args]
        uint32_t header_size = rpc_header_str.size();
        uint32_t total_len = 4 + header_size + args_str.size();

        // 转网络字节序
        uint32_t net_total_len = htonl(total_len);
        uint32_t net_header_len = htonl(header_size);

        std::string send_rpc_str;
        send_rpc_str.reserve(4 + 4 + header_size + args_str.size());
        send_rpc_str.append((char*)&net_total_len, 4);
        send_rpc_str.append((char*)&net_header_len, 4);
        send_rpc_str.append(rpc_header_str);
        send_rpc_str.append(args_str);

        // 4. 发送数据
        if (send(socket_, send_rpc_str.c_str(), send_rpc_str.size(), 0) == SOCKET_ERROR) {
            std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
            return false;
        }

        std::cout << "Sent request: total_len=" << total_len 
                  << ", header_len=" << header_size 
                  << ", args_len=" << args_str.size() << std::endl;

        // 5. 接收响应
        // 格式：[4B Total Len] + [Response Data]
        
        // A. 先读4字节长度头
        uint32_t response_len = 0;
        if (recv_exact(socket_, (char*)&response_len, 4) != 4) {
            std::cerr << "Recv response length failed!" << std::endl;
            return false;
        }
        response_len = ntohl(response_len);

        std::cout << "Received response length: " << response_len << std::endl;

        // B. 根据长度读取Body
        std::vector<char> recv_buf(response_len);
        if (recv_exact(socket_, recv_buf.data(), response_len) != (int)response_len) {
            std::cerr << "Recv response body failed!" << std::endl;
            return false;
        }

        // 6. 反序列化响应
        if (!response.ParseFromArray(recv_buf.data(), response_len)) {
            std::cerr << "Parse response failed!" << std::endl;
            return false;
        }

        std::cout << "Query success!" << std::endl;
        return true;
    }

private:
    std::string server_ip_;
    uint16_t server_port_;
    SOCKET socket_;

    // 确保接收指定长度的数据（处理TCP粘包/半包）
    int recv_exact(SOCKET fd, char* buf, size_t size) {
        size_t received = 0;
        while (received < size) {
            int n = recv(fd, buf + received, size - received, 0);
            if (n == SOCKET_ERROR) {
                std::cerr << "Recv error: " << WSAGetLastError() << std::endl;
                return -1;
            }
            if (n == 0) {
                std::cerr << "Connection closed by peer" << std::endl;
                return 0;
            }
            received += n;
        }
        return received;
    }

};

int main(int argc, char* argv[]) {
    // 配置服务器地址（写死）
    std::string server_ip = "47.105.102.62";
    uint16_t server_port = 8000;

    // 如果有命令行参数，可以覆盖
    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        server_port = std::atoi(argv[2]);
    }

    std::cout << "=== Distributed Monitor Windows Client ===" << std::endl;
    std::cout << "Target server: " << server_ip << ":" << server_port << std::endl;

    // 创建RPC客户端
    RpcClient client(server_ip, server_port);

    // 连接到服务器
    if (!client.Connect()) {
        std::cerr << "Failed to connect to server!" << std::endl;
        return 1;
    }

    // 发送Query请求（查询所有服务器）
    dmonitor::QueryRequest request;
    request.set_server_name(""); // 空字符串表示查询所有

    dmonitor::QueryResponse response;
    if (!client.Query(request, response)) {
        std::cerr << "Query failed!" << std::endl;
        return 1;
    }

    // 打印响应结果
    std::cout << "\n=== Query Results ===" << std::endl;
    std::cout << "Success: " << (response.success() ? "YES" : "NO") << std::endl;
    std::cout << "Error Code: " << response.result().errcode() << std::endl;
    std::cout << "Metrics Count: " << response.metrics_size() << std::endl;

    // 打印每个服务器的指标
    for (int i = 0; i < response.metrics_size(); i++) {
        const auto& metrics = response.metrics(i);
        std::cout << "\n[" << i + 1 << "] Server: " << metrics.server_name() << std::endl;
        std::cout << "    Timestamp: " << metrics.timestamp() << std::endl;
        std::cout << "    CPU Usage: " << metrics.cpu_usage() << "%" << std::endl;
        std::cout << "    Memory Usage: " << metrics.memory_usage() << "%" << std::endl;
        
        // 判断在线状态（负数表示离线）
        if (metrics.cpu_usage() < 0) {
            std::cout << "    Status: OFFLINE" << std::endl;
        } else {
            std::cout << "    Status: ONLINE" << std::endl;
        }
    }

    std::cout << "\n=== Done ===" << std::endl;

    return 0;
}
