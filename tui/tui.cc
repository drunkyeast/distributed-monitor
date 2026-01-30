#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <memory>
#include <mutex>
#include <algorithm>
#include <cmath>

// 引入你的RPC和Protobuf头文件
#include "Krpcapplication.h"
#include "Krpcchannel.h"
#include "monitor.pb.h"

// FTXUI 头文件
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>

using namespace ftxui;

// --- 数据结构 ---
struct ServerMetrics {
    std::string name;
    float cpu_usage;
    float memory_usage;
    int64_t timestamp;
    bool online;
};

// --- 工具函数 ---

// 获取当前时间 HH:MM:SS
std::string GetCurrentTimeStr() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm* timeinfo = localtime(&time);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    return std::string(buffer);
}

// 格式化时间差 (e.g. 5s ago)
std::string FormatElapsed(int64_t timestamp_ms) {
    if (timestamp_ms == 0) return "N/A";
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t diff = (now - timestamp_ms) / 1000;
    if (diff < 0) diff = 0;
    return std::to_string(diff) + "s ago";
}

// 绘制进度条：[████░░░]
Element DrawCustomProgressBar(float percent) {
    percent = std::max(0.0f, std::min(100.0f, percent));
    int width = 20; // 进度条内部字符宽度
    int filled = static_cast<int>(std::round(width * (percent / 100.0f)));
    
    std::string bar_str;
    for (int i = 0; i < width; ++i) {
        if (i < filled) bar_str += "█";
        else bar_str += "░";
    }

    // 颜色逻辑
    Color c = Color::Green;
    if (percent > 80) c = Color::Red;
    else if (percent > 50) c = Color::Yellow;

    // 组合：白色括号 + 彩色条
    return hbox({
        text("[") | color(Color::White),
        text(bar_str) | color(c),
        text("]") | color(Color::White)
    });
}

// --- 监控系统类 ---

class DistributedMonitor {
public:
    DistributedMonitor() : selected_index_(0), show_details_(false) {
        // 初始化 stub
        stub_ = std::make_unique<dmonitor::MonitorQueryServiceRpc_Stub>(new KrpcChannel(false));
    }

    void Run() {
        auto screen = ScreenInteractive::Fullscreen();

        // 后台刷新线程
        std::thread updater([this, &screen] {
            while (!should_exit_) {
                FetchData();
                screen.PostEvent(Event::Custom); // 触发重绘
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });

        // 渲染逻辑路由
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
    std::unique_ptr<dmonitor::MonitorQueryServiceRpc_Stub> stub_;
    std::vector<ServerMetrics> servers_;
    std::mutex data_mutex_;
    bool should_exit_ = false;
    int selected_index_;
    bool show_details_;

    // 获取数据
    void FetchData() {
        dmonitor::QueryRequest req;
        dmonitor::QueryResponse rsp;
        req.set_server_name(""); // 空字符串表示获取所有
        stub_->Query(nullptr, &req, &rsp, nullptr);

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
            // 防止索引越界
            if (selected_index_ >= servers_.size()) selected_index_ = 0;
        }
    }

    // 主页面渲染
    Element RenderMainPage() {
        std::lock_guard<std::mutex> lock(data_mutex_);

        // 1. 标题行
        auto title_row = hbox({
            text(" DISTRIBUTED SYSTEM MONITOR ") | bold | flex,
            text(" " + GetCurrentTimeStr() + " ") | bold 
        }) | color(Color::BlueLight);

        // 定义列宽 (考虑到括号增加了宽度，适当调宽)
        const int w_arrow = 2;
        const int w_name = 20;
        const int w_status = 15;  // 增加宽度以容纳emoji
        const int w_cpu = 35; 
        const int w_mem = 35; 

        // 2. 表头 (去掉竖线，只保留空格间距)
        auto header_row = hbox({
            text("  ")            | size(WIDTH, EQUAL, w_arrow), // 占位给箭头
            text("SERVER NAME")   | size(WIDTH, EQUAL, w_name) | bold,
            text("     "), 
            text("STATUS")        | size(WIDTH, EQUAL, w_status) | bold,
            text("   "), 
            text("CPU USAGE")     | size(WIDTH, EQUAL, w_cpu) | bold,
            text("   "), 
            text("MEM USAGE")     | size(WIDTH, EQUAL, w_mem) | bold
        });

        // 3. 数据行
        Elements rows;
        if (servers_.empty()) {
            rows.push_back(text("Waiting for data...") | center | flex);
        } else {
            for (size_t i = 0; i < servers_.size(); ++i) {
                const auto& svr = servers_[i];
                bool is_selected = (i == selected_index_);

                // 选中指示符 (使用emoji箭头)
                std::string prefix = is_selected ? "> " : "  "; // ▶ 或 > 或
                
                // 在线状态（使用更大的emoji图标）
                auto status = svr.online 
                    ? text("🟢 ONLINE") | color(Color::Green) 
                    : text("🔴 OFFLINE") | color(Color::Red);

                // 进度条组合（处理负数显示为0）
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

                // 构建单行 (无背景色，无竖线)
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
                // 【关键点】每行数据后增加一个空行，增加垂直间距
                rows.push_back(text("")); 
            }
        }

        // 4. 底部栏 (使用箭头符号)
        auto footer_row = text(" [↑/↓] Select   [Enter] Details   [q/Esc] Quit ") | center | dim;

        // 5. 整体组装
        return vbox({
            title_row,
            separator(), 
            vbox({
                header_row,
                separator(), // 表头下方添加分隔线
                // text(""), // 表头和内容之间空一行
                // 使用 margin(1) 在边框内部制造内边距
                vbox(std::move(rows)) | yframe | borderEmpty
            }) | flex, 
            separator(),
            footer_row
        }) | border;
    }

    // 详情页渲染
    Element RenderDetailsPage() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (servers_.empty() || selected_index_ >= servers_.size()) {
            return text("Error: No server selected");
        }
        
        const auto& svr = servers_[selected_index_];

        // 处理负数显示为0
        int cpu_percent = std::max(0, (int)svr.cpu_usage);
        int mem_percent = std::max(0, (int)svr.memory_usage);

        auto content = vbox({
            text(" Server Details: " + svr.name) | bold,
            separator(),
            hbox({ text("Status: "), svr.online ? text("🟢 ONLINE") | color(Color::Green) : text("🔴 OFFLINE") | color(Color::Red) }),
            text(""),
            text("Last Heartbeat:     " + FormatElapsed(svr.timestamp)),
            text("IP Address:         192.168.1.X (Placeholder)"),
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
    // 初始化 RPC
    KrpcApplication::Init(argc, argv);

    // 运行 TUI
    DistributedMonitor monitor;
    monitor.Run();

    return 0;
}