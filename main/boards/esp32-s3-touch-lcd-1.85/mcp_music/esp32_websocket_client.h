#ifndef ESP32_WEBSOCKET_CLIENT_H
#define ESP32_WEBSOCKET_CLIENT_H

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

// ESP32 WebSocket客户端
class ESP32WebSocketClient {
public:
    // 消息类型定义
    enum class MessageType {
        CONTROL,
        AUDIO_CHUNK,
        STATUS,
        ERROR,
        READY
    };

    // 控制命令类型
    enum class ControlCommand {
        START,
        PAUSE,
        PLAY,
        STOP,
        COMPLETE,
        ERROR
    };

    // 消息结构
    struct WebSocketMessage {
        MessageType type;
        ControlCommand command;
        std::string data;
        int chunk_id;
        size_t size;
        std::string song_name;
        std::string error_msg;
    };

    // 回调函数类型
    using MessageCallback = std::function<void(const WebSocketMessage&)>;
    using ConnectionCallback = std::function<void(bool)>;

private:
    std::string server_url_;
    std::atomic<bool> is_connected_;
    std::atomic<bool> should_reconnect_;
    std::thread connection_thread_;
    std::thread message_thread_;
    
    // 消息队列
    std::queue<WebSocketMessage> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 回调函数
    MessageCallback message_callback_;
    ConnectionCallback connection_callback_;
    
    // 重连参数
    int reconnect_interval_ms_;
    int max_reconnect_attempts_;
    int current_reconnect_attempts_;

public:
    ESP32WebSocketClient(const std::string& server_url = "ws://192.168.1.100:8081");
    ~ESP32WebSocketClient();

    // 连接管理
    bool Connect();
    void Disconnect();
    bool IsConnected() const { return is_connected_.load(); }
    
    // 设置回调函数
    void SetMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    void SetConnectionCallback(ConnectionCallback callback) { connection_callback_ = callback; }
    
    // 发送消息
    bool SendReadyMessage();
    bool SendStatusMessage(const std::string& status);
    bool SendErrorMessage(const std::string& error);

private:
    // 内部方法
    void ConnectionThread();
    void MessageThread();
    void HandleIncomingMessage(const std::string& message);
    bool ParseMessage(const std::string& json_str, WebSocketMessage& msg);
    void ProcessMessageQueue();
    
    // WebSocket底层实现（需要根据ESP32的WebSocket库实现）
    bool WebSocketConnect();
    void WebSocketDisconnect();
    bool WebSocketSend(const std::string& message);
    std::string WebSocketReceive();
    bool WebSocketIsConnected();
};

#endif // ESP32_WEBSOCKET_CLIENT_H 