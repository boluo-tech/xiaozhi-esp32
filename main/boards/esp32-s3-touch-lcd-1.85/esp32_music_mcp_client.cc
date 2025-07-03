#include "esp32_music_mcp_client.h"
#include "board.h"
#include "system_info.h"
#include <esp_log.h>
#include <cJSON.h>

#define TAG "Esp32MusicMcp"

Esp32MusicMcpClient::Esp32MusicMcpClient(const std::string& mcp_server_url)
    : mcp_server_url_(mcp_server_url), mcp_available_(false) {
    ESP_LOGI(TAG, "MCP客户端初始化，服务器地址: %s", mcp_server_url_.c_str());
    
    // 获取设备MAC地址
    device_mac_ = GetMacAddress();
    ESP_LOGI(TAG, "设备MAC地址: %s", device_mac_.c_str());
    
    // 测试MCP服务器连接
    std::string status_response;
    if (GetMusicInfoFromMcp("test", status_response)) {
        mcp_available_ = true;
        ESP_LOGI(TAG, "MCP服务器连接成功");
    } else {
        ESP_LOGW(TAG, "MCP服务器连接失败，将使用本地播放模式");
    }
}

std::string Esp32MusicMcpClient::GetMacAddress() {
    // 使用SystemInfo::GetMacAddress()获取设备ID
    return SystemInfo::GetMacAddress();
}

std::string Esp32MusicMcpClient::BuildRequestHeaders() {
    std::string headers;
    headers += "User-Agent: ESP32-MCP-Client/1.0\r\n";
    headers += "Accept: application/json\r\n";
    headers += "Device-Id: " + device_mac_ + "\r\n";
    headers += "X-Device-Type: ESP32\r\n";
    return headers;
}

bool Esp32MusicMcpClient::Download(const std::string& song_name) {
    ESP_LOGI(TAG, "通过MCP服务器下载音乐: %s", song_name.c_str());
    
    if (!mcp_available_) {
        ESP_LOGW(TAG, "MCP服务器不可用，使用父类方法");
        return Esp32Music::Download(song_name);
    }
    
    std::string music_info;
    if (GetMusicInfoFromMcp(song_name, music_info)) {
        // 保存音乐信息到父类的last_downloaded_data_
        // 这里需要访问父类的私有成员，可能需要修改父类或使用其他方式
        ESP_LOGI(TAG, "从MCP服务器获取音乐信息成功");
        return true;
    } else {
        ESP_LOGE(TAG, "从MCP服务器获取音乐信息失败");
        return false;
    }
}

bool Esp32MusicMcpClient::Play() {
    ESP_LOGI(TAG, "通过MCP服务器播放音乐");
    
    if (!mcp_available_) {
        ESP_LOGW(TAG, "MCP服务器不可用，使用父类方法");
        return Esp32Music::Play();
    }
    
    // 这里需要获取当前歌曲名称，可能需要从父类获取
    std::string current_song = "current_song"; // 需要从父类获取
    return PlayMusicViaMcp(current_song);
}

bool Esp32MusicMcpClient::Stop() {
    ESP_LOGI(TAG, "通过MCP服务器停止音乐");
    
    if (!mcp_available_) {
        ESP_LOGW(TAG, "MCP服务器不可用，使用父类方法");
        return Esp32Music::Stop();
    }
    
    return StopMusicViaMcp();
}

bool Esp32MusicMcpClient::GetMusicInfoFromMcp(const std::string& song_name, std::string& music_info) {
    ESP_LOGI(TAG, "从MCP服务器获取音乐信息: %s", song_name.c_str());
    
    // 构建MCP服务器请求URL
    std::string url = mcp_server_url_ + "/music/search?song=" + song_name;
    
    // 使用Board的HTTP客户端
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "创建HTTP客户端失败");
        return false;
    }
    
    // 设置包含MAC地址的请求头
    std::string headers = BuildRequestHeaders();
    http->SetHeader("User-Agent", "ESP32-MCP-Client/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Device-Id", device_mac_);
    http->SetHeader("X-Device-Type", "ESP32");
    
    // 发送GET请求
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "连接MCP服务器失败");
        return false;
    }
    
    // 检查响应状态码
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "MCP服务器响应错误: %d", status_code);
        http->Close();
        return false;
    }
    
    // 读取响应数据
    music_info = http->ReadAll();
    http->Close();
    
    ESP_LOGI(TAG, "MCP服务器响应: %s", music_info.c_str());
    return true;
}

bool Esp32MusicMcpClient::PlayMusicViaMcp(const std::string& song_name) {
    ESP_LOGI(TAG, "通过MCP服务器播放音乐: %s", song_name.c_str());
    
    // 构建MCP服务器请求URL
    std::string url = mcp_server_url_ + "/music/play?song=" + song_name;
    
    // 使用Board的HTTP客户端
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "创建HTTP客户端失败");
        return false;
    }
    
    // 设置包含MAC地址的请求头
    http->SetHeader("User-Agent", "ESP32-MCP-Client/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Device-Id", device_mac_);
    http->SetHeader("X-Device-Type", "ESP32");
    
    // 发送GET请求
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "连接MCP服务器失败");
        return false;
    }
    
    // 检查响应状态码
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "MCP服务器响应错误: %d", status_code);
        http->Close();
        return false;
    }
    
    // 读取响应数据
    std::string response = http->ReadAll();
    http->Close();
    
    ESP_LOGI(TAG, "MCP播放响应: %s", response.c_str());
    return true;
}

bool Esp32MusicMcpClient::StopMusicViaMcp() {
    ESP_LOGI(TAG, "通过MCP服务器停止音乐");
    
    // 构建MCP服务器请求URL
    std::string url = mcp_server_url_ + "/music/stop";
    
    // 使用Board的HTTP客户端
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "创建HTTP客户端失败");
        return false;
    }
    
    // 设置包含MAC地址的请求头
    http->SetHeader("User-Agent", "ESP32-MCP-Client/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Device-Id", device_mac_);
    http->SetHeader("X-Device-Type", "ESP32");
    
    // 发送GET请求
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "连接MCP服务器失败");
        return false;
    }
    
    // 检查响应状态码
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "MCP服务器响应错误: %d", status_code);
        http->Close();
        return false;
    }
    
    // 读取响应数据
    std::string response = http->ReadAll();
    http->Close();
    
    ESP_LOGI(TAG, "MCP停止响应: %s", response.c_str());
    return true;
} 