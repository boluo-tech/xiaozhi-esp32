#ifndef MUSIC_SERVER_CONFIG_H
#define MUSIC_SERVER_CONFIG_H

#include <string>

class MusicServerConfig {
public:
    static MusicServerConfig& GetInstance();
    
    // 获取配置值
    bool IsEnabled() const;
    std::string GetServerUrl() const;
    std::string GetApiPath() const;
    std::string GetUserAgent() const;
    int GetTimeoutMs() const;
    int GetRetryCount() const;
    int GetRetryDelayMs() const;
    bool IsRangeRequestsEnabled() const;
    int GetChunkSize() const;
    int GetMaxBufferSize() const;
    int GetMinBufferSize() const;
    
    // 构建完整的API URL
    std::string BuildSearchUrl(const std::string& song_name) const;
    std::string BuildAudioUrl(const std::string& audio_path) const;
    std::string BuildLyricUrl(const std::string& lyric_path) const;
    
    // 验证配置
    bool IsValid() const;
    
private:
    MusicServerConfig() = default;
    ~MusicServerConfig() = default;
    MusicServerConfig(const MusicServerConfig&) = delete;
    MusicServerConfig& operator=(const MusicServerConfig&) = delete;
};

#endif // MUSIC_SERVER_CONFIG_H 