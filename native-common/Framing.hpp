#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

namespace argus {

// Native Messaging uses a uint32_t in the machine's native byte order. A 1 MiB
// cap also respects Chrome's host-to-browser limit. No allocation trusts a header.
inline constexpr uint32_t kMaxMessageBytes = 1024 * 1024;
inline constexpr size_t kMaxPendingBytes = 4 * (kMaxMessageBytes + sizeof(uint32_t));

class FrameDecoder {
public:
    template <typename Handler>
    bool feed(const char* data, size_t size, Handler&& handle) {
        if (m_failed) return false;
        while (size > 0) {
            if (m_headerUsed < m_header.size()) {
                const auto count = std::min(size, m_header.size() - m_headerUsed);
                std::memcpy(m_header.data() + m_headerUsed, data, count);
                m_headerUsed += count;
                data += count;
                size -= count;
                if (m_headerUsed != m_header.size()) continue;
                std::memcpy(&m_length, m_header.data(), sizeof(m_length));
                if (m_length == 0 || m_length > kMaxMessageBytes) {
                    m_failed = true;
                    return false;
                }
                m_body.reserve(m_length);
            }
            const auto count = std::min(size, size_t(m_length) - m_body.size());
            m_body.append(data, count);
            data += count;
            size -= count;
            if (m_body.size() == m_length) {
                handle(m_body);
                m_body.clear();
                m_headerUsed = 0;
                m_length = 0;
            }
        }
        return true;
    }

    // EOF is only clean at a frame boundary.
    bool complete() const { return !m_failed && m_headerUsed == 0; }

private:
    std::array<char, sizeof(uint32_t)> m_header {};
    size_t m_headerUsed = 0;
    uint32_t m_length = 0;
    bool m_failed = false;
    std::string m_body;
};

class FrameWriter {
public:
    bool append(std::string_view message) {
        if (message.empty() || message.size() > kMaxMessageBytes ||
            pending() + sizeof(uint32_t) + message.size() > kMaxPendingBytes) return false;
        if (m_offset != 0) {
            m_bytes.erase(m_bytes.begin(), m_bytes.begin() + m_offset);
            m_offset = 0;
        }
        const uint32_t size = static_cast<uint32_t>(message.size());
        const auto* length = reinterpret_cast<const char*>(&size);
        m_bytes.insert(m_bytes.end(), length, length + sizeof(size));
        m_bytes.insert(m_bytes.end(), message.begin(), message.end());
        return true;
    }

    // Nonblocking fd: retain the unwritten suffix on EAGAIN; never spin or lose
    // bytes on partial writes. Both sockets and browser stdout use this path.
    bool flush(int fd) {
        while (pending() > 0) {
            const auto count = write(fd, m_bytes.data() + m_offset, pending());
            if (count > 0) m_offset += static_cast<size_t>(count);
            else if (count < 0 && errno == EINTR) continue;
            else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
            else return false;
        }
        m_bytes.clear();
        m_offset = 0;
        return true;
    }

    size_t pending() const { return m_bytes.size() - m_offset; }

private:
    std::vector<char> m_bytes;
    size_t m_offset = 0;
};

} // namespace argus
