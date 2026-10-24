#include "../native-common/Framing.hpp"
#include "../native-common/LocalSocket.hpp"

#include <cassert>
#include <csignal>
#include <iostream>
#include <sys/socket.h>

std::string frame(std::string_view payload) {
    const uint32_t length = static_cast<uint32_t>(payload.size());
    return std::string(reinterpret_cast<const char*>(&length), sizeof(length)) + std::string(payload);
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    const std::string payload = "{\"url\":\"https://example.com/中文\"}";
    const auto encoded = frame(payload);
    // Every possible two-piece split, including inside the uint32_t header.
    for (size_t split = 0; split <= encoded.size(); ++split) {
        argus::FrameDecoder decoder;
        std::vector<std::string> received;
        auto collect = [&](const std::string& value) { received.push_back(value); };
        assert(decoder.feed(encoded.data(), split, collect));
        assert(decoder.feed(encoded.data() + split, encoded.size() - split, collect));
        assert(decoder.complete());
        assert(received == std::vector<std::string>{payload});
    }
    {
        argus::FrameDecoder decoder;
        std::vector<std::string> received;
        const auto joined = encoded + frame("[]") + encoded;
        for (char byte : joined) assert(decoder.feed(&byte, 1, [&](const auto& value) { received.push_back(value); }));
        assert((received == std::vector<std::string>{payload, "[]", payload}));
        assert(decoder.complete());
    }
    for (size_t length = 1; length < encoded.size(); ++length) {
        argus::FrameDecoder decoder;
        assert(decoder.feed(encoded.data(), length, [](const auto&) { assert(false); }));
        assert(!decoder.complete());
    }
    for (const uint32_t size : { 0u, argus::kMaxMessageBytes + 1, UINT32_MAX }) {
        argus::FrameDecoder decoder;
        assert(!decoder.feed(reinterpret_cast<const char*>(&size), sizeof(size), [](const auto&) { assert(false); }));
        assert(!decoder.complete());
    }
    {
        argus::FrameDecoder decoder;
        const std::string large(argus::kMaxMessageBytes, 'a');
        const auto bytes = frame(large);
        size_t count = 0;
        assert(decoder.feed(bytes.data(), bytes.size(), [&](const auto& value) { assert(value == large); ++count; }));
        assert(count == 1 && decoder.complete());
    }
    {
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        argus::UniqueFd output(sockets[0]), input(sockets[1]);
        argus::nonblocking(output.get());
        argus::nonblocking(input.get());
        int small = 1024;
        assert(setsockopt(output.get(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
        argus::FrameWriter writer;
        const std::string large(512 * 1024, 'x');
        assert(writer.append(large));
        assert(writer.flush(output.get()));
        assert(writer.pending() > 0); // Real short write / EAGAIN, not a mock.
        std::string received;
        char buffer[8192];
        while (writer.pending() > 0 || received.size() < large.size() + sizeof(uint32_t)) {
            assert(writer.flush(output.get()));
            for (;;) {
                const auto size = read(input.get(), buffer, sizeof(buffer));
                if (size > 0) received.append(buffer, static_cast<size_t>(size));
                else { assert(size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)); break; }
            }
        }
        assert(received == frame(large));
        input = argus::UniqueFd();
        assert(writer.append(payload));
        assert(!writer.flush(output.get())); // Closed consumer is an error, not SIGPIPE death.
    }
    {
        argus::FrameWriter writer;
        const std::string large(argus::kMaxMessageBytes, 'a');
        assert(!writer.append(""));
        assert(!writer.append(large + "x"));
        for (int i = 0; i < 4; ++i) assert(writer.append(large));
        assert(!writer.append("overflow"));
    }
    std::cout << "PASS: framing splits, concatenation, bounds, EOF, short writes, backpressure, EPIPE\n";
}
