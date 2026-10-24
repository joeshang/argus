#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace argus {

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) : m_fd(fd) {}
    ~UniqueFd() { if (m_fd >= 0) close(m_fd); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : m_fd(std::exchange(other.m_fd, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (m_fd >= 0) close(m_fd);
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }
    int get() const { return m_fd; }
private:
    int m_fd;
};

inline void nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        throw std::runtime_error("Cannot configure nonblocking descriptor");
    }
}

inline void noSigPipe(int fd) {
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        throw std::runtime_error("Cannot configure SO_NOSIGPIPE");
    }
}

inline bool sameUser(int fd) {
    uid_t uid;
    gid_t gid;
    return getpeereid(fd, &uid, &gid) == 0 && uid == geteuid();
}

struct LocalEndpoint {
    UniqueFd directory;
    std::string directoryPath;
    std::string path;
    sockaddr_un address {};

    static LocalEndpoint open() {
        const char* overridePath = std::getenv("ARGUS_RUNTIME_DIR");
        std::string directory = overridePath ? overridePath : "/tmp/argus-" + std::to_string(geteuid());
        if (directory.empty() || directory.front() != '/') {
            throw std::runtime_error("ARGUS_RUNTIME_DIR must be an absolute private directory");
        }
        const std::string path = directory + "/channel.sock";
        sockaddr_un address {};
        if (path.size() >= sizeof(address.sun_path)) throw std::runtime_error("Unix socket path is too long");
        if (mkdir(directory.c_str(), 0700) < 0 && errno != EEXIST) {
            throw std::runtime_error("Cannot create Argus runtime directory");
        }
        UniqueFd fd(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        struct stat info {};
        if (fd.get() < 0 || fstat(fd.get(), &info) < 0 || !S_ISDIR(info.st_mode) ||
            info.st_uid != geteuid() || (info.st_mode & 0777) != 0700) {
            throw std::runtime_error("Argus runtime directory must be owned by this user with mode 0700 (no symlinks)");
        }
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        address.sun_len = sizeof(address);
        return { std::move(fd), directory, path, address };
    }
};

} // namespace argus
