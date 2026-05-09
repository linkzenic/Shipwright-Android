#include "HarpoonWebSocket.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <random>
#include <cstring>
#include <cstdint>

// =============================================================================
// Helpers (file-local). RFC 6455 client-side.
// =============================================================================

namespace {

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= (uint32_t)data[i + 2];
        out.push_back(kB64[(v >> 18) & 0x3F]);
        out.push_back(kB64[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? kB64[(v >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? kB64[v & 0x3F] : '=');
    }
    return out;
}

static std::string MakeWebSocketKey() {
    uint8_t buf[16];
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : buf) b = (uint8_t)dist(gen);
    return Base64Encode(buf, sizeof(buf));
}

// Encode a single text frame (FIN=1, opcode=1, masked) into `out`.
static void EncodeTextFrame(const std::string& payload, std::string& out) {
    out.push_back((char)0x81);  // FIN | text
    size_t len = payload.size();
    uint8_t mask[4];
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : mask) b = (uint8_t)dist(gen);
    }
    if (len < 126) {
        out.push_back((char)(0x80 | len));
    } else if (len <= 0xFFFF) {
        out.push_back((char)(0x80 | 126));
        out.push_back((char)((len >> 8) & 0xFF));
        out.push_back((char)(len & 0xFF));
    } else {
        out.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; --i) out.push_back((char)((len >> (i * 8)) & 0xFF));
    }
    out.append((const char*)mask, 4);
    size_t maskOff = out.size() - 4;
    size_t bodyOff = out.size();
    out.append(payload);
    for (size_t i = 0; i < len; ++i) {
        out[bodyOff + i] ^= out[maskOff + (i & 3)];
    }
}

static void EncodeCloseFrame(std::string& out) {
    out.push_back((char)0x88);
    out.push_back((char)0x80);
    uint8_t mask[4] = { 0, 0, 0, 0 };
    out.append((const char*)mask, 4);
}

// Encode a pong frame mirroring the ping payload.
static void EncodePongFrame(const std::string& pingPayload, std::string& out) {
    out.push_back((char)0x8A);
    size_t len = pingPayload.size();
    uint8_t mask[4];
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : mask) b = (uint8_t)dist(gen);
    }
    if (len < 126) {
        out.push_back((char)(0x80 | len));
    } else {
        out.push_back((char)(0x80 | 126));
        out.push_back((char)((len >> 8) & 0xFF));
        out.push_back((char)(len & 0xFF));
    }
    out.append((const char*)mask, 4);
    size_t bodyOff = out.size();
    out.append(pingPayload);
    for (size_t i = 0; i < len; ++i) {
        out[bodyOff + i] ^= mask[i & 3];
    }
}

enum class FrameOp { CONTINUATION = 0, TEXT = 1, BINARY = 2,
                     CLOSE = 8, PING = 9, PONG = 10, INCOMPLETE = 0xFF };

struct ParsedFrame {
    FrameOp op = FrameOp::INCOMPLETE;
    bool fin = false;
    size_t consumed = 0;
    std::string payload;
};

static ParsedFrame TryParseFrame(const std::string& buf) {
    ParsedFrame f;
    if (buf.size() < 2) return f;
    uint8_t b0 = (uint8_t)buf[0];
    uint8_t b1 = (uint8_t)buf[1];
    f.fin = (b0 & 0x80) != 0;
    uint8_t opcode = b0 & 0x0F;
    f.op = (FrameOp)opcode;
    bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t off = 2;
    if (len == 126) {
        if (buf.size() < off + 2) { f.op = FrameOp::INCOMPLETE; return f; }
        len = ((uint64_t)(uint8_t)buf[off] << 8) | (uint8_t)buf[off + 1];
        off += 2;
    } else if (len == 127) {
        if (buf.size() < off + 8) { f.op = FrameOp::INCOMPLETE; return f; }
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | (uint8_t)buf[off + i];
        off += 8;
    }
    uint8_t maskKey[4] = {};
    if (masked) {
        if (buf.size() < off + 4) { f.op = FrameOp::INCOMPLETE; return f; }
        for (int i = 0; i < 4; ++i) maskKey[i] = (uint8_t)buf[off + i];
        off += 4;
    }
    if (buf.size() < off + len) { f.op = FrameOp::INCOMPLETE; return f; }
    f.payload.assign(buf, off, (size_t)len);
    if (masked) {
        for (size_t i = 0; i < f.payload.size(); ++i) {
            f.payload[i] ^= maskKey[i & 3];
        }
    }
    f.consumed = off + (size_t)len;
    return f;
}

}  // anonymous namespace

// =============================================================================
// HarpoonWebSocket implementation
// =============================================================================

HarpoonWebSocket::HarpoonWebSocket() = default;

HarpoonWebSocket::~HarpoonWebSocket() {
    Disconnect();
}

void HarpoonWebSocket::Connect(const std::string& host, uint16_t port) {
#ifdef ENABLE_REMOTE_CONTROL
    if (enabled_.load()) {
        return;
    }
    host_ = host;
    port_ = port;

    if (SDLNet_ResolveHost(&address_, host.c_str(), port) == -1) {
        SPDLOG_ERROR("[HarpoonWS] SDLNet_ResolveHost failed: {}", SDLNet_GetError());
        return;
    }

    enabled_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    thread_ = std::thread(&HarpoonWebSocket::RunLoop, this);
#endif
}

void HarpoonWebSocket::Disconnect() {
    if (!enabled_.load()) {
        return;
    }
    enabled_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HarpoonWebSocket::SendText(const std::string& payload) {
    if (!connectedAndHandshakeDone_.load()) {
        return;
    }
    std::string frame;
    EncodeTextFrame(payload, frame);
    {
        std::lock_guard<std::mutex> lk(outMutex_);
        outQueue_.push(std::move(frame));
    }
}

// -----------------------------------------------------------------------------

void HarpoonWebSocket::RunLoop() {
#ifdef ENABLE_REMOTE_CONTROL
    while (enabled_.load()) {
        // Connection attempt loop.
        while (enabled_.load() && !socket_) {
            SPDLOG_TRACE("[HarpoonWS] Connecting to {}:{}...", host_, port_);
            socket_ = SDLNet_TCP_Open(&address_);
            if (socket_) {
                rxBuffer_.clear();
                textAccum_.clear();
                connectedAndHandshakeDone_.store(false);
                if (!PerformHandshake()) {
                    SDLNet_TCP_Close(socket_);
                    socket_ = nullptr;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                connectedAndHandshakeDone_.store(true);
                SPDLOG_INFO("[HarpoonWS] WebSocket connected to {}:{}", host_, port_);
                if (onConnected_) onConnected_();
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Frame I/O loop.
        SDLNet_SocketSet socketSet = SDLNet_AllocSocketSet(1);
        if (socket_) {
            SDLNet_TCP_AddSocket(socketSet, socket_);
        }

        while (enabled_.load() && socket_ && connectedAndHandshakeDone_.load()) {
            ProcessOutbound();

            int ready = SDLNet_CheckSockets(socketSet, 0);
            if (ready == -1) {
                SPDLOG_ERROR("[HarpoonWS] CheckSockets: {}", SDLNet_GetError());
                break;
            }
            if (ready == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            char buf[4096];
            int n = SDLNet_TCP_Recv(socket_, buf, (int)sizeof(buf));
            if (n <= 0) {
                SPDLOG_INFO("[HarpoonWS] TCP recv ended (n={})", n);
                break;
            }
            rxBuffer_.append(buf, n);
            ProcessInboundFrames();
        }

        if (socketSet) {
            SDLNet_FreeSocketSet(socketSet);
        }

        Cleanup();
    }
#endif
}

bool HarpoonWebSocket::PerformHandshake() {
#ifdef ENABLE_REMOTE_CONTROL
    if (!socket_) return false;
    std::string key = MakeWebSocketKey();
    std::string req;
    req += "GET / HTTP/1.1\r\n";
    req += "Host: " + host_ + ":" + std::to_string(port_) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "User-Agent: Harpoon-SoH/1.0\r\n";
    req += "\r\n";

    int sent = SDLNet_TCP_Send(socket_, req.data(), (int)req.size());
    if (sent < (int)req.size()) {
        SPDLOG_ERROR("[HarpoonWS] handshake send truncated");
        return false;
    }

    // Read response until \r\n\r\n.
    std::string headers;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    char buf[1024];
    while (headers.find("\r\n\r\n") == std::string::npos) {
        if (!enabled_.load() || std::chrono::steady_clock::now() > deadline) {
            SPDLOG_ERROR("[HarpoonWS] handshake timeout");
            return false;
        }
        int n = SDLNet_TCP_Recv(socket_, buf, (int)sizeof(buf));
        if (n <= 0) {
            SPDLOG_ERROR("[HarpoonWS] handshake recv failed");
            return false;
        }
        headers.append(buf, n);
        if (headers.size() > 16384) {
            SPDLOG_ERROR("[HarpoonWS] handshake response too large");
            return false;
        }
    }

    // We trust our own server — only check for HTTP 101.
    if (headers.find(" 101 ") == std::string::npos) {
        SPDLOG_ERROR("[HarpoonWS] handshake bad status: {}", headers.substr(0, 64));
        return false;
    }

    size_t end = headers.find("\r\n\r\n") + 4;
    if (end < headers.size()) {
        rxBuffer_.append(headers, end, std::string::npos);
    }
    SPDLOG_INFO("[HarpoonWS] handshake OK");
    return true;
#else
    return false;
#endif
}

void HarpoonWebSocket::ProcessOutbound() {
#ifdef ENABLE_REMOTE_CONTROL
    if (!socket_) return;
    std::queue<std::string> drained;
    {
        std::lock_guard<std::mutex> lk(outMutex_);
        std::swap(drained, outQueue_);
    }
    while (!drained.empty()) {
        const std::string& f = drained.front();
        int sent = SDLNet_TCP_Send(socket_, f.data(), (int)f.size());
        if (sent < (int)f.size()) {
            SPDLOG_ERROR("[HarpoonWS] frame send truncated");
            // Push remaining back? simpler: drop. Outgoing rate is small.
            return;
        }
        drained.pop();
    }
#endif
}

void HarpoonWebSocket::ProcessInboundFrames() {
#ifdef ENABLE_REMOTE_CONTROL
    while (true) {
        ParsedFrame f = TryParseFrame(rxBuffer_);
        if (f.op == FrameOp::INCOMPLETE) break;
        rxBuffer_.erase(0, f.consumed);

        switch (f.op) {
            case FrameOp::TEXT:
            case FrameOp::CONTINUATION:
                textAccum_.append(f.payload);
                if (f.fin) {
                    if (onText_) onText_(textAccum_);
                    textAccum_.clear();
                }
                break;
            case FrameOp::PING: {
                std::string pong;
                EncodePongFrame(f.payload, pong);
                std::lock_guard<std::mutex> lk(outMutex_);
                outQueue_.push(std::move(pong));
                break;
            }
            case FrameOp::CLOSE: {
                SPDLOG_INFO("[HarpoonWS] server closed");
                std::string close;
                EncodeCloseFrame(close);
                if (socket_) {
                    SDLNet_TCP_Send(socket_, close.data(), (int)close.size());
                }
                connectedAndHandshakeDone_.store(false);
                return;
            }
            default:
                // PONG / BINARY / unknown — ignore.
                break;
        }
    }
#endif
}

void HarpoonWebSocket::Cleanup() {
#ifdef ENABLE_REMOTE_CONTROL
    if (socket_) {
        if (connectedAndHandshakeDone_.load()) {
            std::string close;
            EncodeCloseFrame(close);
            SDLNet_TCP_Send(socket_, close.data(), (int)close.size());
        }
        SDLNet_TCP_Close(socket_);
        socket_ = nullptr;
    }
    bool wasConnected = connectedAndHandshakeDone_.exchange(false);
    rxBuffer_.clear();
    textAccum_.clear();
    if (wasConnected && onDisconnected_) {
        onDisconnected_();
    }
#endif
}
