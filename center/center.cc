#include <iostream>
#include <map>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include "Krpcapplication.h"
#include "Krpcprovider.h"
#include "monitor.pb.h"

// 数据存储管理类
class MetricsStorage {
private:
    // 每个服务器保存最近20条记录
    std::map<std::string, std::deque<dmonitor::MetricsData>> storage_;
    std::mutex mutex_;
    const size_t MAX_HISTORY = 20;
    const int64_t OFFLINE_THRESHOLD_MS = 10000; // 10秒未上报视为离线

public:
    // 添加监控数据
    void AddMetrics(const dmonitor::MetricsData& metrics) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string server_name = metrics.server_name();
        auto& history = storage_[server_name];
        
        // 添加新数据
        history.push_back(metrics);
        
        // 保持最多20条记录
        if (history.size() > MAX_HISTORY) {
            history.pop_front();
        }
        
        std::cout << "[" << metrics.timestamp() << "] Stored metrics from " 
                  << server_name << ": CPU=" << metrics.cpu_usage() 
                  << "%, Memory=" << metrics.memory_usage() << "%" << std::endl;
    }
    
    // 查询监控数据（空字符串表示查询所有服务器）
    std::vector<dmonitor::MetricsData> QueryMetrics(const std::string& server_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<dmonitor::MetricsData> result;
        
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        if (server_name.empty()) {
            // 查询所有服务器的最新数据
            for (const auto& pair : storage_) {
                if (!pair.second.empty()) {
                    dmonitor::MetricsData data = pair.second.back();
                    // 检查是否离线
                    if (now - data.timestamp() > OFFLINE_THRESHOLD_MS) {
                        // 标记为离线（CPU和内存使用率设为-1）
                        data.set_cpu_usage(-1.0);
                        data.set_memory_usage(-1.0);
                    }
                    result.push_back(data);
                }
            }
        } else {
            // 查询指定服务器的所有历史记录
            auto it = storage_.find(server_name);
            if (it != storage_.end()) {
                for (const auto& data : it->second) {
                    result.push_back(data);
                }
            }
        }
        
        return result;
    }
    
    // 获取所有服务器的在线状态
    void PrintStatus() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        std::cout << "\n=== Server Status ===" << std::endl;
        for (const auto& pair : storage_) {
            if (!pair.second.empty()) {
                const auto& latest = pair.second.back();
                int64_t elapsed = now - latest.timestamp();
                std::string status = (elapsed > OFFLINE_THRESHOLD_MS) ? "OFFLINE" : "ONLINE";
                std::cout << pair.first << ": " << status 
                          << " (last seen " << elapsed / 1000 << "s ago)" << std::endl;
            }
        }
        std::cout << "=====================\n" << std::endl;
    }
};

// 全局数据存储
MetricsStorage g_storage;

class MonitorReportService : public dmonitor::MonitorReportServiceRpc 
{
public:
    void Report(::google::protobuf::RpcController* controller,
        const ::dmonitor::ReportRequest* request,
        ::dmonitor::ReportResponse* response,
        ::google::protobuf::Closure* done)
    {
        // 存储监控数据
        g_storage.AddMetrics(request->metrics());
        
        // 构造响应
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        response->set_success(true);

        // 执行回调
        done->Run();
    }
};

class MonitorQueryService : public dmonitor::MonitorQueryServiceRpc
{
public:
    void Query(::google::protobuf::RpcController* controller,
        const ::dmonitor::QueryRequest* request,
        ::dmonitor::QueryResponse* response,
        ::google::protobuf::Closure* done)
    {
        std::string server_name = request->server_name();
        std::cout << "Query request for: " << (server_name.empty() ? "ALL" : server_name) << std::endl;
        
        // 查询数据
        std::vector<dmonitor::MetricsData> metrics = g_storage.QueryMetrics(server_name);
        
        // 填充响应
        for (const auto& data : metrics) {
            dmonitor::MetricsData* metrics_ptr = response->add_metrics();
            metrics_ptr->CopyFrom(data);
        }
        
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        response->set_success(true);
        
        std::cout << "Returned " << metrics.size() << " records" << std::endl;
        
        // 执行回调
        done->Run();
    }
};

// 状态监控线程
void StatusMonitorThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        g_storage.PrintStatus();
    }
}

int main(int argc, char* argv[]) 
{
    std::cout << "Monitor Center Starting..." << std::endl;
    KrpcApplication::Init(argc, argv);
    
    // 启动状态监控线程
    std::thread status_thread(StatusMonitorThread);
    status_thread.detach();
    
    KrpcProvider provider;
    provider.NotifyService(new MonitorReportService());
    provider.NotifyService(new MonitorQueryService());
    
    std::cout << "Center is running..." << std::endl;
    provider.Run();
    
    return 0;
}