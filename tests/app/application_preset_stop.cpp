// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void reap(pid_t pid) {
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }
}

void exit_success(int) {
    _exit(0);
}

void exit_failure(int) {
    _exit(23);
}

int fixture(const std::string& mode, int identity) {
    if (mode == "success")
    {
        std::signal(SIGTERM, exit_success);
    }
    else if (mode == "failure")
    {
        std::signal(SIGTERM, exit_failure);
    }
    else
    {
        std::signal(SIGTERM, SIG_IGN);
    }
    const auto pid = getpid();
    require(write(identity, &pid, sizeof(pid)) == sizeof(pid), "fixture PID write failed");
    close(identity);
    if (mode == "early_failure")
    {
        return 37;
    }
    if (mode != "not_ready")
    {
        std::cout << "timers_module, PostInit" << std::endl;
    }
    if (mode == "ready_early_failure")
    {
        return 37;
    }
    for (;;)
    {
        pause();
    }
}

class owned_case
{
public:
    owned_case() = default;
    owned_case(const owned_case&) = delete;
    owned_case& operator=(const owned_case&) = delete;

    ~owned_case() {
        if (helper_ > 0)
        {
            kill(helper_, SIGKILL);
            reap(helper_);
        }
        if (child_ > 0)
        {
            kill(child_, SIGKILL);
        }
        std::error_code ignored;
        std::filesystem::remove(pipe_, ignored);
    }

    void run(const char* script, const char* executable, const std::filesystem::path& directory,
        const std::string& mode, int interrupt, bool broken_reader, int expected, pid_t unrelated) {
        pipe_ = directory / "output.pipe";
        int identity[2];
        require(pipe(identity) == 0, "identity pipe failed");
        const auto descriptor = std::to_string(identity[1]);
        helper_ = fork();
        require(helper_ >= 0, "helper fork failed");
        if (helper_ == 0)
        {
            // Trap handlers in a non-interactive shell require these signals
            // to be catchable when the shell starts, regardless of its runner.
            constexpr int signals[] = {SIGHUP, SIGINT, SIGTERM, SIGALRM, SIGPIPE};
            sigset_t catchable;
            if (sigemptyset(&catchable) != 0)
            {
                _exit(126);
            }
            for (const int number : signals)
            {
                if (std::signal(number, SIG_DFL) == SIG_ERR || sigaddset(&catchable, number) != 0)
                {
                    _exit(126);
                }
            }
            if (sigprocmask(SIG_UNBLOCK, &catchable, nullptr) != 0)
            {
                _exit(126);
            }
            close(identity[0]);
            if (broken_reader)
            {
                int output[2];
                if (pipe(output) != 0 || dup2(output[1], STDOUT_FILENO) < 0)
                {
                    _exit(126);
                }
                close(output[0]);
                close(output[1]);
            }
            const auto deadline = interrupt ? "10" : "1";
            execl("/bin/sh", "sh", script, pipe_.c_str(), deadline, executable, "--fixture", mode.c_str(),
                descriptor.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        close(identity[1]);
        pollfd event{identity[0], POLLIN, 0};
        const auto readable = poll(&event, 1, 3000);
        const auto received = readable > 0 ? read(identity[0], &child_, sizeof(child_)) : -1;
        close(identity[0]);
        require(received == sizeof(child_) && child_ > 0, "fixture did not report its PID");
        require(child_ != helper_ && child_ != unrelated, "fixture identity is not an owned child");
        if (interrupt)
        {
            require(kill(helper_, interrupt) == 0, "helper interrupt failed");
        }
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        int status = 0;
        pid_t exited = 0;
        while ((exited = waitpid(helper_, &status, WNOHANG)) == 0 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(10ms);
        }
        require(exited == helper_, "helper failed to clean up within its test bound");
        helper_ = -1;
        require(WIFEXITED(status) && WEXITSTATUS(status) == expected, "helper lost child or interrupt exit status");
        require(kill(child_, 0) < 0 && errno == ESRCH, "owned fixture remains alive or unreaped");
        child_ = -1;
        require(!std::filesystem::exists(pipe_), "helper left its FIFO behind");
        siginfo_t unrelated_state{};
        require(waitid(P_PID, static_cast<id_t>(unrelated), &unrelated_state, WEXITED | WNOHANG | WNOWAIT) == 0 &&
                    unrelated_state.si_pid == 0,
            "helper signalled an unrelated process");
        std::cout << mode << ": bounded cleanup, child reaped, FIFO removed, unrelated process alive; exit " << expected
                  << std::endl;
    }

private:
    pid_t helper_ = -1;
    pid_t child_ = -1;
    std::filesystem::path pipe_;
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 4 && std::string(argv[1]) == "--fixture")
    {
        return fixture(argv[2], std::stoi(argv[3]));
    }
    if (argc != 2)
    {
        std::cerr << "Usage: application_preset_stop <helper-script>" << std::endl;
        return 2;
    }
    // Build runners can ignore or block SIGINT. Exercise both inherited states
    // every run; the helper child must restore its signal preconditions.
    const auto previous_interrupt = std::signal(SIGINT, SIG_IGN);
    if (previous_interrupt == SIG_ERR)
    {
        std::cerr << "could not prepare inherited SIGINT disposition" << std::endl;
        return 1;
    }
    sigset_t blocked, previous_mask;
    if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGINT) != 0 ||
        sigprocmask(SIG_BLOCK, &blocked, &previous_mask) != 0)
    {
        std::cerr << "could not prepare inherited SIGINT mask" << std::endl;
        return 1;
    }
    const auto directory = std::filesystem::temp_directory_path() / ("ark-preset-stop-" + std::to_string(getpid()));
    if (!std::filesystem::create_directory(directory))
    {
        return 1;
    }
    const auto unrelated = fork();
    if (unrelated == 0)
    {
        for (;;)
        {
            pause();
        }
    }
    int result = 0;
    try
    {
        require(unrelated > 0, "unrelated fixture fork failed");
        owned_case{}.run(argv[1], argv[0], directory, "success", 0, false, 0, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "failure", 0, false, 23, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "early_failure", 0, false, 37, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "ready_early_failure", 0, false, 37, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "not_ready", 0, false, 124, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "ignore_term", 0, false, 124, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "not_ready", SIGHUP, false, 129, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "not_ready", SIGINT, false, 130, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "not_ready", SIGTERM, false, 143, unrelated);
        owned_case{}.run(argv[1], argv[0], directory, "ignore_term", 0, true, 141, unrelated);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << std::endl;
        result = 1;
    }
    if (unrelated > 0)
    {
        kill(unrelated, SIGTERM);
        reap(unrelated);
    }
    std::filesystem::remove_all(directory);
    sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
    std::signal(SIGINT, previous_interrupt);
    return result;
}
