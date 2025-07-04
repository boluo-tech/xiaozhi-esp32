#include "music_server_config.h"

#include <esp_log.h>
#include <sdkconfig.h>

#define TAG "MusicServerConfig"

// URL编码函数
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
            encoded += '+';  // 空格编码为'+'
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

MusicServerConfig& MusicServerConfig::GetInstance() {
    static MusicServerConfig instance;
    return instance;
}

bool MusicServerConfig::IsEnabled() const {
#ifdef CONFIG_MUSIC_SERVER_ENABLED
    return true;
#else
    return false;
#endif
}

std::string MusicServerConfig::GetServerUrl() const {
#ifdef CONFIG_MUSIC_SERVER_URL
    return CONFIG_MUSIC_SERVER_URL;
#else
    return "http://www.jsrc.top:5566";
#endif
}

std::string MusicServerConfig::GetApiPath() const {
    return "/stream_pcm";
}

std::string MusicServerConfig::GetUserAgent() const {
#ifdef CONFIG_MUSIC_SERVER_USER_AGENT
    return CONFIG_MUSIC_SERVER_USER_AGENT;
#else
    return "ESP32-Music-Player/1.0";
#endif
}

int MusicServerConfig::GetTimeoutMs() const {
#ifdef CONFIG_MUSIC_SERVER_TIMEOUT_MS
    return CONFIG_MUSIC_SERVER_TIMEOUT_MS;
#else
    return 10000;
#endif
}

int MusicServerConfig::GetRetryCount() const {
#ifdef CONFIG_MUSIC_SERVER_RETRY_COUNT
    return CONFIG_MUSIC_SERVER_RETRY_COUNT;
#else
    return 3;
#endif
}

int MusicServerConfig::GetRetryDelayMs() const {
#ifdef CONFIG_MUSIC_SERVER_RETRY_DELAY_MS
    return CONFIG_MUSIC_SERVER_RETRY_DELAY_MS;
#else
    return 500;
#endif
}

bool MusicServerConfig::IsRangeRequestsEnabled() const {
#ifdef CONFIG_MUSIC_SERVER_ENABLE_RANGE_REQUESTS
    return true;
#else
    return false;
#endif
}

int MusicServerConfig::GetChunkSize() const {
#ifdef CONFIG_MUSIC_SERVER_CHUNK_SIZE
    return CONFIG_MUSIC_SERVER_CHUNK_SIZE;
#else
    return 4096;
#endif
}

int MusicServerConfig::GetMaxBufferSize() const {
#ifdef CONFIG_MUSIC_SERVER_MAX_BUFFER_SIZE
    return CONFIG_MUSIC_SERVER_MAX_BUFFER_SIZE * 1024;  // 转换为字节
#else
    return 512 * 1024;
#endif
}

int MusicServerConfig::GetMinBufferSize() const {
#ifdef CONFIG_MUSIC_SERVER_MIN_BUFFER_SIZE
    return CONFIG_MUSIC_SERVER_MIN_BUFFER_SIZE * 1024;  // 转换为字节
#else
    return 64 * 1024;
#endif
}

std::string MusicServerConfig::BuildSearchUrl(const std::string& song_name) const {
    if (!IsEnabled()) {
        ESP_LOGW(TAG, "Music server is disabled");
        return "";
    }
    
    std::string url = GetServerUrl() + GetApiPath() + "?song=" + url_encode(song_name);
    ESP_LOGD(TAG, "Built search URL: %s", url.c_str());
    return url;
}

std::string MusicServerConfig::BuildAudioUrl(const std::string& audio_path) const {
    if (!IsEnabled()) {
        ESP_LOGW(TAG, "Music server is disabled");
        return "";
    }
    
    std::string url = GetServerUrl() + audio_path;
    ESP_LOGD(TAG, "Built audio URL: %s", url.c_str());
    return url;
}

std::string MusicServerConfig::BuildLyricUrl(const std::string& lyric_path) const {
    if (!IsEnabled()) {
        ESP_LOGW(TAG, "Music server is disabled");
        return "";
    }
    
    std::string url = GetServerUrl() + lyric_path;
    ESP_LOGD(TAG, "Built lyric URL: %s", url.c_str());
    return url;
}

bool MusicServerConfig::IsValid() const {
    if (!IsEnabled()) {
        ESP_LOGW(TAG, "Music server is disabled");
        return false;
    }
    
    std::string server_url = GetServerUrl();
    if (server_url.empty() || server_url.find("http") != 0) {
        ESP_LOGE(TAG, "Invalid server URL: %s", server_url.c_str());
        return false;
    }
    
    std::string api_path = GetApiPath();
    if (api_path.empty() || api_path[0] != '/') {
        ESP_LOGE(TAG, "Invalid API path: %s", api_path.c_str());
        return false;
    }
    
    int timeout = GetTimeoutMs();
    if (timeout < 1000 || timeout > 60000) {
        ESP_LOGE(TAG, "Invalid timeout: %d ms", timeout);
        return false;
    }
    
    int retry_count = GetRetryCount();
    if (retry_count < 0 || retry_count > 10) {
        ESP_LOGE(TAG, "Invalid retry count: %d", retry_count);
        return false;
    }
    
    int chunk_size = GetChunkSize();
    if (chunk_size < 1024 || chunk_size > 16384) {
        ESP_LOGE(TAG, "Invalid chunk size: %d bytes", chunk_size);
        return false;
    }
    
    int max_buffer = GetMaxBufferSize();
    int min_buffer = GetMinBufferSize();
    if (max_buffer < min_buffer) {
        ESP_LOGE(TAG, "Max buffer size (%d) cannot be less than min buffer size (%d)", 
                max_buffer, min_buffer);
        return false;
    }
    
    ESP_LOGI(TAG, "Music server configuration is valid");
    ESP_LOGI(TAG, "Server URL: %s", server_url.c_str());
    ESP_LOGI(TAG, "API Path: %s", api_path.c_str());
    ESP_LOGI(TAG, "User Agent: %s", GetUserAgent().c_str());
    ESP_LOGI(TAG, "Timeout: %d ms", timeout);
    ESP_LOGI(TAG, "Retry Count: %d", retry_count);
    ESP_LOGI(TAG, "Range Requests: %s", IsRangeRequestsEnabled() ? "enabled" : "disabled");
    ESP_LOGI(TAG, "Chunk Size: %d bytes", chunk_size);
    ESP_LOGI(TAG, "Buffer Size: %d-%d bytes", min_buffer, max_buffer);
    
    return true;
} 