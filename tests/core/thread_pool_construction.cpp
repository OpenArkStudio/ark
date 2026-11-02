// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <malloc.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dirent.h>
#endif

#include "../../src/plugins/utility/ctpl_stl.h"

namespace {

// Native enumeration neither allocates through operator new nor needs a worker
// test hook. Observe actual OS workers before throwing on the owner thread.
int process_threads() noexcept {
#ifdef _WIN32
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return -1;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    int count{};
    if (Thread32First(snapshot, &entry))
    {
        do
        {
            if (entry.th32OwnerProcessID == GetCurrentProcessId())
            {
                ++count;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
#elif defined(__APPLE__)
    thread_act_array_t threads{};
    mach_msg_type_number_t count{};
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
    {
        return -1;
    }
    for (mach_msg_type_number_t index{}; index < count; ++index)
    {
        mach_port_deallocate(mach_task_self(), threads[index]);
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
    return static_cast<int>(count);
#else
    auto* directory = opendir("/proc/self/task");
    if (!directory)
    {
        return -1;
    }
    int count{};
    while (const auto* entry = readdir(directory))
    {
        if (entry->d_name[0] != '.')
        {
            ++count;
        }
    }
    closedir(directory);
    return count;
#endif
}

thread_local bool armed{};
thread_local std::size_t remaining{}, attempts{}, rejected{};
thread_local std::size_t failed_allocation_size{};
thread_local int baseline_threads{}, threads_at_failure{};
const char* active_mode = "controller";
const char* active_stage = "controller";
std::size_t active_budget{};

void before_allocation(std::size_t size) {
    ++attempts;
    if (armed && remaining-- == 0)
    {
        // Stay failed: rollback, exception propagation and joining must not
        // depend on another successful allocation on this owner thread.
        remaining = 0;
        ++rejected;
        failed_allocation_size = size;
        threads_at_failure = process_threads();
        throw std::bad_alloc{};
    }
}

void* allocate(std::size_t size) {
    before_allocation(size);
    if (auto* pointer = std::malloc(size ? size : 1))
    {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* allocate_aligned(std::size_t size, std::size_t alignment) {
    before_allocation(size);
#ifdef _WIN32
    if (auto* pointer = _aligned_malloc(size ? size : 1, alignment))
    {
        return pointer;
    }
#else
    void* pointer{};
    if (posix_memalign(&pointer, alignment, size ? size : 1) == 0)
    {
        return pointer;
    }
#endif
    throw std::bad_alloc{};
}

void free_aligned(void* pointer) noexcept {
#ifdef _WIN32
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

} // namespace

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer, std::align_val_t) noexcept {
    free_aligned(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept {
    free_aligned(pointer);
}

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    free_aligned(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    free_aligned(pointer);
}

// Keep every nothrow form in the same allocation family under sanitizers.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new[](size); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    ::operator delete(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    ::operator delete[](pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size, alignment); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try
    { return ::operator new[](size, alignment); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    ::operator delete[](pointer, alignment);
}

namespace {

constexpr int no_failure = 77;
constexpr int partial_workers = 10;

void arm(std::size_t index) {
    remaining = index;
    attempts = rejected = 0;
    armed = true;
}

int settled_process_threads() {
    auto count = process_threads();
    if (count < 0)
    {
        return count;
    }
    auto stable_since = std::chrono::steady_clock::now();
    const auto deadline = stable_since + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto current = process_threads();
        if (current < 0)
        {
            return current;
        }
        if (current != count)
        {
            count = current;
            stable_since = std::chrono::steady_clock::now();
        }
        else if (std::chrono::steady_clock::now() - stable_since >= std::chrono::milliseconds(100))
        {
            return count;
        }
        std::this_thread::yield();
    }
    return -1;
}

bool workers_retired() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (process_threads() != baseline_threads)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

int constructor_failure(std::size_t index) {
    // Let runtimes such as ThreadSanitizer initialize persistent helper threads
    // before recording the pool-owned thread baseline.
    std::thread runtime_warmup([] {});
    runtime_warmup.join();
    baseline_threads = settled_process_threads();
    if (baseline_threads < 1)
    {
        return 20;
    }
    bool threw{};
    arm(index);
    active_stage = "constructing pool";
    try
    {
        ctpl::thread_pool pool(4);
        // Destruction also runs with the persistent owner-thread fault armed.
        active_stage = "pool constructed";
    }
    catch (const std::bad_alloc&)
    {
        active_stage = "caught bad_alloc";
        threw = true;
    }
    active_stage = "checking worker retirement";
    const bool retired = workers_retired();
    const bool persistent = armed && rejected == (threw ? 1u : 0u);
    armed = false;
    if (!retired || !persistent)
    {
        std::fprintf(stderr,
            "constructor budget=%zu retired=%d persistent=%d baseline=%d failure=%d current=%d attempts=%zu "
            "rejected=%zu\n",
            index, retired, persistent, baseline_threads, threads_at_failure, process_threads(), attempts, rejected);
        return 21;
    }
    return !threw ? no_failure : threads_at_failure > baseline_threads ? partial_workers : 0;
}

struct capture
{
    unsigned& destroyed;

    ~capture() {
        ++destroyed;
    }
};

struct submission
{
    std::shared_ptr<capture> owner;
    std::atomic<unsigned>* calls;

    int operator()(int value) const {
        ++*calls;
        return value;
    }

    int operator()(int value, int extra) const {
        ++*calls;
        return value + extra;
    }
};

std::future<int> submit(ctpl::thread_pool& pool, submission&& callback, bool variadic) {
    if (variadic)
    {
        return pool.push(std::move(callback), 7);
    }
    return pool.push(std::move(callback));
}

// Detect a real deque block/map allocation from the extra allocations compared
// with an ordinary submission. This avoids assumptions about libc++/libstdc++/
// MSVC queue block sizes, and requires no production allocation test hook.
std::size_t queue_boundary(bool variadic) {
    ctpl::thread_pool pool(0);
    std::atomic<unsigned> calls{};
    std::array<std::size_t, 2048> counts{};
    for (auto& count : counts)
    {
        submission callback{{}, &calls};
        const auto before = attempts;
        auto future = submit(pool, std::move(callback), variadic);
        count = attempts - before;
    }
    const auto minimum = *std::min_element(counts.begin(), counts.end());
    const auto extra = std::find_if(counts.begin(), counts.end(), [minimum](auto count) { return count > minimum; });
    return static_cast<std::size_t>(extra - counts.begin());
}

int submission_failure(std::size_t index, bool variadic, std::size_t boundary) {
    unsigned destroyed{};
    std::atomic<unsigned> calls{};
    auto owner = std::make_shared<capture>(destroyed);
    bool threw{};
    {
        ctpl::thread_pool pool(0);
        for (std::size_t count{}; count < boundary; ++count)
        {
            auto future = submit(pool, submission{{}, &calls}, variadic);
        }
        // Prepare all call arguments before arming; rejected allocations must
        // come from the actual push body, including its real queue insertion.
        submission callback{owner, &calls};
        arm(index);
        try
        { auto future = submit(pool, std::move(callback), variadic); }
        catch (const std::bad_alloc&)
        { threw = true; }
        callback.owner.reset();
        if (!armed || rejected != (threw ? 1u : 0u) || owner.use_count() != (threw ? 1 : 2) || calls != 0)
        {
            armed = false;
            return 22;
        }
        // Discarding queued wrappers must release every capture without running
        // callbacks, while the allocation failure is still armed.
        pool.stop(false);
        if (owner.use_count() != 1 || calls != 0 || rejected != (threw ? 1u : 0u))
        {
            armed = false;
            return 23;
        }
        armed = false;
    }
    owner.reset();
    if (destroyed != 1)
    {
        return 24;
    }
    return threw ? 0 : no_failure;
}

int run_child(const char* executable, const char* mode, std::size_t index, std::size_t boundary,
    std::chrono::steady_clock::time_point controller_deadline) {
    const auto deadline = std::min(controller_deadline, std::chrono::steady_clock::now() + std::chrono::seconds(5));
    if (std::chrono::steady_clock::now() >= deadline)
    {
        return 91;
    }
    const auto index_text = std::to_string(index);
    const auto boundary_text = std::to_string(boundary);
#ifdef _WIN32
    auto command = std::string("\"") + executable + "\" " + mode + " " + index_text + " " + boundary_text;
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        return 90;
    }
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    const auto wait =
        WaitForSingleObject(process.hProcess, static_cast<DWORD>(std::max<decltype(milliseconds)>(0, milliseconds)));
    DWORD code = 91;
    if (wait == WAIT_OBJECT_0)
    {
        GetExitCodeProcess(process.hProcess, &code);
    }
    else
    {
        TerminateProcess(process.hProcess, 91);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
#else
    const auto child = fork();
    if (child < 0)
    {
        return 90;
    }
    if (child == 0)
    {
        execlp(executable, executable, mode, index_text.c_str(), boundary_text.c_str(), static_cast<char*>(nullptr));
        std::_Exit(90);
    }
    int status{};
    while (true)
    {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child)
        {
            break;
        }
        if (result < 0 && errno != EINTR)
        {
            return 90;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            return 91;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 92;
#endif
}

int sweep(const char* executable, const char* mode, std::size_t boundary,
    std::chrono::steady_clock::time_point controller_deadline) {
    unsigned passed{}, partial{}, failed{};
    bool succeeded{};
    for (std::size_t index{}; index < 128; ++index)
    {
        const auto code = run_child(executable, mode, index, boundary, controller_deadline);
        if (code == no_failure)
        {
            succeeded = true;
            break;
        }
        if (code == 0 || code == partial_workers)
        {
            ++passed;
            partial += code == partial_workers;
        }
        else
        {
            ++failed;
            std::printf("%s allocation %zu failed: child exit %d\n", mode, index, code);
            if (code >= 90)
            {
                break; // a timeout or process-control failure must not start another child
            }
        }
    }
    std::printf(
        "%s: %u persistent failures passed, %u after workers started, %u failed, success=%d, queue boundary=%zu\n",
        mode, passed, partial, failed, succeeded, boundary);
    return !succeeded || failed || !passed || (std::string_view(mode) == "constructor" && !partial);
}

} // namespace

int main(int argc, char** argv) {
    std::set_terminate([]() noexcept {
        int exception_kind{};
        if (auto exception = std::current_exception())
        {
            try
            { std::rethrow_exception(exception); }
            catch (const std::bad_alloc&)
            { exception_kind = 1; }
            catch (const std::exception&)
            { exception_kind = 2; }
            catch (...)
            { exception_kind = 3; }
        }
        std::fprintf(stderr,
            "terminate mode=%s stage=%s budget=%zu exception=%d uncaught=%d armed=%d remaining=%zu attempts=%zu "
            "rejected=%zu failed_size=%zu baseline=%d failure=%d current=%d\n",
            active_mode, active_stage, active_budget, exception_kind, std::uncaught_exceptions(), armed, remaining,
            attempts, rejected, failed_allocation_size, baseline_threads, threads_at_failure, process_threads());
        std::fflush(stderr);
        std::_Exit(86);
    });
    if (argc == 4)
    {
        const auto index = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
        const auto boundary = static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
        active_mode = argv[1];
        active_stage = "child dispatch";
        active_budget = index;
        if (std::string_view(argv[1]) == "constructor")
        {
            return constructor_failure(index);
        }
        if (std::string_view(argv[1]) == "single" || std::string_view(argv[1]) == "variadic")
        {
            return submission_failure(index, std::string_view(argv[1]) == "variadic", boundary);
        }
        return 2;
    }
    if (argc != 1)
    {
        return 2;
    }
    const auto controller_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    const auto single = queue_boundary(false);
    const auto variadic = queue_boundary(true);
    if (single == 2048 || variadic == 2048)
    {
        std::fputs("Could not discover a real queue insertion allocation\n", stderr);
        return 25;
    }
    int failed = sweep(argv[0], "constructor", 0, controller_deadline);
    failed += sweep(argv[0], "single", single, controller_deadline);
    failed += sweep(argv[0], "variadic", variadic, controller_deadline);
    return failed ? 1 : 0;
}
