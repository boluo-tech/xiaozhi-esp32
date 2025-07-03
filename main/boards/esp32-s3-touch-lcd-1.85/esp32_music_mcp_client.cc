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
    
    // 暂时不测试MCP服务器连接，避免阻塞启动
    // 在实际使用时再测试连接
    ESP_LOGW(TAG, "MCP服务器连接测试已跳过，将在首次使用时测试");
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
    
    // 如果还没有测试过MCP连接，先测试一次
    if (!mcp_available_) {
        ESP_LOGI(TAG, "首次使用MCP，测试服务器连接...");
        std::string test_response;
        if (GetMusicInfoFromMcp("test", test_response)) {
            mcp_available_ = true;
            ESP_LOGI(TAG, "MCP服务器连接成功");
        } else {
            ESP_LOGW(TAG, "MCP服务器连接失败，使用父类方法");
            return Esp32Music::Download(song_name);
        }
    }
    
    std::string music_info;
    if (GetMusicInfoFromMcp(song_name, music_info)) {
        ESP_LOGI(TAG, "从MCP服务器获取音乐信息成功");
        
        // 解析MCP服务器返回的JSON响应
        cJSON* response_json = cJSON_Parse(music_info.c_str());
        if (response_json) {
            // 提取音频URL
            cJSON* audio_url = cJSON_GetObjectItem(response_json, "audio_url");
            cJSON* title = cJSON_GetObjectItem(response_json, "title");
            cJSON* artist = cJSON_GetObjectItem(response_json, "artist");
            
            if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0) {
                ESP_LOGI(TAG, "MCP音频URL: %s", audio_url->valuestring);
                
                // 构建完整的音频URL
                std::string full_audio_url = mcp_server_url_ + audio_url->valuestring;
                ESP_LOGI(TAG, "完整音频URL: %s", full_audio_url.c_str());
                
                // 保存音乐信息到父类
                // 这里我们需要访问父类的私有成员，所以我们需要一个公共接口
                // 暂时直接调用父类的StartStreaming方法
                
                // 保存歌名用于显示
                if (cJSON_IsString(title)) {
                    ESP_LOGI(TAG, "歌曲标题: %s", title->valuestring);
                }
                if (cJSON_IsString(artist)) {
                    ESP_LOGI(TAG, "艺术家: %s", artist->valuestring);
                }
                
                // 启动流式播放
                bool result = StartStreaming(full_audio_url);
                cJSON_Delete(response_json);
                return result;
            } else {
                ESP_LOGE(TAG, "MCP响应中没有有效的音频URL");
                cJSON_Delete(response_json);
                return false;
            }
        } else {
            ESP_LOGE(TAG, "解析MCP响应JSON失败");
            return false;
        }
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

// 简单的URL编码函数（复制自esp32_music.cc）
static std::string url_encode(const std::string& str) {
    std::string encoded;
    char hex[4];
    
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';  // 空格编码为'+'或'%20'
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

bool Esp32MusicMcpClient::GetMusicInfoFromMcp(const std::string& song_name, std::string& music_info) {
    ESP_LOGI(TAG, "从MCP服务器获取音乐信息: %s", song_name.c_str());
    
    // 对歌曲名进行URL编码
    std::string encoded_song_name = url_encode(song_name);
    ESP_LOGI(TAG, "URL编码后的歌曲名: %s", encoded_song_name.c_str());
    
    // 构建MCP服务器请求URL
    std::string url = mcp_server_url_ + "/music/search?song=" + encoded_song_name;
    
    // 使用Board的HTTP客户端
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "创建HTTP客户端失败");
        return false;
    }
    
    // 设置超时时间（5秒）
    http->SetTimeout(5000);
    
    // 设置包含MAC地址的请求头
    std::string headers = BuildRequestHeaders();
    http->SetHeader("User-Agent", "ESP32-MCP-Client/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Device-Id", device_mac_);
    http->SetHeader("X-Device-Type", "ESP32");
    
    // 发送GET请求
    ESP_LOGI(TAG, "正在连接MCP服务器: %s", url.c_str());
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "连接MCP服务器失败，可能是服务器地址错误或网络问题");
        http->Close();
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
    
    // 对歌曲名进行URL编码
    std::string encoded_song_name = url_encode(song_name);
    ESP_LOGI(TAG, "URL编码后的歌曲名: %s", encoded_song_name.c_str());
    
    // 构建MCP服务器请求URL
    std::string url = mcp_server_url_ + "/music/play?song=" + encoded_song_name;
    
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