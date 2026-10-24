#pragma once

#include <functional>
#include <memory>
#include <string>

// Owns a single I/O thread. Destruction joins it before releasing descriptors or
// the callback, so no event can reach a destroyed monitor.
class SocketServer {
public:
    explicit SocketServer(std::function<void(const std::string&)> handle);
    ~SocketServer();
    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;
    bool setup();

private:
    struct State;
    std::unique_ptr<State> m_state;
};
