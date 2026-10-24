#include "../../../native-common/Framing.hpp"
#include "../../../native-common/LocalSocket.hpp"

#include <csignal>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <poll.h>

namespace {

int bridge(int fd) {
    argus::FrameDecoder browserInput, serviceInput;
    argus::FrameWriter toService, toBrowser;
    bool browserClosed = false;
    std::chrono::steady_clock::time_point drainDeadline;
    char buffer[8192];
    for (;;) {
        pollfd descriptors[] = {
            { browserClosed ? -1 : STDIN_FILENO, POLLIN, 0 },
            { fd, short(POLLIN | (toService.pending() ? POLLOUT : 0)), 0 },
            { STDOUT_FILENO, short(toBrowser.pending() ? POLLOUT : 0), 0 },
        };
        const auto timeout = browserClosed ? std::chrono::duration_cast<std::chrono::milliseconds>(
            drainDeadline - std::chrono::steady_clock::now()).count() : -1;
        if (browserClosed && timeout <= 0) return EXIT_FAILURE;
        const int result = poll(descriptors, 3, static_cast<int>(timeout));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return EXIT_FAILURE;
        for (int source = 0; source < 2; ++source) {
            if (!(descriptors[source].revents & (POLLIN | POLLHUP))) continue;
            auto& decoder = source == 0 ? browserInput : serviceInput;
            const auto count = read(descriptors[source].fd, buffer, sizeof(buffer));
            if (count > 0) {
                bool queued = true;
                const bool valid = decoder.feed(buffer, static_cast<size_t>(count), [&](const std::string& message) {
                    if (source == 0) queued = queued && toService.append(message);
                    else queued = queued && toBrowser.append(nlohmann::json(message).dump());
                });
                if (!valid || !queued) {
                    std::cerr << "Argus Helper: invalid frame or slow consumer\n";
                    return EXIT_FAILURE;
                }
            } else if (count == 0) {
                if (!decoder.complete()) {
                    std::cerr << "Argus Helper: truncated frame at EOF\n";
                    return EXIT_FAILURE;
                }
                if (source == 1) {
                    std::cerr << "Argus Helper: Native Service disconnected\n";
                    return EXIT_FAILURE; // Close stdio so Chrome can reconnect.
                }
                browserClosed = true;
                drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return EXIT_FAILURE;
        }
        if (!toService.flush(fd) || !toBrowser.flush(STDOUT_FILENO)) return EXIT_FAILURE;
        if (browserClosed && toService.pending() == 0) return EXIT_SUCCESS;
        if (descriptors[2].revents & POLLHUP) return EXIT_FAILURE;
        for (const auto& descriptor : descriptors) {
            if (descriptor.revents & (POLLERR | POLLNVAL)) return EXIT_FAILURE;
        }
    }
}

} // namespace

int main() {
    // SO_NOSIGPIPE covers the UDS; ignoring SIGPIPE also handles a closed browser
    // stdout pipe. Errors propagate through flush(), producing an ordinary exit.
    std::signal(SIGPIPE, SIG_IGN);
    try {
        auto endpoint = argus::LocalEndpoint::open();
        argus::UniqueFd fd(socket(AF_UNIX, SOCK_STREAM, 0));
        if (fd.get() < 0) throw std::runtime_error("Cannot create socket");
        argus::noSigPipe(fd.get());
        argus::nonblocking(fd.get());
        if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&endpoint.address), sizeof(sockaddr_un)) < 0) {
            if (errno != EINPROGRESS) throw std::runtime_error("Cannot connect to Native Service");
            pollfd connecting { fd.get(), POLLOUT, 0 };
            int result;
            do { result = poll(&connecting, 1, 2000); } while (result < 0 && errno == EINTR);
            int error = 0;
            socklen_t length = sizeof(error);
            if (result <= 0 || getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error != 0) {
                throw std::runtime_error("Cannot connect to Native Service");
            }
        }
        if (!argus::sameUser(fd.get())) throw std::runtime_error("Native Service belongs to a different user");
        argus::nonblocking(STDIN_FILENO);
        argus::nonblocking(STDOUT_FILENO);
        return bridge(fd.get());
    } catch (const std::exception& error) {
        std::cerr << "Argus Helper: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
