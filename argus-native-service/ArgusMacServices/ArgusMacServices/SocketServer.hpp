#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <string>

// Owns a single I/O thread. Destruction joins it before releasing descriptors or
// the callback, so no event can reach a destroyed monitor.
class SocketServer {
public:
    using MessageHandler = std::function<void(uint64_t, const std::string&)>;
    using CloseHandler = std::function<void(uint64_t)>;
    explicit SocketServer(MessageHandler handle, CloseHandler closed = {});
    ~SocketServer();
    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;
    bool setup();

private:
    struct State;
    std::unique_ptr<State> m_state;
};
