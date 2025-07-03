#ifndef ESP32_MUSIC_MCP_CLIENT_H
#define ESP32_MUSIC_MCP_CLIENT_H

#include "boards/common/esp32_music.h"
#include <string>
#include <atomic>

// 简化的ESP32 MCP客户端
// 继承自Esp32Music，但通过MCP服务器请求音乐
class Esp32MusicMcpClient : public Esp32Music {
private:
    std::string mcp_server_url_;
    std::atomic<bool> mcp_available_;
    std::string device_mac_;  // 设备MAC地址
    
public:
    Esp32MusicMcpClient(const std::string& mcp_server_url = "http://192.168.1.100:8080");
    ~Esp32MusicMcpClient() = default;
    
    // 重写父类方法，通过MCP服务器请求
    virtual bool Download(const std::string& song_name) override;
    virtual bool Play() override;
    virtual bool Stop() override;
    
    // MCP相关方法
    void SetMcpServerUrl(const std::string& url) { mcp_server_url_ = url; }
    std::string GetMcpServerUrl() const { return mcp_server_url_; }
    bool IsMcpAvailable() const { return mcp_available_.load(); }
    
    // MAC地址相关方法
    std::string GetDeviceMac() const { return device_mac_; }
    void SetDeviceMac(const std::string& mac) { device_mac_ = mac; }
    
private:
    // 获取设备MAC地址
    std::string GetMacAddress();
    
    // 构建带MAC地址的HTTP请求头
    std::string BuildRequestHeaders();
    
    // 通过MCP服务器获取音乐信息
    bool GetMusicInfoFromMcp(const std::string& song_name, std::string& music_info);
    
    // 通过MCP服务器播放音乐
    bool PlayMusicViaMcp(const std::string& song_name);
    
    // 通过MCP服务器停止音乐
    bool StopMusicViaMcp();
};

#endif // ESP32_MUSIC_MCP_CLIENT_H 