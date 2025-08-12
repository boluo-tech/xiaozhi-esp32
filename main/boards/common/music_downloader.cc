#include "music_downloader.h"
#include "board.h"
#include "system_info.h"
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <queue>
#include <esp_vfs.h>
#include <esp_spiffs.h>
#include <esp_timer.h>

#define TAG "MusicDownloader"

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
            encoded += '+';
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

// 构建URL参数 - 暂时注释掉，因为未使用
/*
static std::string buildUrlWithParams(const std::string& base_url, const std::string& path, const std::string& query) {
    std::string result_url = base_url + path + "?";
    size_t pos = 0;
    size_t amp_pos = 0;

    while ((amp_pos = query.find("&", pos)) != std::string::npos) {
        std::string param = query.substr(pos, amp_pos - pos);
        size_t eq_pos = param.find("=");

        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value = param.substr(eq_pos + 1);
            result_url += key + "=" + url_encode(value) + "&";
        } else {
            result_url += param + "&";
        }

        pos = amp_pos + 1;
    }

    // 处理最后一个参数
    std::string last_param = query.substr(pos);
    size_t eq_pos = last_param.find("=");

    if (eq_pos != std::string::npos) {
        std::string key = last_param.substr(0, eq_pos);
        std::string value = last_param.substr(eq_pos + 1);
        result_url += key + "=" + url_encode(value);
    } else {
        result_url += last_param;
    }

    return result_url;
}
*/

MusicDownloader::MusicDownloader() : task_mutex_(nullptr), download_task_handle_(nullptr),
                                    command_queue_(nullptr), is_running_(false),
                                    http_client_(nullptr), cache_mutex_(nullptr),
                                    total_downloaded_(0), total_errors_(0),
                                    cache_hits_(0), cache_misses_(0) {
    ESP_LOGI(TAG, "MusicDownloader initialized");
}

MusicDownloader::~MusicDownloader() {
    ESP_LOGI(TAG, "Destroying MusicDownloader");
    Deinitialize();
}

bool MusicDownloader::Initialize(const CacheConfig& config) {
    ESP_LOGI(TAG, "Initializing MusicDownloader");

    cache_config_ = config;

    // 创建互斥锁
    task_mutex_ = xSemaphoreCreateMutex();
    cache_mutex_ = xSemaphoreCreateMutex();
    
    if (!task_mutex_ || !cache_mutex_) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return false;
    }

    // 创建命令队列
    command_queue_ = xQueueCreate(10, sizeof(std::string));
    if (!command_queue_) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return false;
    }

    // 初始化HTTP客户端配置
    memset(&http_config_, 0, sizeof(http_config_));
    http_config_.timeout_ms = 30000;
    http_config_.buffer_size = 1024;
    http_config_.buffer_size_tx = 1024;

    // 创建缓存目录
    if (!CreateDirectory(cache_config_.cache_dir)) {
        ESP_LOGE(TAG, "Failed to create cache directory");
        return false;
    }

    // 启动下载任务
    is_running_ = true;
    BaseType_t ret = xTaskCreate([](void* arg) {
        MusicDownloader* downloader = static_cast<MusicDownloader*>(arg);
        downloader->DownloadTaskLoop();
        vTaskDelete(nullptr);
    }, "music_download", 8192, this, 5, &download_task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create download task");
        is_running_ = false;
        return false;
    }

    ESP_LOGI(TAG, "MusicDownloader initialized successfully");
    return true;
}

void MusicDownloader::Deinitialize() {
    ESP_LOGI(TAG, "Deinitializing MusicDownloader");

    // 停止下载任务
    is_running_ = false;

    if (download_task_handle_) {
        vTaskDelete(download_task_handle_);
        download_task_handle_ = nullptr;
    }

    // 清理资源
    if (task_mutex_) {
        vSemaphoreDelete(task_mutex_);
        task_mutex_ = nullptr;
    }

    if (cache_mutex_) {
        vSemaphoreDelete(cache_mutex_);
        cache_mutex_ = nullptr;
    }

    if (command_queue_) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
    }

    if (http_client_) {
        esp_http_client_cleanup(http_client_);
        http_client_ = nullptr;
    }

    // 清空任务列表
    download_tasks_.clear();
    while (!task_queue_.empty()) {
        task_queue_.pop();
    }

    ESP_LOGI(TAG, "MusicDownloader deinitialized");
}

void MusicDownloader::SetCallbacks(const DownloadCallbacks& callbacks) {
    callbacks_ = callbacks;
}

std::string MusicDownloader::AddDownloadTask(const std::string& url, const std::string& file_path,
                                           DownloadPriority priority) {
    if (url.empty()) {
        ESP_LOGE(TAG, "URL is empty");
        return "";
    }

    std::string task_id = GenerateTaskId(url);
    
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 检查任务是否已存在
        if (download_tasks_.find(task_id) != download_tasks_.end()) {
            ESP_LOGW(TAG, "Task already exists: %s", task_id.c_str());
            xSemaphoreGive(task_mutex_);
            return task_id;
        }

        // 创建新任务
        DownloadTask task;
        task.id = task_id;
        task.url = url;
        task.file_path = file_path.empty() ? GetCachePath(url) : file_path;
        task.priority = priority;
        task.state = DownloadState::IDLE;

        download_tasks_[task_id] = task;
        task_queue_.push(task_id);

        ESP_LOGI(TAG, "Added download task: %s", task_id.c_str());
        xSemaphoreGive(task_mutex_);
        return task_id;
    }

    ESP_LOGE(TAG, "Failed to acquire task mutex");
    return "";
}

bool MusicDownloader::CancelDownload(const std::string& task_id) {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        auto it = download_tasks_.find(task_id);
        if (it != download_tasks_.end()) {
            it->second.state = DownloadState::ERROR;
            it->second.error_message = "Cancelled by user";
            
            if (callbacks_.on_cancelled) {
                callbacks_.on_cancelled(task_id);
            }
            
            ESP_LOGI(TAG, "Cancelled download task: %s", task_id.c_str());
            xSemaphoreGive(task_mutex_);
            return true;
        }
        xSemaphoreGive(task_mutex_);
    }
    return false;
}

bool MusicDownloader::PauseDownload(const std::string& task_id) {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        auto it = download_tasks_.find(task_id);
        if (it != download_tasks_.end() && it->second.state == DownloadState::DOWNLOADING) {
            it->second.state = DownloadState::PAUSED;
            ESP_LOGI(TAG, "Paused download task: %s", task_id.c_str());
            xSemaphoreGive(task_mutex_);
            return true;
        }
        xSemaphoreGive(task_mutex_);
    }
    return false;
}

bool MusicDownloader::ResumeDownload(const std::string& task_id) {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        auto it = download_tasks_.find(task_id);
        if (it != download_tasks_.end() && it->second.state == DownloadState::PAUSED) {
            it->second.state = DownloadState::IDLE;
            task_queue_.push(task_id);
            ESP_LOGI(TAG, "Resumed download task: %s", task_id.c_str());
            xSemaphoreGive(task_mutex_);
            return true;
        }
        xSemaphoreGive(task_mutex_);
    }
    return false;
}

bool MusicDownloader::RemoveDownloadTask(const std::string& task_id) {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        auto it = download_tasks_.find(task_id);
        if (it != download_tasks_.end()) {
            download_tasks_.erase(it);
            ESP_LOGI(TAG, "Removed download task: %s", task_id.c_str());
            xSemaphoreGive(task_mutex_);
            return true;
        }
        xSemaphoreGive(task_mutex_);
    }
    return false;
}

void MusicDownloader::CancelAllDownloads() {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (auto& pair : download_tasks_) {
            pair.second.state = DownloadState::ERROR;
            pair.second.error_message = "Cancelled all";
            
            if (callbacks_.on_cancelled) {
                callbacks_.on_cancelled(pair.first);
            }
        }
        
        while (!task_queue_.empty()) {
            task_queue_.pop();
        }
        
        ESP_LOGI(TAG, "Cancelled all download tasks");
        xSemaphoreGive(task_mutex_);
    }
}

void MusicDownloader::PauseAllDownloads() {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (auto& pair : download_tasks_) {
            if (pair.second.state == DownloadState::DOWNLOADING) {
                pair.second.state = DownloadState::PAUSED;
            }
        }
        ESP_LOGI(TAG, "Paused all download tasks");
        xSemaphoreGive(task_mutex_);
    }
}

void MusicDownloader::ResumeAllDownloads() {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (auto& pair : download_tasks_) {
            if (pair.second.state == DownloadState::PAUSED) {
                pair.second.state = DownloadState::IDLE;
                task_queue_.push(pair.first);
            }
        }
        ESP_LOGI(TAG, "Resumed all download tasks");
        xSemaphoreGive(task_mutex_);
    }
}

DownloadState MusicDownloader::GetTaskState(const std::string& task_id) const {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        auto it = download_tasks_.find(task_id);
        if (it != download_tasks_.end()) {
            DownloadState state = it->second.state;
            xSemaphoreGive(task_mutex_);
            return state;
        }
        xSemaphoreGive(task_mutex_);
    }
    return DownloadState::ERROR;
}

size_t MusicDownloader::GetTaskProgress(const std::string& task_id) const {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        auto it = download_tasks_.find(task_id);
        if (it != download_tasks_.end()) {
            size_t progress = it->second.total_size > 0 ? 
                (it->second.downloaded_size * 100 / it->second.total_size) : 0;
            xSemaphoreGive(task_mutex_);
            return progress;
        }
        xSemaphoreGive(task_mutex_);
    }
    return 0;
}

const std::string& MusicDownloader::GetTaskError(const std::string& task_id) const {
    static const std::string empty_error;
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        auto it = download_tasks_.find(task_id);
        if (it != download_tasks_.end()) {
            const std::string& error = it->second.error_message;
            xSemaphoreGive(task_mutex_);
            return error;
        }
        xSemaphoreGive(task_mutex_);
    }
    return empty_error;
}

std::vector<std::string> MusicDownloader::GetActiveTasks() const {
    std::vector<std::string> active_tasks;
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (const auto& pair : download_tasks_) {
            if (pair.second.state == DownloadState::DOWNLOADING || 
                pair.second.state == DownloadState::CONNECTING) {
                active_tasks.push_back(pair.first);
            }
        }
        xSemaphoreGive(task_mutex_);
    }
    return active_tasks;
}

bool MusicDownloader::ClearCache() {
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 清理缓存文件
        DIR* dir = opendir(cache_config_.cache_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_REG) { // 普通文件
                    std::string file_path = cache_config_.cache_dir + "/" + entry->d_name;
                    unlink(file_path.c_str());
                }
            }
            closedir(dir);
        }
        
        cache_index_.clear();
        ESP_LOGI(TAG, "Cache cleared");
        xSemaphoreGive(cache_mutex_);
        return true;
    }
    return false;
}

size_t MusicDownloader::GetCacheSize() const {
    size_t total_size = 0;
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        DIR* dir = opendir(cache_config_.cache_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_REG) {
                    std::string file_path = cache_config_.cache_dir + "/" + entry->d_name;
                    struct stat st;
                    if (stat(file_path.c_str(), &st) == 0) {
                        total_size += st.st_size;
                    }
                }
            }
            closedir(dir);
        }
        xSemaphoreGive(cache_mutex_);
    }
    return total_size;
}

size_t MusicDownloader::GetCacheFileCount() const {
    size_t count = 0;
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        DIR* dir = opendir(cache_config_.cache_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_REG) {
                    count++;
                }
            }
            closedir(dir);
        }
        xSemaphoreGive(cache_mutex_);
    }
    return count;
}

bool MusicDownloader::IsCacheEnabled() const {
    return cache_config_.max_cache_size > 0;
}

float MusicDownloader::GetCacheHitRate() const {
    size_t total = cache_hits_ + cache_misses_;
    return total > 0 ? (float)cache_hits_ / total : 0.0f;
}

void MusicDownloader::SetTimeout(uint32_t timeout_ms) {
    http_config_.timeout_ms = timeout_ms;
}

void MusicDownloader::SetRetryCount(uint32_t retry_count) {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (auto& pair : download_tasks_) {
            pair.second.max_retries = retry_count;
        }
        xSemaphoreGive(task_mutex_);
    }
}

void MusicDownloader::SetUserAgent(const std::string& user_agent) {
    http_config_.user_agent = user_agent.c_str();
}

void MusicDownloader::PrintTaskList() const {
    ESP_LOGI(TAG, "=== Download Task List ===");
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (const auto& pair : download_tasks_) {
            const DownloadTask& task = pair.second;
            ESP_LOGI(TAG, "Task: %s, State: %d, Progress: %zu%%, URL: %s",
                     task.id.c_str(), (int)task.state, GetTaskProgress(task.id), task.url.c_str());
        }
        xSemaphoreGive(task_mutex_);
    }
    ESP_LOGI(TAG, "=== End Task List ===");
}

void MusicDownloader::PrintCacheStats() const {
    ESP_LOGI(TAG, "=== Cache Statistics ===");
    ESP_LOGI(TAG, "Cache Size: %zu bytes", GetCacheSize());
    ESP_LOGI(TAG, "File Count: %zu", GetCacheFileCount());
    ESP_LOGI(TAG, "Cache Hits: %zu", cache_hits_.load());
    ESP_LOGI(TAG, "Cache Misses: %zu", cache_misses_.load());
    ESP_LOGI(TAG, "Hit Rate: %.2f%%", GetCacheHitRate() * 100.0f);
    ESP_LOGI(TAG, "=== End Cache Stats ===");
}

void MusicDownloader::PrintNetworkStats() const {
    ESP_LOGI(TAG, "=== Network Statistics ===");
    ESP_LOGI(TAG, "Total Downloaded: %zu bytes", total_downloaded_.load());
    ESP_LOGI(TAG, "Total Errors: %zu", total_errors_.load());
    ESP_LOGI(TAG, "Active Tasks: %zu", GetActiveTasks().size());
    ESP_LOGI(TAG, "=== End Network Stats ===");
}

// 私有方法实现
void MusicDownloader::DownloadTaskLoop() {
    ESP_LOGI(TAG, "Download task loop started");

    while (is_running_) {
        std::string task_id;
        
        // 获取下一个任务
        if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!task_queue_.empty()) {
                task_id = task_queue_.front();
                task_queue_.pop();
            }
            xSemaphoreGive(task_mutex_);
        }

        if (!task_id.empty()) {
            // 处理下载任务
            auto it = download_tasks_.find(task_id);
            if (it != download_tasks_.end()) {
                ProcessDownloadTask(it->second);
            }
        } else {
            // 没有任务时等待
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "Download task loop ended");
}

bool MusicDownloader::ProcessDownloadTask(DownloadTask& task) {
    ESP_LOGI(TAG, "Processing download task: %s", task.id.c_str());

    // 检查缓存
    if (IsCached(task.url)) {
        ESP_LOGI(TAG, "File found in cache: %s", task.url.c_str());
        cache_hits_++;
        task.state = DownloadState::COMPLETED;
        
        if (callbacks_.on_completed) {
            callbacks_.on_completed(task.id);
        }
        return true;
    }

    cache_misses_++;

    // 开始下载
    task.state = DownloadState::CONNECTING;
    
    bool success = DownloadFile(task, [this, &task](const uint8_t* data, size_t size) -> bool {
        // 检查任务是否被取消或暂停
        if (task.state == DownloadState::ERROR || task.state == DownloadState::PAUSED) {
            return false;
        }

        // 更新进度
        task.downloaded_size += size;
        total_downloaded_ += size;

        if (callbacks_.on_progress) {
            callbacks_.on_progress(task.id, task.downloaded_size, task.total_size);
        }

        return true;
    });

    if (success) {
        task.state = DownloadState::COMPLETED;
        ESP_LOGI(TAG, "Download completed: %s", task.id.c_str());
        
        if (callbacks_.on_completed) {
            callbacks_.on_completed(task.id);
        }
    } else {
        task.state = DownloadState::ERROR;
        total_errors_++;
        ESP_LOGE(TAG, "Download failed: %s", task.id.c_str());
        
        if (callbacks_.on_error) {
            callbacks_.on_error(task.id, task.error_message);
        }
    }

    return success;
}

bool MusicDownloader::DownloadFile(const DownloadTask& task, std::function<bool(const uint8_t*, size_t)> callback) {
    ESP_LOGI(TAG, "Downloading file: %s", task.url.c_str());

    // 创建HTTP客户端
    http_client_ = esp_http_client_init(&http_config_);
    if (!http_client_) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    // 设置URL
    esp_http_client_set_url(http_client_, task.url.c_str());
    esp_http_client_set_method(http_client_, HTTP_METHOD_GET);

    // 设置请求头
    esp_http_client_set_header(http_client_, "User-Agent", "ESP32-Music-Player/1.0");
    esp_http_client_set_header(http_client_, "Accept", "*/*");
    esp_http_client_set_header(http_client_, "Device-Id", SystemInfo::GetMacAddress().c_str());
    esp_http_client_set_header(http_client_, "Client-Id", Board::GetInstance().GetUuid().c_str());
    esp_http_client_set_header(http_client_, "Board-Type", Board::GetInstance().GetBoardType().c_str());

    // 执行请求
    esp_err_t err = esp_http_client_open(http_client_, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(http_client_);
        http_client_ = nullptr;
        return false;
    }

    // 获取内容长度
    int content_length = esp_http_client_fetch_headers(http_client_);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Failed to fetch headers");
        esp_http_client_cleanup(http_client_);
        http_client_ = nullptr;
        return false;
    }

    // 检查状态码
    int status_code = esp_http_client_get_status_code(http_client_);
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed with status: %d", status_code);
        esp_http_client_cleanup(http_client_);
        http_client_ = nullptr;
        return false;
    }

    // 读取数据
    uint8_t buffer[1024];
    int read_len;
    bool download_success = true;

    while ((read_len = esp_http_client_read(http_client_, (char*)buffer, sizeof(buffer))) > 0) {
        if (!callback(buffer, read_len)) {
            download_success = false;
            break;
        }
    }

    esp_http_client_cleanup(http_client_);
    http_client_ = nullptr;

    return download_success && read_len >= 0;
}

bool MusicDownloader::ResumeDownload(DownloadTask& task) {
    // 实现断点续传逻辑
    ESP_LOGI(TAG, "Resuming download: %s", task.id.c_str());
    return DownloadFile(task, [this, &task](const uint8_t* data, size_t size) -> bool {
        // 跳过已下载的部分
        if (task.resume_position > 0) {
            if (task.resume_position >= size) {
                task.resume_position -= size;
                return true;
            } else {
                data += task.resume_position;
                size -= task.resume_position;
                task.resume_position = 0;
            }
        }

        // 更新进度
        task.downloaded_size += size;
        total_downloaded_ += size;

        if (callbacks_.on_progress) {
            callbacks_.on_progress(task.id, task.downloaded_size, task.total_size);
        }

        return true;
    });
}

void MusicDownloader::HandleDownloadError(DownloadTask& task, const std::string& error) {
    task.error_message = error;
    task.retry_count++;

    if (task.retry_count < task.max_retries) {
                ESP_LOGW(TAG, "Retrying download: %s (attempt %u/%u)", task.id.c_str(),
                 (unsigned int)task.retry_count, (unsigned int)task.max_retries);
        RetryTask(task);
    } else {
        ESP_LOGE(TAG, "Download failed after %u retries: %s", (unsigned int)task.max_retries, task.id.c_str());
        task.state = DownloadState::ERROR;
        total_errors_++;
        
        if (callbacks_.on_error) {
            callbacks_.on_error(task.id, error);
        }
    }
}

void MusicDownloader::RetryTask(DownloadTask& task) {
    task.state = DownloadState::IDLE;
    task_queue_.push(task.id);
}

// 缓存管理方法
bool MusicDownloader::IsCached(const std::string& url) {
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        std::string cache_path = GetCachePath(url);
        bool exists = access(cache_path.c_str(), F_OK) == 0;
        xSemaphoreGive(cache_mutex_);
        return exists;
    }
    return false;
}

std::string MusicDownloader::GetCachePath(const std::string& url) {
    std::string filename = UrlToFilename(url);
    return cache_config_.cache_dir + "/" + filename;
}

bool MusicDownloader::SaveToCache(const std::string& url, const uint8_t* data, size_t size) {
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        std::string cache_path = GetCachePath(url);
        
        FILE* file = fopen(cache_path.c_str(), "wb");
        if (file) {
            size_t written = fwrite(data, 1, size, file);
            fclose(file);
            
            if (written == size) {
                cache_index_[url] = cache_path;
                ESP_LOGI(TAG, "Saved to cache: %s", url.c_str());
                xSemaphoreGive(cache_mutex_);
                return true;
            }
        }
        xSemaphoreGive(cache_mutex_);
    }
    return false;
}

bool MusicDownloader::LoadFromCache(const std::string& url, std::vector<uint8_t>& data) {
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        std::string cache_path = GetCachePath(url);
        
        FILE* file = fopen(cache_path.c_str(), "rb");
        if (file) {
            fseek(file, 0, SEEK_END);
            size_t size = ftell(file);
            fseek(file, 0, SEEK_SET);
            
            data.resize(size);
            size_t read = fread(data.data(), 1, size, file);
            fclose(file);
            
            if (read == size) {
                ESP_LOGI(TAG, "Loaded from cache: %s", url.c_str());
                xSemaphoreGive(cache_mutex_);
                return true;
            }
        }
        xSemaphoreGive(cache_mutex_);
    }
    return false;
}

void MusicDownloader::CleanupCache() {
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        size_t current_size = GetCacheSize();
        
        if (current_size > cache_config_.max_cache_size) {
            // 简单的LRU清理策略：删除最旧的文件
            DIR* dir = opendir(cache_config_.cache_dir.c_str());
            if (dir) {
                std::vector<std::pair<std::string, time_t>> files;
                struct dirent* entry;
                
                while ((entry = readdir(dir)) != nullptr) {
                    if (entry->d_type == DT_REG) {
                        std::string file_path = cache_config_.cache_dir + "/" + entry->d_name;
                        struct stat st;
                        if (stat(file_path.c_str(), &st) == 0) {
                            files.push_back({file_path, st.st_mtime});
                        }
                    }
                }
                closedir(dir);
                
                // 按修改时间排序
                std::sort(files.begin(), files.end(), 
                         [](const auto& a, const auto& b) { return a.second < b.second; });
                
                // 删除最旧的文件直到缓存大小符合要求
                for (const auto& file : files) {
                    if (GetCacheSize() <= cache_config_.max_cache_size) {
                        break;
                    }
                    unlink(file.first.c_str());
                    ESP_LOGI(TAG, "Cleaned up cache file: %s", file.first.c_str());
                }
            }
        }
        xSemaphoreGive(cache_mutex_);
    }
}

void MusicDownloader::UpdateCacheIndex() {
    // 更新缓存索引
    if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
        cache_index_.clear();
        
        DIR* dir = opendir(cache_config_.cache_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_REG) {
                    std::string file_path = cache_config_.cache_dir + "/" + entry->d_name;
                    // 这里可以添加URL到文件路径的映射逻辑
                }
            }
            closedir(dir);
        }
        xSemaphoreGive(cache_mutex_);
    }
}

// 工具方法
std::string MusicDownloader::GenerateTaskId(const std::string& url) {
    // 简单的哈希算法生成任务ID
    size_t hash = 0;
    for (char c : url) {
        hash = hash * 31 + c;
    }
    
    char id[16];
    snprintf(id, sizeof(id), "%08zx", hash);
    return std::string(id);
}

std::string MusicDownloader::UrlToFilename(const std::string& url) {
    // 将URL转换为安全的文件名
    std::string filename = url;
    
    // 替换不安全的字符
    std::replace(filename.begin(), filename.end(), '/', '_');
    std::replace(filename.begin(), filename.end(), ':', '_');
    std::replace(filename.begin(), filename.end(), '?', '_');
    std::replace(filename.begin(), filename.end(), '&', '_');
    std::replace(filename.begin(), filename.end(), '=', '_');
    
    // 限制长度
    if (filename.length() > 100) {
        filename = filename.substr(0, 100);
    }
    
    return filename;
}

size_t MusicDownloader::GetFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
}

bool MusicDownloader::CreateDirectory(const std::string& path) {
    // 检查目录是否已存在
    DIR* dir = opendir(path.c_str());
    if (dir) {
        closedir(dir);
        return true;
    }
    
    // 创建目录
    int result = mkdir(path.c_str(), 0755);
    return result == 0;
} 