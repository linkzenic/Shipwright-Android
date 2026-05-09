#ifndef HARPOON_WEBSOCKET_H
#define HARPOON_WEBSOCKET_H
#ifdef __cplusplus

// =============================================================================
// HarpoonWebSocket — WebSocket client (RFC 6455 plain ws://) used ONLY by Harpoon.
// =============================================================================
//
// The base `Network` class still does raw TCP + \0-delimited JSON for Anchor,
// Sail, and CrowdControl. Harpoon needs a real WebSocket transport to talk to
// the Python server (which uses the `websockets` library and rejects raw TCP).
// We implement the WS protocol here without adding any external dependency
// — only SDL_net for the underlying socket plus <random> for the masking key
// and Sec-WebSocket-Key generation.
//
// For TLS (wss://), front the server with a reverse proxy (Caddy / Nginx /
// AWS ALB) that terminates TLS — this client always speaks plain ws://.
// =============================================================================

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>
#include <queue>
#ifdef ENABLE_REMOTE_CONTROL
#include <SDL2/SDL_net.h>
#endif

class HarpoonWebSocket {
  public:
    using TextHandler        = std::function<void(const std::string&)>;
    using ConnectHandler     = std::function<void()>;
    using DisconnectHandler  = std::function<void()>;

    HarpoonWebSocket();
    ~HarpoonWebSocket();

    // Lifecycle.
    void Connect(const std::string& host, uint16_t port);
    void Disconnect();

    // Send a UTF-8 text frame. Thread-safe; called from any thread.
    void SendText(const std::string& payload);

    // Callbacks (set once before Connect).
    void SetOnText(TextHandler h)            { onText_ = std::move(h); }
    void SetOnConnected(ConnectHandler h)    { onConnected_ = std::move(h); }
    void SetOnDisconnected(DisconnectHandler h) { onDisconnected_ = std::move(h); }

    bool IsEnabled()   const { return enabled_.load(); }
    bool IsConnected() const { return connectedAndHandshakeDone_.load(); }

  private:
#ifdef ENABLE_REMOTE_CONTROL
    IPaddress address_{};
    TCPsocket socket_ = nullptr;
#endif
    std::string host_;
    uint16_t port_ = 0;

    std::thread thread_;
    std::atomic<bool> enabled_{ false };
    std::atomic<bool> connectedAndHandshakeDone_{ false };

    // Pending outgoing frames (raw bytes — already framed). Drained by the
    // worker thread and pushed into the socket. Protects against partial
    // sends from multiple game-thread callers.
    std::mutex outMutex_;
    std::queue<std::string> outQueue_;

    // Receive accumulator. Bytes from TCP arrive here; we parse WS frames
    // out of it. `textAccum_` joins continuation frames until FIN=1.
    std::string rxBuffer_;
    std::string textAccum_;

    TextHandler        onText_;
    ConnectHandler     onConnected_;
    DisconnectHandler  onDisconnected_;

    // Worker.
    void RunLoop();
    bool PerformHandshake();
    void ProcessOutbound();
    void ProcessInboundFrames();
    void Cleanup();
};

#endif // __cplusplus
#endif // HARPOON_WEBSOCKET_H
