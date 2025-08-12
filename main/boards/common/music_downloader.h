#ifndef MUSIC_DOWNLOADER_H
#define MUSIC_DOWNLOADER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <atomic>
#include <functional>
#include <memory>

// 下载状态
enum class DownloadState {
    IDLE,           // 空闲
    CONNECTING,     // 连接中
    DOWNLOADING,    // 下载中
    PAUSED,         // 暂停
    COMPLETED,      // 完成
    ERROR           // 错误
};

// 下载优先级
enum class DownloadPriority {
    LOW,            // 低优先级（预加载）
    NORMAL,         // 普通优先级
    HIGH,           // 高优先级（当前播放）
    URGENT          // 紧急优先级
};

// 下载任务
struct DownloadTask {
    std::string id;
    std::string url;
    std::string file_path;
    size_t total_size;
    size_t downloaded_size;
    size_t resume_position;
    DownloadState state;
    DownloadPriority priority;
    uint32_t retry_count;
    uint32_t max_retries;
    uint32_t timeout_ms;
    std::string error_message;
    
    DownloadTask() : total_size(0), downloaded_size(0), resume_position(0),
                    state(DownloadState::IDLE), priority(DownloadPriority::NORMAL),
                    retry_count(0), max_retries(3), timeout_ms(30000) {}
};

// 下载回调
struct DownloadCallbacks {
    std::function<void(const std::string&, size_t, size_t)> on_progress;
    std::function<void(const std::string&)> on_completed;
    std::function<void(const std::string&, const std::string&)> on_error;
    std::function<void(const std::string&)> on_cancelled;
};

// 缓存配置
struct CacheConfig {
    size_t max_cache_size;      // 最大缓存大小
    size_t max_file_size;       // 单个文件最大大小
    std::string cache_dir;      // 缓存目录
    bool enable_compression;    // 是否启用压缩
    uint32_t cleanup_interval;  // 清理间隔（秒）
    
    CacheConfig() : max_cache_size(50 * 1024 * 1024), max_file_size(10 * 1024 * 1024),
                   cache_dir("/spiffs/cache"), enable_compression(true), cleanup_interval(3600) {}
};

class MusicDownloader {
private:
    // 下载任务队列
    std::map<std::string, DownloadTask> download_tasks_;
    std::queue<std::string> task_queue_;
    SemaphoreHandle_t task_mutex_;
    
    // 下载线程
    TaskHandle_t download_task_handle_;
    QueueHandle_t command_queue_;
    std::atomic<bool> is_running_;
    
    // HTTP客户端
    esp_http_client_handle_t http_client_;
    esp_http_client_config_t http_config_;
    
    // 缓存管理
    CacheConfig cache_config_;
    std::map<std::string, std::string> cache_index_;
    SemaphoreHandle_t cache_mutex_;
    
    // 回调函数
    DownloadCallbacks callbacks_;
    
    // 统计信息
    std::atomic<size_t> total_downloaded_;
    std::atomic<size_t> total_errors_;
    std::atomic<size_t> cache_hits_;
    std::atomic<size_t> cache_misses_;
    
    // 私有方法
    void DownloadTaskLoop();
    bool ProcessDownloadTask(DownloadTask& task);
    bool DownloadFile(const DownloadTask& task, std::function<bool(const uint8_t*, size_t)> callback);
    bool ResumeDownload(DownloadTask& task);
    void HandleDownloadError(DownloadTask& task, const std::string& error);
    void RetryTask(DownloadTask& task);
    
    // 缓存管理
    bool IsCached(const std::string& url);
    std::string GetCachePath(const std::string& url);
    bool SaveToCache(const std::string& url, const uint8_t* data, size_t size);
    bool LoadFromCache(const std::string& url, std::vector<uint8_t>& data);
    void CleanupCache();
    void UpdateCacheIndex();
    
    // 工具方法
    std::string GenerateTaskId(const std::string& url);
    std::string UrlToFilename(const std::string& url);
    size_t GetFileSize(const std::string& path);
    bool CreateDirectory(const std::string& path);
    
public:
    MusicDownloader();
    ~MusicDownloader();
    
    // 初始化和配置
    bool Initialize(const CacheConfig& config = CacheConfig());
    void Deinitialize();
    void SetCallbacks(const DownloadCallbacks& callbacks);
    
    // 下载管理
    std::string AddDownloadTask(const std::string& url, const std::string& file_path = "",
                               DownloadPriority priority = DownloadPriority::NORMAL);
    bool CancelDownload(const std::string& task_id);
    bool PauseDownload(const std::string& task_id);
    bool ResumeDownload(const std::string& task_id);
    bool RemoveDownloadTask(const std::string& task_id);
    
    // 批量操作
    void CancelAllDownloads();
    void PauseAllDownloads();
    void ResumeAllDownloads();
    
    // 任务查询
    DownloadState GetTaskState(const std::string& task_id) const;
    size_t GetTaskProgress(const std::string& task_id) const;
    const std::string& GetTaskError(const std::string& task_id) const;
    std::vector<std::string> GetActiveTasks() const;
    
    // 缓存操作
    bool ClearCache();
    size_t GetCacheSize() const;
    size_t GetCacheFileCount() const;
    bool IsCacheEnabled() const;
    
    // 统计信息
    size_t GetTotalDownloaded() const { return total_downloaded_; }
    size_t GetTotalErrors() const { return total_errors_; }
    size_t GetCacheHits() const { return cache_hits_; }
    size_t GetCacheMisses() const { return cache_misses_; }
    float GetCacheHitRate() const;
    
    // 网络配置
    void SetTimeout(uint32_t timeout_ms);
    void SetRetryCount(uint32_t retry_count);
    void SetUserAgent(const std::string& user_agent);
    
    // 调试和监控
    void PrintTaskList() const;
    void PrintCacheStats() const;
    void PrintNetworkStats() const;
};

#endif // MUSIC_DOWNLOADER_H 