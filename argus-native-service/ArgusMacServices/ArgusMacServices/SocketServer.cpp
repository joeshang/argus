#include "SocketServer.hpp"
#include "../../../native-common/Framing.hpp"
#include "../../../native-common/LocalSocket.hpp"

#include <atomic>
#include <iostream>
#include <poll.h>
#include <sys/file.h>
#include <thread>
#include <vector>

namespace {
constexpr size_t kMaxClients = 64;

struct Client {
    uint64_t id;
    argus::UniqueFd fd;
    argus::FrameDecoder decoder;
    argus::FrameWriter writer;
};
}

struct SocketServer::State {
    State(MessageHandler callback, CloseHandler onClose) : handle(std::move(callback)), closed(std::move(onClose)) {}
    MessageHandler handle;
    CloseHandler closed;
    uint64_t nextId = 1;
    std::unique_ptr<argus::LocalEndpoint> endpoint;
    argus::UniqueFd lock;
    argus::UniqueFd listener;
    std::vector<Client> clients;
    std::atomic<bool> running { false };
    std::thread thread;
    dev_t device = 0;
    ino_t inode = 0;

    ~State() {
        running = false;
        if (thread.joinable()) thread.join();
        for (const auto& client : clients) notifyClosed(client.id);
        clients.clear();
        listener = argus::UniqueFd();
        // Keep the lock file inode stable across runs. Only remove our socket
        // while still holding the exclusive service lock.
        struct stat info {};
        if (endpoint && inode != 0 && fstatat(endpoint->directory.get(), "channel.sock", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
            info.st_ino == inode && info.st_dev == device) {
            unlinkat(endpoint->directory.get(), "channel.sock", 0);
        }
    }

    void notifyClosed(uint64_t id) noexcept {
        try { if (closed) closed(id); } catch (...) {}
    }

    void acceptClients() {
        while (running) {
            argus::UniqueFd fd(accept(listener.get(), nullptr, nullptr));
            if (fd.get() < 0) {
                if (errno == EINTR || errno == ECONNABORTED) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) std::cerr << "Argus accept failed: " << strerror(errno) << '\n';
                return;
            }
            if (clients.size() >= kMaxClients || !argus::sameUser(fd.get())) continue;
            try {
                argus::nonblocking(fd.get());
                argus::noSigPipe(fd.get());
                Client client { nextId++, std::move(fd), {}, {} };
                client.writer.append("native service connected");
                clients.push_back(std::move(client));
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
            }
        }
    }

    bool readClient(Client& client) {
        char buffer[8192];
        // Bound work per client per poll iteration so a busy peer cannot starve
        // other clients or shutdown. Remaining bytes stay readable for next poll.
        for (int chunk = 0; chunk < 16; ++chunk) {
            const auto count = read(client.fd.get(), buffer, sizeof(buffer));
            if (count > 0) {
                try {
                    if (!client.decoder.feed(buffer, static_cast<size_t>(count), [&](const std::string& message) { handle(client.id, message); })) return false;
                } catch (const std::exception& error) {
                    std::cerr << "Argus rejected client message: " << error.what() << '\n';
                    return false;
                }
            } else if (count == 0) {
                if (!client.decoder.complete()) std::cerr << "Argus client closed in a partial frame\n";
                return false;
            } else if (errno == EINTR) continue;
            else return errno == EAGAIN || errno == EWOULDBLOCK;
        }
        return true;
    }

    void run() {
        while (running) {
            std::vector<pollfd> descriptors {{ listener.get(), POLLIN, 0 }};
            for (const auto& client : clients) {
                descriptors.push_back({ client.fd.get(), short(POLLIN | (client.writer.pending() ? POLLOUT : 0)), 0 });
            }
            const int result = poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()), 100);
            if (result < 0) {
                if (errno == EINTR) continue;
                std::cerr << "Argus poll failed: " << strerror(errno) << '\n';
                break;
            }
            // Process the existing client set before accept can append clients.
            for (size_t i = clients.size(); i > 0; --i) {
                auto& client = clients[i - 1];
                const auto events = descriptors[i].revents;
                bool alive = true;
                if (events & (POLLIN | POLLHUP)) alive = readClient(client);
                if (alive && (events & POLLOUT)) alive = client.writer.flush(client.fd.get());
                if (events & (POLLERR | POLLNVAL)) alive = false;
                if (!alive) { notifyClosed(client.id); clients.erase(clients.begin() + (i - 1)); }
            }
            if (descriptors[0].revents & POLLIN) acceptClients();
        }
    }
};

SocketServer::SocketServer(MessageHandler handle, CloseHandler closed)
    : m_state(std::make_unique<State>(std::move(handle), std::move(closed))) {}
SocketServer::~SocketServer() = default;

bool SocketServer::setup() {
    auto& state = *m_state;
    if (state.running) return true;
    try {
        state.endpoint = std::make_unique<argus::LocalEndpoint>(argus::LocalEndpoint::open());
        state.lock = argus::UniqueFd(openat(state.endpoint->directory.get(), "service.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
        struct stat info {};
        if (state.lock.get() < 0 || fstat(state.lock.get(), &info) < 0 || !S_ISREG(info.st_mode) ||
            info.st_uid != geteuid() || (info.st_mode & 0077) != 0 || flock(state.lock.get(), LOCK_EX | LOCK_NB) < 0) {
            throw std::runtime_error("Cannot acquire Argus service lock (another instance may be running)");
        }
        if (fstatat(state.endpoint->directory.get(), "channel.sock", &info, AT_SYMLINK_NOFOLLOW) == 0) {
            if (!S_ISSOCK(info.st_mode) || info.st_uid != geteuid()) throw std::runtime_error("Refusing to replace an untrusted socket path");
            if (unlinkat(state.endpoint->directory.get(), "channel.sock", 0) < 0) throw std::runtime_error("Cannot remove stale Argus socket");
        } else if (errno != ENOENT) throw std::runtime_error("Cannot inspect Argus socket path");
        state.listener = argus::UniqueFd(socket(AF_UNIX, SOCK_STREAM, 0));
        if (state.listener.get() < 0) throw std::runtime_error("Cannot create Argus socket");
        argus::nonblocking(state.listener.get());
        // The enclosing directory is already private (0700). Avoid a process-
        // global umask change entirely; set the socket's final mode explicitly.
        if (bind(state.listener.get(), reinterpret_cast<const sockaddr*>(&state.endpoint->address), sizeof(sockaddr_un)) < 0) {
            throw std::runtime_error("Cannot bind Argus socket");
        }
        if (fstatat(state.endpoint->directory.get(), "channel.sock", &info, AT_SYMLINK_NOFOLLOW) < 0) throw std::runtime_error("Cannot inspect bound socket");
        state.device = info.st_dev;
        state.inode = info.st_ino;
        if (chmod(state.endpoint->path.c_str(), 0600) < 0 || listen(state.listener.get(), 16) < 0) throw std::runtime_error("Cannot listen on Argus socket");
        state.running = true;
        state.thread = std::thread([&state] { state.run(); });
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Argus socket setup: " << error.what() << '\n';
        state.running = false;
        return false;
    }
}
