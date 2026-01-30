#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <thread>
#include <chrono>
#include "Krpcapplication.h"
#include "monitor.pb.h"

// 系统监控类
class SystemMonitor {
public:
    // 获取CPU使用率
    float GetCpuUsage() {
        static unsigned long long prev_total = 0;
        static unsigned long long prev_idle = 0;
        
        std::ifstream stat_file("/proc/stat");
        std::string line;
        std::getline(stat_file, line);
        stat_file.close();
        
        std::istringstream ss(line);
        std::string cpu;
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        ss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        
        unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
        unsigned long long total_idle = idle + iowait;
        
        float usage = 0.0;
        if (prev_total != 0) {
            unsigned long long total_diff = total - prev_total;
            unsigned long long idle_diff = total_idle - prev_idle;
            if (total_diff > 0) {
                usage = 100.0 * (total_diff - idle_diff) / total_diff;
            }
        }
        
        prev_total = total;
        prev_idle = total_idle;
        
        return usage;
    }
    
    // 获取内存使用率
    float GetMemoryUsage() {
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        unsigned long long mem_total = 0, mem_available = 0;
        
        while (std::getline(meminfo, line)) {
            std::istringstream ss(line);
            std::string key;
            unsigned long long value;
            ss >> key >> value;
            
            if (key == "MemTotal:") {
                mem_total = value;
            } else if (key == "MemAvailable:") {
                mem_available = value;
                break;
            }
        }
        meminfo.close();
        
        if (mem_total > 0) {
            return 100.0 * (mem_total - mem_available) / mem_total;
        }
        return 0.0;
    }
    
    // 获取主机名
    std::string GetHostname() {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            return std::string(hostname);
        }
        return "unknown";
    }
    
    // 获取当前时间戳（毫秒）
    int64_t GetTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
};

int main(int argc, char* argv[])
{
    KrpcApplication::Init(argc, argv);
    
    SystemMonitor monitor;
    dmonitor::MonitorReportServiceRpc_Stub stub(new KrpcChannel(false));
    
    std::string hostname = monitor.GetHostname();
    std::cout << "Collector started for server: " << hostname << std::endl;
    
    // 定时上报循环
    while (true) {
        dmonitor::ReportRequest req;
        dmonitor::ReportResponse rsp;
        
        // 采集系统指标
        float cpu_usage = monitor.GetCpuUsage();
        float memory_usage = monitor.GetMemoryUsage();
        int64_t timestamp = monitor.GetTimestamp();
        
        // 填充请求
        req.mutable_metrics()->set_server_name(hostname);
        req.mutable_metrics()->set_timestamp(timestamp);
        req.mutable_metrics()->set_cpu_usage(cpu_usage);
        req.mutable_metrics()->set_memory_usage(memory_usage);
        
        // 发送RPC请求
        stub.Report(nullptr, &req, &rsp, nullptr);
        
        if (rsp.success()) {
            std::cout << "[" << timestamp << "] Reported: CPU=" << cpu_usage 
                      << "%, Memory=" << memory_usage << "%" << std::endl;
        } else {
            std::cerr << "Report failed: " << rsp.result().errmsg() << std::endl;
        }
        
        // 等待3秒
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    
    return 0;
}