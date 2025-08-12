#ifndef MUSIC_H
#define MUSIC_H

#include <string>

class Music {
public:
    virtual ~Music() = default;  // 添加虚析构函数
    
    virtual bool Download(const std::string& song_name, bool auto_start = true) = 0;
    virtual bool Play() = 0;
    virtual bool Stop(bool stop_playlist = true) = 0;
    virtual std::string GetDownloadResult() = 0;
    
    // 新增流式播放相关方法
    virtual bool StartStreaming(const std::string& music_url, bool stop_playlist = true) = 0;
    virtual bool StopStreaming() = 0;  // 停止流式播放
    virtual size_t GetBufferSize() const = 0;
    virtual bool IsDownloading() const = 0;

    virtual bool PlayPlaylist(const std::string& query) = 0;
    virtual bool FetchPlaylist(const std::string& query, std::vector<std::string>& out_playlist) = 0;
    
    // 新增：紧急停止方法，用于唤醒打断
    virtual void EmergencyStop() = 0;
};

#endif // MUSIC_H 