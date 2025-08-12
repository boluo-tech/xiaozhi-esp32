#include "esp32_music.h"
#include "board.h"
#include "system_info.h"
#include "audio_codecs/audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"
#include "sdkconfig.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>  // 为isdigit函数
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32Music"

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
            encoded += '+';  // 空格编码为'+'或'%20'
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

// 在文件开头添加一个辅助函数，统一处理URL构建
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

Esp32Music::Esp32Music() : last_downloaded_data_(), current_music_url_(), current_song_name_(),
                         song_name_displayed_(false), current_lyric_url_(), lyrics_(), 
                         current_lyric_index_(-1), lyric_thread_(), is_lyric_running_(false),
                         lyric_task_handle_(nullptr), is_playing_(false), is_downloading_(false), 
                         play_thread_(), download_thread_(), audio_buffer_(), buffer_mutex_(), 
                         buffer_cv_(), buffer_size_(0), mp3_decoder_(nullptr), mp3_frame_info_(), 
                         mp3_decoder_initialized_(false), playlist_task_handle_(nullptr) {
    ESP_LOGI(TAG, "Music player initialized");
    InitializeMp3Decoder();
}

Esp32Music::~Esp32Music() {
    ESP_LOGI(TAG, "Destroying music player - stopping all operations");
    
    // 停止所有操作
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;
    
    // 通知所有等待的线程
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // 等待下载线程结束，设置5秒超时
    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for download thread to finish (timeout: 5s)");
        auto start_time = std::chrono::steady_clock::now();
        
        // 等待线程结束
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 5) {
                ESP_LOGW(TAG, "Download thread join timeout after 5 seconds");
                break;
            }
            
            // 再次设置停止标志，确保线程能够检测到
            is_downloading_ = false;
            
            // 通知条件变量
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // 检查线程是否已经结束
            if (!download_thread_.joinable()) {
                thread_finished = true;
            }
            
            // 定期打印等待信息
            if (elapsed > 0 && elapsed % 1 == 0) {
                ESP_LOGI(TAG, "Still waiting for download thread to finish... (%ds)", (int)elapsed);
            }
        }
        
        if (download_thread_.joinable()) {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Download thread finished");
    }
    
    // 等待播放线程结束，设置3秒超时
    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for playback thread to finish (timeout: 3s)");
        auto start_time = std::chrono::steady_clock::now();
        
        bool thread_finished = false;
        while (!thread_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            if (elapsed >= 3) {
                ESP_LOGW(TAG, "Playback thread join timeout after 3 seconds");
                break;
            }
            
            // 再次设置停止标志
            is_playing_ = false;
            
            // 通知条件变量
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }
            
            // 检查线程是否已经结束
            if (!play_thread_.joinable()) {
                thread_finished = true;
            }
        }
        
        if (play_thread_.joinable()) {
            play_thread_.join();
        }
        ESP_LOGI(TAG, "Playback thread finished");
    }
    
    // 等待歌词线程结束
    if (lyric_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread finished");
    }
    
    // 停止歌词显示任务（如果是 FreeRTOS 任务）
    if (lyric_task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Stopping lyric display task");
        is_lyric_running_ = false;
        vTaskDelete(lyric_task_handle_);
        lyric_task_handle_ = nullptr;
        ESP_LOGI(TAG, "Lyric display task stopped");
    }
    
    // 清理缓冲区和MP3解码器
    ClearAudioBuffer();
    CleanupMp3Decoder();
    
    ESP_LOGI(TAG, "Music player destroyed successfully");
}

bool Esp32Music::Download(const std::string& song_name, bool auto_start) {
    ESP_LOGI(TAG, "Starting to get music details for: %s", song_name.c_str());
    
    // 清空之前的下载数据
    last_downloaded_data_.clear();
    
    // 保存歌名用于后续显示
    current_song_name_ = song_name;
    
    // 第一步：请求stream_pcm接口获取音频信息
    std::string api_url = CONFIG_MUSIC_SERVER_URL;
    api_url += "/stream_pcm";
    std::string full_url = api_url + "?song=" + url_encode(song_name);
    
    ESP_LOGI(TAG, "Request URL: %s", full_url.c_str());
    
    // 使用Board提供的HTTP客户端
    auto http = Board::GetInstance().CreateHttp();
    
    // 设置请求头
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
    http->SetHeader("Board-Type", Board::GetInstance().GetBoardType());
    
    // 打开GET连接
    if (!http->Open("GET", full_url)) {
        ESP_LOGE(TAG, "Failed to connect to music API");
        return false;
    }
    
    // 检查响应状态码
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        return false;
    }
    
    // 读取响应数据
    last_downloaded_data_ = http->ReadAll();
    http->Close();
    
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d", status_code, last_downloaded_data_.length());
    ESP_LOGD(TAG, "Complete music details response: %s", last_downloaded_data_.c_str());
    
    if (!last_downloaded_data_.empty()) {
        // 解析响应JSON以提取音频URL
        cJSON* response_json = cJSON_Parse(last_downloaded_data_.c_str());
        if (response_json) {
            // 提取关键信息
            cJSON* artist = cJSON_GetObjectItem(response_json, "artist");
            cJSON* title = cJSON_GetObjectItem(response_json, "title");
            cJSON* audio_url = cJSON_GetObjectItem(response_json, "audio_url");
            cJSON* lyric_url = cJSON_GetObjectItem(response_json, "lyric_url");
            
            if (cJSON_IsString(artist)) {
                ESP_LOGI(TAG, "Artist: %s", artist->valuestring);
            }
            if (cJSON_IsString(title)) {
                ESP_LOGI(TAG, "Title: %s", title->valuestring);
            }
            
            // 检查audio_url是否有效
            if (cJSON_IsString(audio_url) && audio_url->valuestring && strlen(audio_url->valuestring) > 0) {
                ESP_LOGI(TAG, "Audio URL path: %s", audio_url->valuestring);
                
                // 第二步：拼接完整的音频下载URL，确保对audio_url进行URL编码
                std::string base_url = CONFIG_MUSIC_SERVER_URL;
                std::string audio_path = audio_url->valuestring;
                
                // 使用统一的URL构建功能
                if (audio_path.find("?") != std::string::npos) {
                    size_t query_pos = audio_path.find("?");
                    std::string path = audio_path.substr(0, query_pos);
                    std::string query = audio_path.substr(query_pos + 1);
                    
                    current_music_url_ = buildUrlWithParams(base_url, path, query);
                } else {
                    current_music_url_ = base_url + audio_path;
                }
                
              
                ESP_LOGI(TAG, "Music details obtained for: %s", song_name.c_str());
                song_name_displayed_ = false;  // 重置歌名显示标志
                
                // 只有在 auto_start 为 true 时才自动开始播放
                if (auto_start) {
                    ESP_LOGI(TAG, "Auto-starting streaming playback for: %s", song_name.c_str());
                    StartStreaming(current_music_url_);
                } else {
                    ESP_LOGI(TAG, "Auto-start disabled, waiting for manual playback control");
                }
                
                // 处理歌词URL（只有在自动播放模式下才启动歌词显示）
                if (auto_start && cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0) {
                    // 拼接完整的歌词下载URL，使用相同的URL构建逻辑
                    std::string lyric_path = lyric_url->valuestring;
                    if (lyric_path.find("?") != std::string::npos) {
                        size_t query_pos = lyric_path.find("?");
                        std::string path = lyric_path.substr(0, query_pos);
                        std::string query = lyric_path.substr(query_pos + 1);
                        
                        current_lyric_url_ = buildUrlWithParams(base_url, path, query);
                    } else {
                        current_lyric_url_ = base_url + lyric_path;
                    }
                    
                    ESP_LOGI(TAG, "Loading lyrics for: %s", song_name.c_str());
                    
                    // 启动歌词下载和显示
                    if (is_lyric_running_) {
                        is_lyric_running_ = false;
                        if (lyric_thread_.joinable()) {
                            lyric_thread_.join();
                        }
                    }
                    
                    is_lyric_running_ = true;
                    current_lyric_index_ = -1;
                    lyrics_.clear();
                    
                    // 使用 xTaskCreate 替代 std::thread 避免 pthread 创建失败
                    BaseType_t ret = xTaskCreate([](void* arg) {
                        Esp32Music* music = static_cast<Esp32Music*>(arg);
                        music->LyricDisplayThread();
                        vTaskDelete(nullptr);
                    }, "lyric_display", 8192, this, 5, &lyric_task_handle_);
                    
                    if (ret != pdPASS) {
                        ESP_LOGE(TAG, "Failed to create lyric display task: %d", ret);
                        is_lyric_running_ = false;
                        lyric_task_handle_ = nullptr;
                        // 歌词显示任务创建失败不影响音乐播放，继续执行
                    } else {
                        ESP_LOGI(TAG, "Lyric display task created successfully");
                    }
                } else if (!auto_start && cJSON_IsString(lyric_url) && lyric_url->valuestring && strlen(lyric_url->valuestring) > 0) {
                    // 在歌单播放模式下，只保存歌词URL，不启动歌词显示任务
                    std::string lyric_path = lyric_url->valuestring;
                    if (lyric_path.find("?") != std::string::npos) {
                        size_t query_pos = lyric_path.find("?");
                        std::string path = lyric_path.substr(0, query_pos);
                        std::string query = lyric_path.substr(query_pos + 1);
                        
                        current_lyric_url_ = buildUrlWithParams(base_url, path, query);
                    } else {
                        current_lyric_url_ = base_url + lyric_path;
                    }
                    ESP_LOGI(TAG, "Lyric URL saved for later use: %s", song_name.c_str());
                } else {
                    ESP_LOGW(TAG, "No lyric URL found for this song");
                }
                
                cJSON_Delete(response_json);
                return true;
            } else {
                // audio_url为空或无效
                ESP_LOGE(TAG, "Audio URL not found or empty for song: %s", song_name.c_str());
                ESP_LOGE(TAG, "Failed to find music: 没有找到歌曲 '%s'", song_name.c_str());
                cJSON_Delete(response_json);
                return false;
            }
        } else {
            ESP_LOGE(TAG, "Failed to parse JSON response");
        }
    } else {
        ESP_LOGE(TAG, "Empty response from music API");
    }
    
    return false;
}

bool Esp32Music::Play() {
    if (is_playing_.load()) {  // 使用atomic的load()
        ESP_LOGW(TAG, "Music is already playing");
        return true;
    }
    
    if (last_downloaded_data_.empty()) {
        ESP_LOGE(TAG, "No music data to play");
        return false;
    }
    
    // 清理之前的播放线程
    if (play_thread_.joinable()) {
        play_thread_.join();
    }
    
    // 实际应调用流式播放接口
    return StartStreaming(current_music_url_, true);
}

bool Esp32Music::PlayInPlaylist() {
    if (last_downloaded_data_.empty()) {
        ESP_LOGE(TAG, "No music data to play");
        return false;
    }
    
    // 清理之前的播放线程
    if (play_thread_.joinable()) {
        play_thread_.join();
    }
    
    // 在歌单播放模式下启动歌词显示（如果之前没有启动）
    if (!current_lyric_url_.empty() && !is_lyric_running_) {
        ESP_LOGI(TAG, "Starting lyric display for playlist song");
        
        is_lyric_running_ = true;
        current_lyric_index_ = -1;
        lyrics_.clear();
        
        // 使用 xTaskCreate 替代 std::thread 避免 pthread 创建失败
        BaseType_t ret = xTaskCreate([](void* arg) {
            Esp32Music* music = static_cast<Esp32Music*>(arg);
            music->LyricDisplayThread();
            vTaskDelete(nullptr);
        }, "lyric_display", 8192, this, 5, &lyric_task_handle_);
        
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create lyric display task: %d", ret);
            is_lyric_running_ = false;
            lyric_task_handle_ = nullptr;
        } else {
            ESP_LOGI(TAG, "Lyric display task created successfully for playlist");
        }
    }
    
    // 在歌单播放模式下启动流式播放，不停止歌单
    return StartStreaming(current_music_url_, false);
}

bool Esp32Music::Stop(bool stop_playlist) {
    ESP_LOGI(TAG, "Stopping music, stop_playlist=%d", stop_playlist);
    // 1. 通知解码线程退出
    if (stop_playlist) {
        is_playing_ = false;  // 只有停止整个歌单时才设置
    }
    is_downloading_ = false;
    buffer_cv_.notify_all();

    // 2. 等待解码线程安全退出
    if (play_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for play thread to exit...");
        play_thread_.join();
        ESP_LOGI(TAG, "Play thread exited");
    }
    if (download_thread_.joinable()) {
        ESP_LOGI(TAG, "Waiting for download thread to exit...");
        download_thread_.join();
        ESP_LOGI(TAG, "Download thread exited");
    }

    // 3. 释放解码器
    if (mp3_decoder_ != nullptr) {
        ESP_LOGI(TAG, "Freeing MP3 decoder");
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
        mp3_decoder_initialized_ = false;
    }

    // 4. 歌单和歌词任务处理
    if (stop_playlist && playlist_task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Stopping playlist task...");
        vTaskDelete(playlist_task_handle_);
        playlist_task_handle_ = nullptr;
    }
    if (lyric_task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Stopping lyric display task");
        is_lyric_running_ = false;
        // 检查任务是否还在运行，避免删除已删除的任务
        if (eTaskGetState(lyric_task_handle_) != eDeleted) {
            vTaskDelete(lyric_task_handle_);
        }
        lyric_task_handle_ = nullptr;
    }

    // 5. 清空缓冲区
    ClearAudioBuffer();
    ESP_LOGI(TAG, "Music stopped successfully");
    return true;
}

std::string Esp32Music::GetDownloadResult() {
    return last_downloaded_data_;
}

// 开始流式播放
bool Esp32Music::StartStreaming(const std::string& music_url, bool stop_playlist) {
    ESP_LOGI(TAG, "Starting streaming playback for: %s", music_url.c_str());

    // 只在未初始化时初始化解码器
    if (!mp3_decoder_initialized_ || mp3_decoder_ == nullptr) {
        if (!InitializeMp3Decoder()) {
            ESP_LOGE(TAG, "Failed to initialize MP3 decoder before streaming");
            return false;
        }
    }
    
    if (music_url.empty()) {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    
    ESP_LOGD(TAG, "Starting streaming for URL: %s", music_url.c_str());
    
    // 停止之前的播放和下载
    is_downloading_ = false;
    if (stop_playlist) {
        is_playing_ = false;
    }
    
    // 等待之前的线程完全结束
    if (download_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // 通知线程退出
        }
        download_thread_.join();
    }
    if (play_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all();  // 通知线程退出
        }
        play_thread_.join();
    }
    
    // 清空缓冲区
    ClearAudioBuffer();
    
    // 配置线程栈大小以避免栈溢出
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 8192;  // 8KB栈大小
    cfg.prio = 5;           // 中等优先级
    cfg.thread_name = "audio_stream";
    esp_pthread_set_cfg(&cfg);
    
    // 开始下载线程
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32Music::DownloadAudioStream, this, music_url);
    
    // 开始播放线程（会等待缓冲区有足够数据）
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32Music::PlayAudioStream, this);
    
    ESP_LOGI(TAG, "Streaming threads started successfully");
    return true;
}

// 停止流式播放
bool Esp32Music::StopStreaming() {
    ESP_LOGI(TAG, "Stopping music streaming - current state: downloading=%d, playing=%d", 
            is_downloading_.load(), is_playing_.load());

    // 重置采样率到原始值
    ResetSampleRate();
    
    // 检查是否有流式播放正在进行
    if (!is_playing_ && !is_downloading_) {
        ESP_LOGW(TAG, "No streaming in progress");
        return true;
    }
    
    // 停止下载和播放标志
    is_downloading_ = false;
    is_playing_ = false;
    
    // 清空歌名显示
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");  // 清空歌名显示
        ESP_LOGI(TAG, "Cleared song name display");
    }
    
    // 通知所有等待的线程
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    ESP_LOGI(TAG, "Music streaming stop signal sent");
    return true;
}

// 流式下载音频数据
void Esp32Music::DownloadAudioStream(const std::string& music_url) {
    ESP_LOGD(TAG, "Starting audio stream download from: %s", music_url.c_str());
    
    // 验证URL有效性
    if (music_url.empty() || music_url.find("http") != 0) {
        ESP_LOGE(TAG, "Invalid URL format: %s", music_url.c_str());
        is_downloading_ = false;
        return;
    }
    
    auto http = Board::GetInstance().CreateHttp();
    
    // 设置请求头
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "*/*");
    http->SetHeader("Range", "bytes=0-");  // 支持断点续传
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
    http->SetHeader("Board-Type", Board::GetInstance().GetBoardType());
    
    if (!http->Open("GET", music_url)) {
        ESP_LOGE(TAG, "Failed to connect to music stream URL");
        is_downloading_ = false;
        return;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code != 200 && status_code != 206) {  // 206 for partial content
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        is_downloading_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "Started downloading audio stream, status: %d", status_code);
    
    // 分块读取音频数据
    const size_t chunk_size = 4096;  // 4KB每块
    char buffer[chunk_size];
    size_t total_downloaded = 0;
    
    while (is_downloading_ && is_playing_) {
        int bytes_read = http->Read(buffer, chunk_size);
        if (bytes_read < 0) {
            ESP_LOGE(TAG, "Failed to read audio data: error code %d", bytes_read);
            break;
        }
        if (bytes_read == 0) {
            ESP_LOGI(TAG, "Audio stream download completed, total: %d bytes", total_downloaded);
            break;
        }
        
        // 打印数据块信息
        // ESP_LOGI(TAG, "Downloaded chunk: %d bytes at offset %d", bytes_read, total_downloaded);
        
        // 安全地打印数据块的十六进制内容（前16字节）
        if (bytes_read >= 16) {
            // ESP_LOGI(TAG, "Data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X ...", 
            //         (unsigned char)buffer[0], (unsigned char)buffer[1], (unsigned char)buffer[2], (unsigned char)buffer[3],
            //         (unsigned char)buffer[4], (unsigned char)buffer[5], (unsigned char)buffer[6], (unsigned char)buffer[7],
            //         (unsigned char)buffer[8], (unsigned char)buffer[9], (unsigned char)buffer[10], (unsigned char)buffer[11],
            //         (unsigned char)buffer[12], (unsigned char)buffer[13], (unsigned char)buffer[14], (unsigned char)buffer[15]);
        } else {
            ESP_LOGI(TAG, "Data chunk too small: %d bytes", bytes_read);
        }
        
        // 尝试检测文件格式（检查文件头）
        if (total_downloaded == 0 && bytes_read >= 4) {
            if (memcmp(buffer, "ID3", 3) == 0) {
                ESP_LOGI(TAG, "Detected MP3 file with ID3 tag");
            } else if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0) {
                ESP_LOGI(TAG, "Detected MP3 file header");
            } else if (memcmp(buffer, "RIFF", 4) == 0) {
                ESP_LOGI(TAG, "Detected WAV file");
            } else if (memcmp(buffer, "fLaC", 4) == 0) {
                ESP_LOGI(TAG, "Detected FLAC file");
            } else if (memcmp(buffer, "OggS", 4) == 0) {
                ESP_LOGI(TAG, "Detected OGG file");
            } else {
                ESP_LOGI(TAG, "Unknown audio format, first 4 bytes: %02X %02X %02X %02X", 
                        (unsigned char)buffer[0], (unsigned char)buffer[1], 
                        (unsigned char)buffer[2], (unsigned char)buffer[3]);
            }
        }
        
        // 创建音频数据块
        uint8_t* chunk_data = (uint8_t*)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for audio chunk");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);
        
        // 等待缓冲区有空间
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] { return buffer_size_ < MAX_BUFFER_SIZE || !is_downloading_; });
            
            if (is_downloading_) {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;
                
                // 通知播放线程有新数据
                buffer_cv_.notify_one();
                
                if (total_downloaded % (256 * 1024) == 0) {  // 每256KB打印一次进度
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer size: %d", total_downloaded, buffer_size_);
                }
            } else {
                heap_caps_free(chunk_data);
                break;
            }
        }
    }
    
    http->Close();
    is_downloading_ = false;
    
    // 通知播放线程下载完成
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    ESP_LOGI(TAG, "Audio stream download thread finished");
}

// 流式播放音频数据
void Esp32Music::PlayAudioStream() {
    ESP_LOGI(TAG, "Starting audio stream playback");
    
    // 初始化时间跟踪变量
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;
    
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec || !codec->output_enabled()) {
        ESP_LOGE(TAG, "Audio codec not available or not enabled");
        is_playing_ = false;
        return;
    }
    
    if (!mp3_decoder_initialized_) {
        ESP_LOGE(TAG, "MP3 decoder not initialized");
        is_playing_ = false;
        return;
    }
    
    
    // 等待缓冲区有足够数据开始播放
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this] { 
            return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); 
        });
    }
    
 
    ESP_LOGI(TAG, "Starting playback with buffer size: %d", buffer_size_);
    
    size_t total_played = 0;
    uint8_t* mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t* read_ptr = nullptr;
    
    // 分配MP3输入缓冲区
    mp3_input_buffer = (uint8_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer) {
        ESP_LOGE(TAG, "Failed to allocate MP3 input buffer");
        is_playing_ = false;
        return;
    }
    
    // 标记是否已经处理过ID3标签
    bool id3_processed = false;
    
    while (is_playing_) {
        // 检查设备状态，只有在空闲状态才播放音乐
        auto& app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();
        
        // 如果用户唤醒（进入listening状态），立即停止播放
        if (current_state == kDeviceStateListening) {
            ESP_LOGI(TAG, "Device is in listening state, emergency stopping music playback");
            break;  // 直接退出播放循环
        }
        
        // 如果设备状态不是idle或speaking，立即停止播放
        if (current_state != kDeviceStateIdle && current_state != kDeviceStateSpeaking) {
            ESP_LOGI(TAG, "Device state is %d, emergency stopping music playback", current_state);
            break;  // 直接退出播放循环
        }
        
        // 设备状态检查通过，显示当前播放的歌名
        if (!song_name_displayed_ && !current_song_name_.empty()) {
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (display) {
                // 格式化歌名显示为《歌名》播放中...
                std::string formatted_song_name = "《" + current_song_name_ + "》播放中...";
                display->SetMusicInfo(formatted_song_name.c_str());
                ESP_LOGI(TAG, "Displaying song name: %s", formatted_song_name.c_str());
                song_name_displayed_ = true;
            }
        }
        
        // 如果需要更多MP3数据，从缓冲区读取
        if (bytes_left < 4096) {  // 保持至少4KB数据用于解码
            AudioChunk chunk;
            
            // 从缓冲区获取音频数据
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty()) {
                    if (!is_downloading_) {
                        // 下载完成且缓冲区为空，播放结束
                        ESP_LOGI(TAG, "Playback finished, total played: %d bytes", total_played);
                        break;
                    }
                    // 等待新数据
                    buffer_cv_.wait(lock, [this] { return !audio_buffer_.empty() || !is_downloading_; });
                    if (audio_buffer_.empty()) {
                        continue;
                    }
                }
                
                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;
                
                // 通知下载线程缓冲区有空间
                buffer_cv_.notify_one();
            }
            
            // 将新数据添加到MP3输入缓冲区
            if (chunk.data && chunk.size > 0) {
                // 移动剩余数据到缓冲区开头
                if (bytes_left > 0 && read_ptr != mp3_input_buffer) {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }
                
                // 检查缓冲区空间
                size_t space_available = 8192 - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);
                
                // 复制新数据
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;
                
                // 检查并跳过ID3标签（仅在开始时处理一次）
                if (!id3_processed && bytes_left >= 10) {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0) {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned int)id3_skip);
                    }
                    id3_processed = true;
                }
                
                // 释放chunk内存
                heap_caps_free(chunk.data);
            }
        }
        
        // 尝试找到MP3帧同步
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
            bytes_left = 0;
            continue;
        }
        
        // 跳过到同步位置
        if (sync_offset > 0) {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }
        
        // 解码MP3帧
        int16_t pcm_buffer[2304];
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
        
        if (decode_result == 0) {
            // 解码成功，获取帧信息
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
            total_frames_decoded_++;
            
            // 基本的帧信息有效性检查，防止除零错误
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0) {
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d, skipping", 
                        mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }
            
            // 计算当前帧的持续时间(毫秒)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) / 
                                  (mp3_frame_info_.samprate * mp3_frame_info_.nChans);
            
            // 更新当前播放时间
            current_play_time_ms_ += frame_duration_ms;
            
            ESP_LOGD(TAG, "Frame %d: time=%lldms, duration=%dms, rate=%d, ch=%d", 
                    total_frames_decoded_, current_play_time_ms_, frame_duration_ms,
                    mp3_frame_info_.samprate, mp3_frame_info_.nChans);
            
            // 更新歌词显示
            int buffer_latency_ms = 600; // 实测调整值
            UpdateLyricDisplay(current_play_time_ms_ + buffer_latency_ms);
            
            // 将PCM数据发送到Application的音频解码队列
            if (mp3_frame_info_.outputSamps > 0) {
                int16_t* final_pcm_data = pcm_buffer;
                int final_sample_count = mp3_frame_info_.outputSamps;
                std::vector<int16_t> mono_buffer;
                
                // 如果是双通道，转换为单通道混合
                if (mp3_frame_info_.nChans == 2) {
                    // 双通道转单通道：将左右声道混合
                    int stereo_samples = mp3_frame_info_.outputSamps;  // 包含左右声道的总样本数
                    int mono_samples = stereo_samples / 2;  // 实际的单声道样本数
                    
                    mono_buffer.resize(mono_samples);
                    
                    for (int i = 0; i < mono_samples; ++i) {
                        // 混合左右声道 (L + R) / 2
                        int left = pcm_buffer[i * 2];      // 左声道
                        int right = pcm_buffer[i * 2 + 1]; // 右声道
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }
                    
                    final_pcm_data = mono_buffer.data();
                    final_sample_count = mono_samples;

                    ESP_LOGD(TAG, "Converted stereo to mono: %d -> %d samples", 
                            stereo_samples, mono_samples);
                } else if (mp3_frame_info_.nChans == 1) {
                    // 已经是单声道，无需转换
                    ESP_LOGD(TAG, "Already mono audio: %d samples", final_sample_count);
                } else {
                    ESP_LOGW(TAG, "Unsupported channel count: %d, treating as mono", 
                            mp3_frame_info_.nChans);
                }
                
                // 创建AudioStreamPacket
                AudioStreamPacket packet;
                packet.sample_rate = mp3_frame_info_.samprate;
                packet.frame_duration = 60;  // 使用Application默认的帧时长
                packet.timestamp = 0;
                
                // 将int16_t PCM数据转换为uint8_t字节数组
                size_t pcm_size_bytes = final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);
                
                ESP_LOGD(TAG, "Sending %d PCM samples (%d bytes, rate=%d, channels=%d->1) to Application", 
                        final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                
                // 发送到Application的音频解码队列
                app.AddAudioData(std::move(packet));
                total_played += pcm_size_bytes;
                
                // 打印播放进度
                if (total_played % (128 * 1024) == 0) {
                    ESP_LOGI(TAG, "Played %d bytes, buffer size: %d", total_played, buffer_size_);
                }
            }
            
        } else {
            // 解码失败
            ESP_LOGW(TAG, "MP3 decode failed with error: %d", decode_result);
            
            // 跳过一些字节继续尝试
            if (bytes_left > 1) {
                read_ptr++;
                bytes_left--;
            } else {
                bytes_left = 0;
            }
        }
    }
    
    // 清理
    if (mp3_input_buffer) {
        heap_caps_free(mp3_input_buffer);
    }
    
    // 播放结束时清空歌名显示
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display) {
        display->SetMusicInfo("");  // 清空歌名显示
        ESP_LOGI(TAG, "Cleared song name display on playback end");
    }

    // 重置采样率到原始值
    ResetSampleRate();
    
    // 播放结束时保持音频输出启用状态，让Application管理
    // 不在这里禁用音频输出，避免干扰其他音频功能
    ESP_LOGI(TAG, "Audio stream playback finished, total played: %d bytes", total_played);
    
    // 播放完成后清空缓冲区，确保歌单循环能正确检测到播放完成
    ClearAudioBuffer();
    
    // 设置下载状态为false，让歌单循环能检测到播放完成
    // 这是必要的，否则 PlayPlaylist 循环中的条件 if (buffer_size == 0 && !is_downloading) 无法满足
    is_downloading_ = false;
    ESP_LOGI(TAG, "[DEBUG] PlayAudioStream finished, set is_downloading_ = false");
    
    // 添加短暂延迟，确保 PlayPlaylist 循环能检测到播放完成
    // 这样可以避免立即开始下一首歌的播放
    vTaskDelay(pdMS_TO_TICKS(500));
}

// 清空音频缓冲区
void Esp32Music::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    while (!audio_buffer_.empty()) {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }
    
    buffer_size_ = 0;
    ESP_LOGI(TAG, "Audio buffer cleared");
}

// 初始化MP3解码器
bool Esp32Music::InitializeMp3Decoder() {
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

// 清理MP3解码器
void Esp32Music::CleanupMp3Decoder() {
    if (mp3_decoder_ != nullptr) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

// 重置采样率到原始值
void Esp32Music::ResetSampleRate() {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (codec && codec->original_output_sample_rate() > 0 && 
        codec->output_sample_rate() != codec->original_output_sample_rate()) {
        ESP_LOGI(TAG, "重置采样率：从 %d Hz 重置到原始值 %d Hz", 
                codec->output_sample_rate(), codec->original_output_sample_rate());
        if (codec->SetOutputSampleRate(-1)) {  // -1 表示重置到原始值
            ESP_LOGI(TAG, "成功重置采样率到原始值: %d Hz", codec->output_sample_rate());
        } else {
            ESP_LOGW(TAG, "无法重置采样率到原始值");
        }
    }
}

// 跳过MP3文件开头的ID3标签
size_t Esp32Music::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) {
        return 0;
    }
    
    // 检查ID3v2标签头 "ID3"
    if (memcmp(data, "ID3", 3) != 0) {
        return 0;
    }
    
    // 计算标签大小（synchsafe integer格式）
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7)  |
                        ((uint32_t)(data[9] & 0x7F));
    
    // ID3v2头部(10字节) + 标签内容
    size_t total_skip = 10 + tag_size;
    
    // 确保不超过可用数据大小
    if (total_skip > size) {
        total_skip = size;
    }
    
    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}

// 下载歌词
bool Esp32Music::DownloadLyrics(const std::string& lyric_url) {
    ESP_LOGI(TAG, "Downloading lyrics from: %s", lyric_url.c_str());
    
    // 检查URL是否为空
    if (lyric_url.empty()) {
        ESP_LOGE(TAG, "Lyric URL is empty!");
        return false;
    }
    
    // 添加重试逻辑
    const int max_retries = 3;
    int retry_count = 0;
    bool success = false;
    std::string lyric_content;
    std::string current_url = lyric_url;
    int redirect_count = 0;
    const int max_redirects = 5;  // 最多允许5次重定向
    
    while (retry_count < max_retries && !success && redirect_count < max_redirects) {
        if (retry_count > 0) {
            ESP_LOGI(TAG, "Retrying lyric download (attempt %d of %d)", retry_count + 1, max_retries);
            // 重试前暂停一下
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        // 使用Board提供的HTTP客户端
        auto http = Board::GetInstance().CreateHttp();
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for lyric download");
            retry_count++;
            continue;
        }
        
        // 设置请求头
        http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
        http->SetHeader("Accept", "text/plain");
        http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
        http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
        http->SetHeader("Board-Type", Board::GetInstance().GetBoardType());
        
        // 打开GET连接
        if (!http->Open("GET", current_url)) {
            ESP_LOGE(TAG, "Failed to open HTTP connection for lyrics");
            delete http;
            retry_count++;
            continue;
        }
        
        // 检查HTTP状态码
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Lyric download HTTP status code: %d", status_code);
        
        // 处理重定向 - 由于Http类没有GetHeader方法，我们只能根据状态码判断
        if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
            // 由于无法获取Location头，只能报告重定向但无法继续
            ESP_LOGW(TAG, "Received redirect status %d but cannot follow redirect (no GetHeader method)", status_code);
            http->Close();
            delete http;
            retry_count++;
            continue;
        }
        
        // 非200系列状态码视为错误
        if (status_code < 200 || status_code >= 300) {
            ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
            http->Close();
            delete http;
            retry_count++;
            continue;
        }
        
        // 读取响应
        lyric_content.clear();
        char buffer[1024];
        int bytes_read;
        bool read_error = false;
        int total_read = 0;
        
        // 由于无法获取Content-Length和Content-Type头，我们不知道预期大小和内容类型
        ESP_LOGD(TAG, "Starting to read lyric content");
        
        while (true) {
            bytes_read = http->Read(buffer, sizeof(buffer) - 1);
            // ESP_LOGD(TAG, "Lyric HTTP read returned %d bytes", bytes_read); // 注释掉以减少日志输出
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                lyric_content += buffer;
                total_read += bytes_read;
                
                // 定期打印下载进度 - 改为DEBUG级别减少输出
                if (total_read % 4096 == 0) {
                    ESP_LOGD(TAG, "Downloaded %d bytes so far", total_read);
                }
            } else if (bytes_read == 0) {
                // 正常结束，没有更多数据
                ESP_LOGD(TAG, "Lyric download completed, total bytes: %d", total_read);
                success = true;
                break;
            } else {
                // bytes_read < 0，可能是ESP-IDF的已知问题
                // 如果已经读取到了一些数据，则认为下载成功
                if (!lyric_content.empty()) {
                    ESP_LOGW(TAG, "HTTP read returned %d, but we have data (%d bytes), continuing", bytes_read, lyric_content.length());
                    success = true;
                    break;
                } else {
                    ESP_LOGE(TAG, "Failed to read lyric data: error code %d", bytes_read);
                    read_error = true;
                    break;
                }
            }
        }
        
        http->Close();
        delete http;
        
        if (read_error) {
            retry_count++;
            continue;
        }
        
        // 如果成功读取数据，跳出重试循环
        if (success) {
            break;
        }
    }
    
    // 检查是否超过了最大重试次数
    if (retry_count >= max_retries) {
        ESP_LOGE(TAG, "Failed to download lyrics after %d attempts", max_retries);
        return false;
    }
    
    // 记录前几个字节的数据，帮助调试
    if (!lyric_content.empty()) {
        size_t preview_size = std::min(lyric_content.size(), size_t(50));
        std::string preview = lyric_content.substr(0, preview_size);
        ESP_LOGD(TAG, "Lyric content preview (%d bytes): %s", lyric_content.length(), preview.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to download lyrics or lyrics are empty");
        return false;
    }
    
    ESP_LOGI(TAG, "Lyrics downloaded successfully, size: %d bytes", lyric_content.length());
    return ParseLyrics(lyric_content);
}

// 解析歌词
bool Esp32Music::ParseLyrics(const std::string& lyric_content) {
    ESP_LOGI(TAG, "Parsing lyrics content");
    
    // 使用锁保护lyrics_数组访问
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    lyrics_.clear();
    
    // 限制歌词行数，避免内存无限增长
    const size_t MAX_LYRIC_LINES = 100;
    size_t line_count = 0;
    
    // 按行分割歌词内容
    std::istringstream stream(lyric_content);
    std::string line;
    
    while (std::getline(stream, line)) {
        // 去除行尾的回车符
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // 跳过空行
        if (line.empty()) {
            continue;
        }
        
        // 解析LRC格式: [mm:ss.xx]歌词文本
        if (line.length() > 10 && line[0] == '[') {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos) {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);
                
                // 检查是否是元数据标签而不是时间戳
                // 元数据标签通常是 [ti:标题], [ar:艺术家], [al:专辑] 等
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos) {
                    std::string left_part = tag_or_time.substr(0, colon_pos);
                    
                    // 检查冒号左边是否是时间（数字）
                    bool is_time_format = true;
                    for (char c : left_part) {
                        if (!isdigit(c)) {
                            is_time_format = false;
                            break;
                        }
                    }
                    
                    // 如果不是时间格式，跳过这一行（元数据标签）
                    if (!is_time_format) {
                        // 可以在这里处理元数据，例如提取标题、艺术家等信息
                        ESP_LOGD(TAG, "Skipping metadata tag: [%s]", tag_or_time.c_str());
                        continue;
                    }
                    
                    // 是时间格式，解析时间戳
                    try {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);
                        
                        // 安全处理歌词文本，确保UTF-8编码正确
                        std::string safe_lyric_text;
                        if (!content.empty()) {
                            // 创建安全副本并验证字符串
                            safe_lyric_text = content;
                            // 确保字符串以null结尾
                            safe_lyric_text.shrink_to_fit();
                        }
                        
                        // 限制歌词行数
                        if (line_count >= MAX_LYRIC_LINES) {
                            ESP_LOGW(TAG, "Reached maximum lyric lines limit (%zu), skipping remaining lines", MAX_LYRIC_LINES);
                            break;
                        }
                        
                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));
                        line_count++;
                        
                        if (!safe_lyric_text.empty()) {
                            // 限制日志输出长度，避免中文字符截断问题
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] %s", timestamp_ms, log_text.c_str());
                        } else {
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] (empty)", timestamp_ms);
                        }
                    } catch (const std::exception& e) {
                        ESP_LOGW(TAG, "Failed to parse time: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }
    
    // 按时间戳排序
    std::sort(lyrics_.begin(), lyrics_.end());
    
    ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
    return !lyrics_.empty();
}

// 歌词显示线程
void Esp32Music::LyricDisplayThread() {
    ESP_LOGI(TAG, "Lyric display thread started");
    
    if (!DownloadLyrics(current_lyric_url_)) {
        ESP_LOGE(TAG, "Failed to download or parse lyrics");
        is_lyric_running_ = false;
        return;
    }
    
    // 定期检查是否需要更新显示(频率可以降低)
    while (is_lyric_running_ && is_playing_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    ESP_LOGI(TAG, "Lyric display thread finished");
}

void Esp32Music::UpdateLyricDisplay(int64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(lyrics_mutex_);
    
    if (lyrics_.empty()) {
        return;
    }
    
    // 查找当前应该显示的歌词
    int new_lyric_index = -1;
    
    // 从当前歌词索引开始查找，提高效率
    int start_index = (current_lyric_index_.load() >= 0) ? current_lyric_index_.load() : 0;
    
    // 正向查找：找到最后一个时间戳小于等于当前时间的歌词
    for (int i = start_index; i < (int)lyrics_.size(); i++) {
        if (lyrics_[i].first <= current_time_ms) {
            new_lyric_index = i;
        } else {
            break;  // 时间戳已超过当前时间
        }
    }
    
    // 如果没有找到(可能当前时间比第一句歌词还早)，显示空
    if (new_lyric_index == -1) {
        new_lyric_index = -1;
    }
    
    // 如果歌词索引发生变化，更新显示
    if (new_lyric_index != current_lyric_index_) {
        current_lyric_index_ = new_lyric_index;
        
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display) {
            std::string lyric_text;
            
            if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size()) {
                lyric_text = lyrics_[current_lyric_index_].second;
            }
            
            // 显示歌词
            display->SetChatMessage("lyric", lyric_text.c_str());
            
            ESP_LOGD(TAG, "Lyric update at %lldms: %s", 
                    current_time_ms, 
                    lyric_text.empty() ? "(no lyric)" : lyric_text.c_str());
        }
    }
}

bool Esp32Music::FetchPlaylist(const std::string& query, std::vector<std::string>& out_playlist) {
    out_playlist.clear();
    std::string api_url = CONFIG_MUSIC_SERVER_URL;
    api_url += "/playlist";
    if (!query.empty()) {
        api_url += "?query=" + url_encode(query);
    }
    auto http = Board::GetInstance().CreateHttp();
    http->SetHeader("User-Agent", "ESP32-Music-Player/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
    http->SetHeader("Board-Type", Board::GetInstance().GetBoardType());
    if (!http->Open("GET", api_url)) {
        ESP_LOGE(TAG, "Failed to connect to playlist API");
        return false;
    }
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP GET playlist failed: %d", status_code);
        http->Close();
        return false;
    }
    std::string response = http->ReadAll();
    http->Close();
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) return false;
    cJSON* success = cJSON_GetObjectItem(root, "success");
    if (!cJSON_IsBool(success) || !cJSON_IsTrue(success)) {
        cJSON_Delete(root);
        return false;
    }
    cJSON* playlist = cJSON_GetObjectItem(root, "playlist");
    if (!cJSON_IsArray(playlist)) {
        cJSON_Delete(root);
        return false;
    }
    int sz = cJSON_GetArraySize(playlist);
    for (int i = 0; i < sz; ++i) {
        cJSON* item = cJSON_GetArrayItem(playlist, i);
        if (cJSON_IsString(item)) {
            out_playlist.push_back(item->valuestring);
        }
    }
    cJSON_Delete(root);
    return !out_playlist.empty();
}

bool Esp32Music::PlayPlaylist(const std::string& query) {
    // 停止当前播放
    Stop(true);
    playlist_.clear();
    current_playlist_index_ = 0;
    if (!FetchPlaylist(query, playlist_)) {
        ESP_LOGE(TAG, "Failed to fetch playlist for query: %s", query.c_str());
        return false;
    }
    if (playlist_.empty()) {
        ESP_LOGE(TAG, "Playlist is empty for query: %s", query.c_str());
        return false;
    }
    
    ESP_LOGI(TAG, "[Playlist] Starting playlist with %d songs", (int)playlist_.size());
    
    // 启动播放线程
    is_playing_ = true;
    
    // 使用 FreeRTOS 任务替代 pthread，更好地管理资源
    BaseType_t ret = xTaskCreate([](void* arg) {
        Esp32Music* music = static_cast<Esp32Music*>(arg);
        
        while (music->is_playing_ && music->current_playlist_index_ < (int)music->playlist_.size()) {
            std::string song = music->playlist_[music->current_playlist_index_];
            ESP_LOGI(TAG, "[Playlist] Playing song %d/%d: %s (current_playlist_index_=%d)", 
                    music->current_playlist_index_+1, (int)music->playlist_.size(), song.c_str(), music->current_playlist_index_);
            
            // 下载歌曲信息（不自动开始播放）
            if (!music->Download(song, false)) {
                ESP_LOGW(TAG, "[Playlist] Failed to download song: %s", song.c_str());
                // 下载失败，跳到下一首
                ++music->current_playlist_index_;
                continue;
            }
            
            // 开始播放（使用歌单播放模式）
            if (!music->PlayInPlaylist()) {
                ESP_LOGW(TAG, "[Playlist] Failed to start playback for: %s", song.c_str());
                ++music->current_playlist_index_;
                continue;
            }
            
            ESP_LOGI(TAG, "[Playlist] Started playing: %s", song.c_str());
            
            // 确保设备状态为 idle，以便音乐能够正常播放
            auto& app = Application::GetInstance();
            int not_idle_count = 0;
            while (app.GetDeviceState() != kDeviceStateIdle) {
                not_idle_count++;
                if (not_idle_count > 50) { // 超过5秒都不是idle，强制切回idle
                    ESP_LOGW(TAG, "[Playlist] Device state stuck, force set to idle!");
                    app.SetDeviceState(kDeviceStateIdle);
                    not_idle_count = 0;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // 等待当前歌曲播放完成
            // 检查播放状态，直到歌曲播放结束或被中断
            int wait_count = 0;
            const int max_wait_count = 30000; // 最多等待30秒（假设一首歌最长30秒）
            
            while (music->is_playing_ && wait_count < max_wait_count) {
                // 检查是否还在播放状态
                if (!music->is_playing_.load()) {
                    ESP_LOGI(TAG, "[Playlist] Playback stopped by user");
                    break;
                }
                
                // 检查设备状态 - 更频繁和敏感的状态检查
                auto& app = Application::GetInstance();
                DeviceState current_state = app.GetDeviceState();
                
                // 如果用户唤醒（进入listening状态），立即停止音乐播放
                if (current_state == kDeviceStateListening) {
                    ESP_LOGI(TAG, "[Playlist] User wakeup detected (listening state), emergency stopping music playback");
                    music->EmergencyStop();  // 使用紧急停止
                    break;
                }
                
                // 如果设备状态不是idle或speaking，立即停止播放
                if (current_state != kDeviceStateIdle && current_state != kDeviceStateSpeaking) {
                    ESP_LOGI(TAG, "[Playlist] Device state changed to %d, emergency stopping music playback", current_state);
                    music->EmergencyStop();  // 使用紧急停止
                    break;
                }
                
                // 只有在设备状态为严重错误时才停止播放
                if (current_state == kDeviceStateFatalError || 
                    current_state == kDeviceStateStarting ||
                    current_state == kDeviceStateUpgrading) {
                    ESP_LOGI(TAG, "[Playlist] Device state critical (%d), stopping playlist", current_state);
                    music->Stop(true);
                    break;
                }
                
                // 如果设备状态是 speaking，继续播放音乐
                // 这个状态不影响音乐播放
                if (current_state == kDeviceStateSpeaking) {
                    // 继续播放，不停止
                    ESP_LOGD(TAG, "[Playlist] Device state %d, continuing music playback", current_state);
                }
                
                // 检查音频缓冲区是否还有数据在播放
                // 只有当缓冲区为空且下载也完成时，才认为歌曲播放结束
                size_t buffer_size = music->GetBufferSize();
                bool is_downloading = music->is_downloading_.load();
                
                ESP_LOGD(TAG, "[Playlist] Check: buffer_size=%zu, is_downloading=%d", buffer_size, is_downloading);
                
                if (buffer_size == 0 && !is_downloading) {
                    // 缓冲区为空且下载完成，歌曲播放结束
                    ESP_LOGI(TAG, "[Playlist] Audio buffer empty and download finished, song completed");
                    break;
                } else if (buffer_size == 0) {
                    // 缓冲区为空但还在下载，继续等待
                    ESP_LOGD(TAG, "[Playlist] Audio buffer empty but still downloading, waiting...");
                } else {
                    // 缓冲区还有数据，继续等待
                    ESP_LOGD(TAG, "[Playlist] Buffer size: %zu, downloading: %d, waiting for song to finish...", 
                            buffer_size, is_downloading);
                }
                
                vTaskDelay(pdMS_TO_TICKS(100));
                wait_count++;
            }
            
            if (wait_count >= max_wait_count) {
                ESP_LOGW(TAG, "[Playlist] Song playback timeout: %s", song.c_str());
                ESP_LOGW(TAG, "[Playlist] Final buffer size: %zu, downloading: %d", 
                        music->GetBufferSize(), music->is_downloading_.load());
            }
            
            // 确保停止当前歌曲播放，但不停止歌单
            ESP_LOGI(TAG, "[Playlist] Calling Stop(false) for song: %s", song.c_str());
            music->Stop(false);
            ESP_LOGI(TAG, "[Playlist] Stop(false) completed for song: %s", song.c_str());
            
            // 确保歌单播放状态被正确维护
            if (music->is_playing_.load() == false) {
                ESP_LOGW(TAG, "[Playlist] is_playing_ was reset to false, restoring playlist state");
                music->is_playing_ = true;
            }
            
            // 移动到下一首（在清理之前递增索引）
            ++music->current_playlist_index_;
            ESP_LOGI(TAG, "[Playlist] Moving to next song, index: %d/%d", 
                    music->current_playlist_index_, (int)music->playlist_.size());
            
            // 等待资源释放
            vTaskDelay(pdMS_TO_TICKS(1000));
    
            // 清理和重置资源，为下一首歌做准备
            ESP_LOGI(TAG, "[Playlist] Cleaning up for next song...");
            music->CleanupAndReset();
    
    // 等待TCP连接完全关闭
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 短暂延迟，确保清理完成
    vTaskDelay(pdMS_TO_TICKS(500));
            
            // 检查是否应该继续播放
            ESP_LOGI(TAG, "[Playlist] Checking is_playing_ status: %d", music->is_playing_.load());
            if (!music->is_playing_) {
                ESP_LOGI(TAG, "[Playlist] Playlist stopped by user");
                break;
            }
            
            // 在开始下一首歌之前，再次检查设备状态
            DeviceState pre_next_state = app.GetDeviceState();
            ESP_LOGI(TAG, "[Playlist] Pre-next song device state: %d", pre_next_state);
            
            // 只有在设备状态正常时才继续播放下一首歌
            if (pre_next_state == kDeviceStateIdle || pre_next_state == kDeviceStateSpeaking) {
                ESP_LOGI(TAG, "[Playlist] Device state OK, continuing to next song");
            } else {
                ESP_LOGW(TAG, "[Playlist] Device state not suitable for music playback: %d, stopping playlist", pre_next_state);
                break;
            }
        }
        
        ESP_LOGI(TAG, "[Playlist] Playlist finished or interrupted.");
        music->is_playing_ = false;
        vTaskDelete(nullptr);
    }, "playlist_task", 8192, this, 5, &playlist_task_handle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create playlist task: %d", ret);
        is_playing_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "Playlist task created successfully");
    return true;
}

// 新增：完整的资源清理和重置函数
void Esp32Music::CleanupAndReset() {
    ESP_LOGI(TAG, "Starting cleanup and reset for next song...");
    
    // 1. 清理音频缓冲区（使用现有的函数）
    ClearAudioBuffer();
    
    // 2. 重置下载和播放状态（但不重置歌单播放状态）
    is_downloading_ = false;
    // 注意：不重置 is_playing_，保持歌单播放状态
    
    // 3. 清理最后下载的数据
    last_downloaded_data_.clear();
    last_downloaded_data_.shrink_to_fit();  // 释放多余内存
    
    // 4. 清理歌词数据以节省内存
    {
        std::lock_guard<std::mutex> lock(lyrics_mutex_);
        lyrics_.clear();
        lyrics_.shrink_to_fit();
    }
    
    // 5. 强制垃圾回收，清理TCP连接
    ESP_LOGI(TAG, "Forcing garbage collection to clean up TCP connections...");
    heap_caps_check_integrity_all(true);
    
    // 6. 等待一小段时间确保资源释放
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "Cleanup and reset completed");
}

// 重置MP3解码器
void Esp32Music::ResetMP3Decoder() {
    ESP_LOGI(TAG, "Resetting MP3 decoder...");
    
    // 释放旧的解码器
    if (mp3_decoder_ != nullptr) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    
    // 重置解码器状态
    mp3_decoder_initialized_ = false;
    mp3_frame_info_ = {};
    
    ESP_LOGI(TAG, "MP3 decoder reset completed");
}

// 新增：紧急停止方法，用于唤醒打断
void Esp32Music::EmergencyStop() {
    ESP_LOGI(TAG, "Emergency stop triggered - clearing all music tasks");
    
    // 1. 立即设置停止标志
    is_playing_ = false;
    is_downloading_ = false;
    
    // 2. 通知所有等待的线程
    buffer_cv_.notify_all();
    
    // 3. 强制停止所有任务
    if (playlist_task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Force deleting playlist task");
        vTaskDelete(playlist_task_handle_);
        playlist_task_handle_ = nullptr;
    }
    
    if (lyric_task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Force deleting lyric task");
        is_lyric_running_ = false;
        if (eTaskGetState(lyric_task_handle_) != eDeleted) {
            vTaskDelete(lyric_task_handle_);
        }
        lyric_task_handle_ = nullptr;
    }
    
    // 4. 清理音频缓冲区
    ClearAudioBuffer();
    
    // 5. 重置MP3解码器
    CleanupMp3Decoder();
    
    // 6. 清理歌词数据
    {
        std::lock_guard<std::mutex> lock(lyrics_mutex_);
        lyrics_.clear();
        lyrics_.shrink_to_fit();
    }
    
    // 7. 重置播放状态
    current_playlist_index_ = 0;
    playlist_.clear();
    current_song_name_.clear();
    song_name_displayed_ = false;
    
    ESP_LOGI(TAG, "Emergency stop completed");
}

