//
//  main.cpp
//  ArgusMacExtensionHelper
//
//  Created by Chuanren Shang on 2022/11/29.
//

#include <iostream>
#include <cstdio>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CFRunLoop.h>
#include <nlohmann/json.hpp>

static const char *socket_path = "/tmp/com.argus.channel";
static int client_socket = 0;

void sendMessageToExtension(nlohmann::json content) {
    std::string encoded_content = content.dump();
    uint32_t size = (uint32_t)encoded_content.length();
    char* length = reinterpret_cast<char*>(&size);
    fwrite(length, 4, sizeof(char), stdout);
    fwrite(encoded_content.c_str(), encoded_content.length(), sizeof(char), stdout);
    fflush(stdout);
}

void connectNativeService() {
    if ((client_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        std::cerr << "socket failed\n";
        return;
    }

    struct sockaddr_un remote;
    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, socket_path);
    if (connect(client_socket, (struct sockaddr *)&remote, sizeof(remote)) == -1) {
        std::cerr << "connect failed\n";
        return;
    }

    // cerr for debug for native messaging's host
    std::cerr << "connected!\n";

    dispatch_source_t sockSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, client_socket, 0, dispatch_get_main_queue());

    dispatch_source_set_event_handler(sockSource, ^{
        char raw_length[4];
        memset(raw_length, 0, 4);
        size_t recv_len = recv(client_socket, raw_length, 4, 0);
        if (recv_len <= 0) {
            dispatch_cancel(sockSource);
            return;
        }
        uint32_t message_length = *reinterpret_cast<uint32_t*>(raw_length);
        if (!message_length) {
            exit(EXIT_SUCCESS);
        };

        char message[message_length];
        recv_len = recv(client_socket, message, message_length, 0);
        if (recv_len <= 0) {
            dispatch_cancel(sockSource);
            return;
        }
        std::string m(message, message + sizeof(message) / sizeof(message[0]));
        sendMessageToExtension(m);
    });
    dispatch_source_set_cancel_handler(sockSource, ^{
        std::cerr << "server closed" << "\n";
        close(client_socket);
        dispatch_release(sockSource);
    });
    dispatch_resume(sockSource);
}

int main(int argc, const char * argv[]) {
    connectNativeService();

    dispatch_source_t input = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, STDIN_FILENO, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(input, ^{
        int length_size = 4;
        char raw_length_buffer[length_size];
        memset(raw_length_buffer, 0, length_size);
        fread(raw_length_buffer, length_size, sizeof(char), stdin);
        uint32_t message_length = *reinterpret_cast<uint32_t*>(raw_length_buffer);
        if (!message_length) {
            exit(EXIT_SUCCESS);
        };

        int buffer_size = length_size + message_length;
        char message[buffer_size];
        memcpy(message, raw_length_buffer, length_size);
        fread(message + length_size, message_length, sizeof(char), stdin);

        send(client_socket, message, buffer_size, 0);
    });
    dispatch_resume(input);

    CFRunLoopRun();

    return 0;
}
