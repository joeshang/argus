//
//  SocketServer.cpp
//  ArgusMacServices
//
//  Created by Chuanren Shang on 2022/12/4.
//

#include "SocketServer.hpp"
#include <cstdio>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <CoreFoundation/CFRunLoop.h>

static const char *socket_path = "/tmp/com.argus.channel";

void SocketServer::setup() {
    m_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_socket == -1) {
        std::cerr << "socket failed\n";
        return;
    }

    std::cout << "bind\n";
    // delete existed socket's file, otherwise bind will failed
    unlink(socket_path);
    // make unix domain socket's file writable by non root program
    int mask = umask(777);
    struct sockaddr_un local;
    memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    strncpy(local.sun_path, socket_path, sizeof(local.sun_path));
    if (bind(m_socket, (struct sockaddr *)&local, sizeof(local)) != 0) {
        std::cerr << "bind failed\n";
        return;
    }
    umask(mask);

    std::cout << "listen\n";
    if (listen(m_socket, 10) != 0) {
        std::cerr << "listen failed\n";
        return;
    }

    (void)fcntl(m_socket, F_SETFL, O_NONBLOCK);

    std::cout << "wait for connection...\n";
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    m_acceptSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, m_socket, 0, queue);
    dispatch_source_set_event_handler(m_acceptSource, ^{
        unsigned int sock_len = 0;
        struct sockaddr_un remote;
        memset(&remote, 0, sizeof(remote));
        int acceptFd = accept(m_socket, (struct sockaddr*)&remote, &sock_len);
        if (acceptFd == -1) {
            std::cerr << "accept failed\n";
            dispatch_source_cancel(m_acceptSource);
        }
        std::cout << "new connection: " << acceptFd << "\n";
        receiveClientData(acceptFd);
        std::string m = "native service connected";
        sendMessage(m, acceptFd);
    });

    dispatch_source_set_cancel_handler(m_acceptSource, ^{
        close(m_socket);
        dispatch_release(m_acceptSource);
        m_acceptSource = NULL;
    });

    dispatch_resume(m_acceptSource);
}


void SocketServer::receiveClientData(int fd) {
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    dispatch_source_t recvSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, fd, 0, queue);

    dispatch_source_set_event_handler(recvSource, ^{
        char raw_length[4];
        memset(raw_length, 0, 4);
        size_t recv_len = recv(fd, raw_length, 4, 0);
        if (recv_len <= 0) {
            dispatch_cancel(recvSource);
            return;
        }
        uint32_t message_length = *reinterpret_cast<uint32_t*>(raw_length);
//        std::cout << "message length: " << message_length << ", raw: " << raw_length << "\n";
        if (!message_length) {
            exit(EXIT_SUCCESS);
        };

        char message[message_length];
        recv_len = recv(fd, message, message_length, 0);
        if (recv_len <= 0) {
            dispatch_cancel(recvSource);
            return;
        }
        std::string m(message, message + sizeof(message) / sizeof(message[0]));
        m_handleMessage(m);
    });
    dispatch_source_set_cancel_handler(recvSource, ^{
        std::cout << "lost connection: " << fd << "\n";
        close(fd);
        dispatch_release(recvSource);
    });
    dispatch_resume(recvSource);
}

void SocketServer::sendMessage(std::string &message, int fd) {
    auto size = uint32_t(message.size());
    char* length = reinterpret_cast<char*>(&size);
    send(fd, length, 4, 0);
    send(fd, message.c_str(), message.length(), 0);
}

