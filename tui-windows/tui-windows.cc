#define NOMINMAX  // 必须在最前面

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cmath>

#define WIN32_LEAN_AND_MEAN  // 减少Windows.h包含的内容
#include <winsock2.h>
#include <ws2tcpip.h>

// Protobuf头文件
#include "monitor.pb.h"
#include "Krpcheader.pb.h"

// FTXUI 头文件
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>

using namespace ftxui;

#pragma comment(lib, "ws2_32.lib")

// --- 数据结构 ---
struct ServerMetrics {
    std::string name;
    float cpu_usage;
    float memory_usage;
    int64_t timestamp;
    bool online;
};

// --- RPC客户端类 ---
class RpcClient {
public:
    RpcClient(const std::string& ip, uint16_t port) 
        : server_ip_(ip), server_port_(port), socket_(INVALID_SOCKET) {
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

    bool Connect() {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        // 设置超时
        DWORD timeout = 5000;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port_);
        inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr);

        if (connect(socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }

        return true;
    }

    bool Query(const dmonitor::QueryRequest& request, dmonitor::QueryResponse& response) {
        if (socket_ == INVALID_SOCKET && !Connect()) {
            return false;
        }

        // 构造RpcHeader
        Krpc::RpcHeader rpc_header;
        rpc_header.set_service_name("MonitorQueryServiceRpc");
        rpc_header.set_method_name("Query");
        
        // 序列化
        std::string args_str;
        if (!request.SerializeToString(&args_str)) {
            return false;
        }
        
        rpc_header.set_args_size(args_str.size());
        
        std::string rpc_header_str;
        if (!rpc_header.SerializeToString(&rpc_header_str)) {
            return false;
        }

        // 打包数据
        uint32_t header_size = rpc_header_str.size();
        uint32_t total_len = 4 + header_size + args_str.size();

        uint32_t net_total_len = htonl(total_len);
        uint32_t net_header_len = htonl(header_size);

        std::string send_rpc_str;
        send_rpc_str.reserve(4 + 4 + header_size + args_str.size());
        send_rpc_str.append((char*)&net_total_len, 4);
        send_rpc_str.append((char*)&net_header_len, 4);
        send_rpc_str.append(rpc_header_str);
        send_rpc_str.append(args_str);

        // 发送
        if (send(socket_, send_rpc_str.c_str(), send_rpc_str.size(), 0) == SOCKET_ERROR) {
            return false;
        }

        // 接收响应长度
        uint32_t response_len = 0;
        if (recv_exact(socket_, (char*)&response_len, 4) != 4) {
            return false;
        }
        response_len = ntohl(response_len);

        // 接收响应体
        std::vector<char> recv_buf(response_len);
        if (recv_exact(socket_, recv_buf.data(), response_len) != (int)response_len) {
            return false;
        }

        // 反序列化
        if (!response.ParseFromArray(recv_buf.data(), response_len)) {
            return false;
        }

        return true;
    }

private:
    std::string server_ip_;
    uint16_t server_port_;
    SOCKET socket_;

    int recv_exact(SOCKET fd, char* buf, size_t size) {
        size_t received = 0;
        while (received < size) {
            int n = recv(fd, buf + received, size - received, 0);
            if (n == SOCKET_ERROR || n == 0) {
                return -1;
            }
            received += n;
        }
        return received;
    }
};

// --- 工具函数 ---
std::string GetCurrentTimeStr() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return std::string(buffer);
}

std::string FormatElapsed(int64_t timestamp_ms) {
    if (timestamp_ms == 0) return "N/A";
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t diff = (now - timestamp_ms) / 1000;
    if (diff < 0) diff = 0;
    return std::to_string(diff) + "s ago";
}

Element DrawCustomProgressBar(float percent) {
    percent = std::max(0.0f, std::min(100.0f, percent));
    int width = 20;
    int filled = static_cast<int>(std::round(width * (percent / 100.0f)));
    
    std::string bar_str;
    for (int i = 0; i < width; ++i) {
        if (i < filled) bar_str += "█";
        else bar_str += "░";
    }

    Color c = Color::Green;
    if (percent > 80) c = Color::Red;
    else if (percent > 50) c = Color::Yellow;

    return hbox({
        text("[") | color(Color::White),
        text(bar_str) | color(c),
        text("]") | color(Color::White)
    });
}

// --- 监控系统类 ---
class DistributedMonitor {
public:
    DistributedMonitor(const std::string& ip, uint16_t port) 
        : selected_index_(0), show_details_(false), client_(ip, port) {
    }

    void Run() {
        auto screen = ScreenInteractive::Fullscreen();

        // 后台刷新线程
        std::thread updater([this, &screen] {
            while (!should_exit_) {
                FetchData();
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });

        // 渲染逻辑
        auto component = Renderer([this] {
            if (show_details_) return RenderDetailsPage();
            return RenderMainPage();
        });

        // 事件处理
        component = CatchEvent(component, [this, &screen](Event event) {
            if (show_details_) {
                if (event == Event::Escape || event == Event::Character('q') || event == Event::Backspace) {
                    show_details_ = false;
                    return true;
                }
            } else {
                if (event == Event::ArrowDown || event == Event::Character('j')) {
                    if (!servers_.empty()) selected_index_ = (selected_index_ + 1) % servers_.size();
                    return true;
                }
                if (event == Event::ArrowUp || event == Event::Character('k')) {
                    if (!servers_.empty()) selected_index_ = (selected_index_ + servers_.size() - 1) % servers_.size();
                    return true;
                }
                if (event == Event::Return) {
                    if (!servers_.empty()) show_details_ = true;
                    return true;
                }
                if (event == Event::Escape || event == Event::Character('q')) {
                    screen.ExitLoopClosure()();
                    should_exit_ = true;
                    return true;
                }
            }
            return false;
        });

        screen.Loop(component);
        should_exit_ = true;
        if (updater.joinable()) updater.join();
    }

private:
    RpcClient client_;
    std::vector<ServerMetrics> servers_;
    std::mutex data_mutex_;
    bool should_exit_ = false;
    int selected_index_;
    bool show_details_;

    void FetchData() {
        dmonitor::QueryRequest req;
        dmonitor::QueryResponse rsp;
        req.set_server_name("");

        if (!client_.Query(req, rsp)) {
            return;
        }

        std::lock_guard<std::mutex> lock(data_mutex_);
        if (rsp.metrics_size() > 0) {
            servers_.clear();
            for (int i = 0; i < rsp.metrics_size(); i++) {
                const auto& m = rsp.metrics(i);
                servers_.push_back({
                    m.server_name(),
                    m.cpu_usage(),
                    m.memory_usage(),
                    m.timestamp(),
                    m.cpu_usage() >= 0
                });
            }
            if (selected_index_ >= servers_.size()) selected_index_ = 0;
        }
    }

    Element RenderMainPage() {
        std::lock_guard<std::mutex> lock(data_mutex_);

        auto title_row = hbox({
            text(" DISTRIBUTED SYSTEM MONITOR ") | bold | flex,
            text(" " + GetCurrentTimeStr() + " ") | bold 
        }) | color(Color::BlueLight);

        const int w_arrow = 2;
        const int w_name = 20;
        const int w_status = 15;
        const int w_cpu = 35;
        const int w_mem = 35;

        auto header_row = hbox({
            text("  ")            | size(WIDTH, EQUAL, w_arrow),
            text("SERVER NAME")   | size(WIDTH, EQUAL, w_name) | bold,
            text("     "), 
            text("STATUS")        | size(WIDTH, EQUAL, w_status) | bold,
            text("   "), 
            text("CPU USAGE")     | size(WIDTH, EQUAL, w_cpu) | bold,
            text("   "), 
            text("MEM USAGE")     | size(WIDTH, EQUAL, w_mem) | bold
        });

        Elements rows;
        if (servers_.empty()) {
            rows.push_back(text("Waiting for data...") | center | flex);
        } else {
            for (size_t i = 0; i < servers_.size(); ++i) {
                const auto& svr = servers_[i];
                bool is_selected = (i == selected_index_);

                std::string prefix = is_selected ? "> " : "  ";
                
                auto status = svr.online 
                    ? text("ONLINE") | color(Color::Green) 
                    : text("OFFLINE") | color(Color::Red);

                int cpu_percent = std::max(0, (int)svr.cpu_usage);
                int mem_percent = std::max(0, (int)svr.memory_usage);
                
                auto cpu_bar = hbox({
                    DrawCustomProgressBar(cpu_percent),
                    text(" " + std::to_string(cpu_percent) + "%")
                });
                
                auto mem_bar = hbox({
                    DrawCustomProgressBar(mem_percent),
                    text(" " + std::to_string(mem_percent) + "%")
                });

                auto row = hbox({
                    text(prefix)   | size(WIDTH, EQUAL, w_arrow) | color(Color::BlueLight) | bold,
                    text(svr.name) | size(WIDTH, EQUAL, w_name) | (is_selected ? (color(Color::Blue) | bold) : color(Color::White)),
                    text("   "),
                    status         | size(WIDTH, EQUAL, w_status),
                    text("   "),
                    cpu_bar        | size(WIDTH, EQUAL, w_cpu),
                    text("   "),
                    mem_bar        | size(WIDTH, EQUAL, w_mem)
                });

                rows.push_back(row);
                rows.push_back(text(""));
            }
        }

        auto footer_row = text(" [Up/Down] Select   [Enter] Details   [q/Esc] Quit ") | center | dim;

        return vbox({
            title_row,
            separator(), 
            vbox({
                header_row,
                separator(),
                vbox(std::move(rows)) | yframe | borderEmpty
            }) | flex, 
            separator(),
            footer_row
        }) | border;
    }

    Element RenderDetailsPage() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (servers_.empty() || selected_index_ >= servers_.size()) {
            return text("Error: No server selected");
        }
        
        const auto& svr = servers_[selected_index_];

        int cpu_percent = std::max(0, (int)svr.cpu_usage);
        int mem_percent = std::max(0, (int)svr.memory_usage);

        auto content = vbox({
            text(" Server Details: " + svr.name) | bold,
            separator(),
            hbox({ text("Status: "), svr.online ? text("ONLINE") | color(Color::Green) : text("OFFLINE") | color(Color::Red) }),
            text(""),
            text("Last Heartbeat:     " + FormatElapsed(svr.timestamp)),
            text(""),
            text("Realtime Metrics:") | bold,
            hbox({ text("CPU: "), DrawCustomProgressBar(cpu_percent) | flex, text(" " + std::to_string(cpu_percent) + "%") }),
            hbox({ text("MEM: "), DrawCustomProgressBar(mem_percent) | flex, text(" " + std::to_string(mem_percent) + "%") }),
        }) | border | size(WIDTH, GREATER_THAN, 60) | center;

        return vbox({
            filler(),
            content,
            filler(),
            text("Press [Esc] to return") | center
        });
    }
};

int main(int argc, char* argv[]) {
    std::string server_ip = "47.105.102.62";
    uint16_t server_port = 8000;

    if (argc >= 2) {
        server_ip = argv[1];
    }
    if (argc >= 3) {
        server_port = std::atoi(argv[2]);
    }

    DistributedMonitor monitor(server_ip, server_port);
    monitor.Run();

    return 0;
}