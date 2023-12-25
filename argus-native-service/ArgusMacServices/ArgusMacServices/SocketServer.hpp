//
//  SocketServer.hpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/12/4.
//

#ifndef SocketServer_hpp
#define SocketServer_hpp

#include <cstdio>
#include <string>
#include <unistd.h>
#include <dispatch/dispatch.h>

class BrowserMonitor;

class SocketServer {
public:
    SocketServer(std::function<void(std::string &)> handle): m_handleMessage(handle) {}
    ~SocketServer() {
        if (m_socket != -1) {
            close(m_socket);
        }
        if (m_acceptSource) {
            dispatch_release(m_acceptSource);
        }
    }

    void setup();
    void sendMessage(std::string &message, int fd);

private:
    void receiveClientData(int fd);

    int m_socket = -1;
    dispatch_source_t m_acceptSource = NULL;
    std::function<void(std::string &)> m_handleMessage;
};

#endif /* SocketServer_hpp */
