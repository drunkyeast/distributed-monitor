#ifndef _Krpcconfig_h
#define _Krpcconfig_h
#include <unordered_map>
#include <string>
// rpcserverip rpcserverport zookeeperip zookeeperport
// 框架读取配置文件
class Krpcconfig{
    public:
    void LoadConfigFile(const char *config_file);// 负责解析和加载配置文件
    std::string Load(const std::string &key);// 查询配置项信息
    private:
    std::unordered_map<std::string, std::string> config_map;
    void Trim(std::string &read_buf);//去掉字符串前后的空格
};
#endif
