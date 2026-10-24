// Synthetic Chrome processes with real OS parentage/argv; no browser profile is touched.
#include "../native-common/Framing.hpp"
#include "../native-common/LocalSocket.hpp"
#include <csignal>
#include <iostream>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;

struct Child {
    pid_t pid;
    argus::UniqueFd input;
    Child(pid_t value, argus::UniqueFd fd) : pid(value), input(std::move(fd)) {}
    Child(Child&& other) noexcept : pid(std::exchange(other.pid, -1)), input(std::move(other.input)) {}
    ~Child() {
        input = argus::UniqueFd();
        if (pid > 0) { int status; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    }
};
Child launch(const char* path, bool renderer) {
    int fds[2];
    if (pipe(fds) < 0) throw std::runtime_error("pipe failed");
    argus::UniqueFd input(fds[0]), output(fds[1]);
    argus::nonblocking(output.get());
    fcntl(input.get(), F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, input.get(), STDIN_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    const char* args[] = { path, renderer ? "--type=renderer" : nullptr,
        renderer ? "--renderer-client-id=7" : nullptr, nullptr };
    pid_t pid = 0;
    const int error = posix_spawn(&pid, path, &actions, nullptr, const_cast<char**>(args), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) throw std::runtime_error("spawn failed");
    return {pid, std::move(output)};
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--type=renderer") { std::string ignored; std::getline(std::cin, ignored); return 0; }
    if (argc != 3) return 1;
    std::signal(SIGPIPE, SIG_IGN);
    try {
        auto renderer = launch(argv[2], true);
        auto a = launch(argv[1], false);
        auto b = launch(argv[1], false);
        std::cout << nlohmann::json{{"pid",getpid()},{"renderer",renderer.pid}}.dump() << std::endl;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "drop-a") { a.input = argus::UniqueFd(); continue; }
            auto value = nlohmann::json::parse(line);
            const int fd = value.at("sourceId") == "profile-b" ? b.input.get() : a.input.get();
            argus::FrameWriter writer;
            if (!writer.append(line)) return 2;
            while (writer.pending()) {
                if (!writer.flush(fd)) return 3;
                if (writer.pending()) { pollfd wait{fd,POLLOUT,0}; if (poll(&wait,1,2000) <= 0) return 4; }
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
