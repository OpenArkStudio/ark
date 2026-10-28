/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <args.hxx>

#include "ark/application_manifest.hpp"
#include "ark/bus.hpp"
#include "ark/dep.hpp"
#include "ark/macros.hpp"
#include "ark/misc.hpp"
#include "ark/platform.hpp"
#include "ark/plugin_manager.hpp"
#include "ark/time/date_time.hpp"
#include "shutdown_request.hpp"

using namespace ark;

#ifdef ARK_PLATFORM_WIN
// command control
std::thread g_cmd_thread;

// mini-dump
void create_dump_file(const std::string& dump_file_path_name, EXCEPTION_POINTERS* exception_ptr) {
    HANDLE h_dump_file =
        CreateFile(dump_file_path_name.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    MINIDUMP_EXCEPTION_INFORMATION dump_info;
    dump_info.ExceptionPointers = exception_ptr;
    dump_info.ThreadId = GetCurrentThreadId();
    dump_info.ClientPointers = TRUE;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h_dump_file, MiniDumpNormal, &dump_info, NULL, NULL);

    CloseHandle(h_dump_file);
}

long application_crash_handler(EXCEPTION_POINTERS* exception_ptr) {
    date_time now;
    std::string dump_name =
        ARK_FORMAT("{}-{:04d}{:02d}{:02d}_{:02d}_{:02d}_{:02d}.dmp", plugin_manager::instance()->get_app_name(),
            now.get_year(), now.get_month(), now.get_day(), now.get_hour(), now.get_minute(), now.get_second());

    create_dump_file(dump_name.c_str(), exception_ptr);

    FatalAppExit(-1, dump_name.c_str());
    return EXCEPTION_EXECUTE_HANDLER;
}

void disable_close_button() {
    HWND h_wnd = GetConsoleWindow();

    if (h_wnd)
    {
        HMENU h_menu = GetSystemMenu(h_wnd, FALSE);
        EnableMenuItem(h_menu, SC_CLOSE, MF_DISABLED | MF_BYCOMMAND);
    }
}

#elif defined(ARK_PLATFORM_LINUX) || defined(ARK_PLATFORM_DARWIN)

void init_daemon() {
#ifdef ARK_PLATFORM_DARWIN
    pid_t pid;
    int ret = posix_spawn(&pid, "/dev/null", nullptr, nullptr, nullptr, nullptr);
#else
    int ret = daemon(1, 0);
#endif
    ARK_ASSERT_NO_EFFECT(ret == 0);
}
#endif

void print_logo() {
#ifdef ARK_PLATFORM_WIN
    SetConsoleTextAttribute(
        GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#endif

    std::string logo = R"(
*******************************************************************************
__   __   ______      _____                        ___         _    
\ \ / /   |  _  \    |  _  |                      / _ \       | |   
 \ V /    | | | |    | | | | _ __    ___  _ __   / /_\ \ _ __ | | __
 /   \    | | | |    | | | || '_ \  / _ \| '_ \  |  _  || '__|| |/ /
/ /^\ \ _ | |/ /_    \ \_/ /| |_) ||  __/| | | | | | | || |   |   < 
\/   \/(_)|___/(_)    \___/ | .__/  \___||_| |_| \_| |_/|_|   |_|\_\
                            | |                                     
                            |_|                                     
                            
Copyright 2020 © X.D. NetWork Inc.
*******************************************************************************
)";

    ARK_LOG_INFO("{}", logo);
    ARK_LOG_INFO("ARK Server start ...");

#ifdef ARK_PLATFORM_WIN
    SetConsoleTextAttribute(
        GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#endif
}

#ifdef ARK_PLATFORM_WIN
void create_back_thread() {
    auto thread_func = []() {
        while (app_detail::requested_stop_reason() == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            std::string s;
            std::cin >> s;
            string_utils::to_lower(s);
            if (s == "exit")
            {
                app_detail::request_stop(app_detail::command_stop_reason);
            }
        }
    };

    g_cmd_thread = std::thread(thread_func);
}
#endif // ARK_PLATFORM_WIN

bool parse_args(int argc, char* argv[], std::chrono::milliseconds& shutdown_timeout) {
#if defined(ARK_PLATFORM_LINUX) || defined(ARK_PLATFORM_DARWIN)
    // ignore signals
    signal(SIGHUP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    // interupt signals
    std::signal(SIGINT, app_detail::ark_app_stop_signal_handler);
    std::signal(SIGTERM, app_detail::ark_app_stop_signal_handler);
#endif

    auto close_x_button = []() {
#ifdef ARK_PLATFORM_WIN
        SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)application_crash_handler);
        disable_close_button();
#endif
    };

    auto use_daemon = []() {
#if defined(ARK_PLATFORM_LINUX) || defined(ARK_PLATFORM_DARWIN)
        init_daemon();
#endif
    };

    args::ArgumentParser parser(
        "Here is ark plugin loader argument tools", "If you have any questions, please report an issue in GitHub.");
    args::HelpFlag help(parser, "help", "Display the help menu", {'h', "help"});
    args::ActionFlag xbutton(parser, "close", "Close [x] button in Windows", {'x'}, close_x_button);
    args::ActionFlag daemon(parser, "daemon", "Run application as daemon", {'d'}, use_daemon);

    args::ValueFlag<std::string> busid(parser, "busid", "Set application id(like IP address: 8.8.8.8)", {'b', "busid"},
        "8.8.8.8", args::Options::Required | args::Options::Single);

    args::ValueFlag<std::string> name(parser, "name", "Set application name", {'n', "name"}, "my-server",
        args::Options::Required | args::Options::Single);

    args::ValueFlag<std::string> app_conf(parser, "app config path", "Set application configuration", {'a', "app_conf"},
        "some.app.toml", args::Options::Required | args::Options::Single);

    args::ValueFlag<std::string> log_conf(parser, "log config path", "Set application log configuration",
        {'l', "log_conf"}, "./conf/log.conf", args::Options::Single);

    args::ValueFlag<std::string> shutdown_wait(parser, "milliseconds",
        "Retain process/resources if shutdown waits this long (0: unlimited)", {"shutdown_timeout_ms"}, "0",
        args::Options::Single);

    // start parse argument list
    try
    { parser.ParseCLI(argc, argv); }
    catch (args::Help& h)
    {
        std::cout << parser << ", error = " << h.what() << std::endl;
        return false;
    }
    catch (args::ParseError& e)
    {
        std::cout << parser << ", error = " << e.what() << std::endl;
        return false;
    }
    catch (args::ValidationError& e)
    {
        std::cout << parser << ", error = " << e.what() << std::endl;
        return false;
    }

    std::int64_t timeout_ms{};
    const auto& value = shutdown_wait.Get();
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), timeout_ms);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || timeout_ms < 0)
    {
        std::cout << "Shutdown timeout must be a non-negative integer in milliseconds within int64 range" << std::endl;
        return false;
    }
    shutdown_timeout = std::chrono::milliseconds(timeout_ms);

    // Set bus id
    if (busid)
    {
        std::vector<std::string> slices;
        string_utils::split(slices, busid.Get(), '.');
        if (slices.size() != 4)
        {
            std::cout << "Bus id is invalid, it likes 8.8.8.8" << std::endl;
            return false;
        }

        bus_addr bus_addr;
        bus_addr.from_string(busid.Get());
        plugin_manager::instance()->set_bus_id(bus_addr.bus_id);
    }
    else
    {
        return false;
    }

    // Set app name
    if (name)
    {
        plugin_manager::instance()->set_app_name(name.Get());

        std::string process_name =
            ARK_FORMAT("{}-{}-{}", name.Get(), busid.Get(), plugin_manager::instance()->get_bus_id());
        // Set process name
#ifdef ARK_PLATFORM_WIN
        SetConsoleTitle(process_name.c_str());
#elif defined(ARK_PLATFORM_LINUX)
        // Do not need to change process name
#endif
    }
    else
    {
        return false;
    }

    // Set plugin file
    if (app_conf)
    {
        plugin_manager::instance()->set_app_conf(app_conf.Get());
    }
    else
    {
        return false;
    }

    // log_conf is optional arg.
    logger::init(nullptr);
    auto log_file_name = ARK_FORMAT("{}{}{}{}", plugin_manager::instance()->get_app_name(), "_",
        misc::bus2_str(plugin_manager::instance()->get_bus_id()), ".log");
    logger::instance()->init_logger(log_conf.Get(), log_file_name);

    return true;
}

bool capture_update_exception(std::exception_ptr& error) noexcept {
    try
    { return plugin_manager::instance()->update(date_time::get_now_time()); }
    catch (...)
    {
        error = std::current_exception();
        return false;
    }
}

bool main_loop(std::exception_ptr* error = nullptr) {
#ifdef ARK_PLATFORM_WIN
    __try
    {
        // Catch C++ exceptions before the outer SEH filter sees them. The
        // shutdown owner rethrows only after this guarded frame returns.
        return error ? capture_update_exception(*error) : plugin_manager::instance()->update(date_time::get_now_time());
    }
    __except (application_crash_handler(GetExceptionInformation()))
    { return false; }
#else
    return error ? capture_update_exception(*error) : plugin_manager::instance()->update(date_time::get_now_time());
#endif
}

[[noreturn]] void exit_startup_failure(const char* detail) {
    std::string diagnostic{"Application startup failed"};
    const auto& failure = plugin_manager::instance()->get_startup_failure();
    if (!failure.empty())
    {
        diagnostic += " at ";
        diagnostic += failure;
    }
    diagnostic += ": ";
    diagnostic += detail;
    diagnostic += '\n';
    // Build the line before writing: separate stream insertions let concurrent
    // console logs split the lifecycle phase from its module context.
    std::fwrite(diagnostic.data(), 1, diagnostic.size(), stderr);
    std::fflush(stderr);

    // The diagnostic is flushed before exiting. Partially started plugins may
    // retain live IO pools; normal static cleanup can wait for them forever.
#ifdef ARK_PLATFORM_WIN
    // Avoid DLL_PROCESS_DETACH and its static destructors as well as main's CRT
    // cleanup. A failed startup has no safe plugin teardown order.
    TerminateProcess(GetCurrentProcess(), 1);
#endif
    std::_Exit(1);
}

int main(int argc, char* argv[]) {
    std::chrono::milliseconds shutdown_timeout{};
    if (!parse_args(argc, argv, shutdown_timeout))
    {
        std::cout << "Application parameter is invalid, please check it..." << std::endl;
        return 0;
    }

    print_logo();

    const auto& app_conf_path = plugin_manager::instance()->get_app_conf();
    application_manifest manifest;
    try
    { manifest = load_application_manifest(app_conf_path); }
    catch (const application_manifest_error& error)
    {
        std::cerr << "Application manifest loading failed: " << error.what() << std::endl;
        return 1;
    }

    try
    {
        if (!plugin_manager::instance()->start(
                date_time::get_now_time(), manifest.plugin_library_dir, manifest.plugin_config_dir, manifest.plugins))
        {
            exit_startup_failure("returned false");
        }
    }
    catch (const std::exception& error)
    { exit_startup_failure(error.what()); }
    catch (...)
    { exit_startup_failure("unknown exception"); }

#ifdef ARK_PLATFORM_WIN
    create_back_thread();
#endif

    for (;; std::this_thread::sleep_for(std::chrono::milliseconds(1)))
    {
        const auto stop_reason = app_detail::requested_stop_reason();
        if (stop_reason != 0)
        {
            if (stop_reason > 0)
            {
                ARK_LOG_INFO("Server stop requested by signal {}", stop_reason);
            }
            break;
        }

        main_loop();
    }

    // Normal shutdown closes only managed top-level task admission. Accepted
    // tasks still need module updates and the shared owner executor to finish.
    // A configured deadline latches resource retention; it never authorizes
    // cancellation, unloading or process exit, even if work completes later.
    const auto shutdown_started = std::chrono::steady_clock::now();
    bool shutdown_retained = false;
    bool shutdown_failure_reported = false;
    bool shutdown_update_error_reported = false;
    for (;;)
    {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shutdown_started);
        if (!shutdown_retained && shutdown_timeout.count() > 0 && elapsed >= shutdown_timeout &&
            plugin_manager::instance()->get_shutdown_state() == plugin_manager::shutdown_state::draining)
        {
            shutdown_retained = true;
            const auto pending = plugin_manager::instance()->pending_tasks();
            ARK_LOG_ERROR("Shutdown wait timed out after {} ms (limit={} ms, pending={}, phase={}); "
                          "process and resources are retained",
                elapsed.count(), shutdown_timeout.count(), pending, pending ? "managed_tasks" : "ready_to_shut");
        }
        if (!shutdown_retained && plugin_manager::instance()->stop(date_time::get_now_time()))
        {
            break;
        }
        if (plugin_manager::instance()->get_shutdown_state() == plugin_manager::shutdown_state::failed)
        {
            if (!shutdown_failure_reported)
            {
                try
                {
                    if (const auto error = plugin_manager::instance()->get_shutdown_exception())
                    {
                        std::rethrow_exception(error);
                    }
                    ARK_LOG_ERROR("Shutdown failed at {}; process and resources are retained",
                        plugin_manager::instance()->get_shutdown_failure());
                }
                catch (const std::exception& error)
                {
                    ARK_LOG_ERROR("Shutdown failed at {}: {}; process and resources are retained",
                        plugin_manager::instance()->get_shutdown_failure(), error.what());
                }
                catch (...)
                {
                    ARK_LOG_ERROR("Shutdown failed at {} with an unknown exception; process and resources are retained",
                        plugin_manager::instance()->get_shutdown_failure());
                }
                shutdown_failure_reported = true;
            }
        }
        else
        {
            try
            {
                std::exception_ptr update_error;
                const auto updated = main_loop(&update_error);
                if (update_error)
                {
                    std::rethrow_exception(update_error);
                }
                if (!updated)
                {
                    shutdown_retained = true;
                    if (!shutdown_update_error_reported)
                    {
                        ARK_LOG_ERROR(
                            "{}", "Shutdown update failed: update returned false; process and resources are retained");
                        shutdown_update_error_reported = true;
                    }
                }
            }
            catch (const std::exception& error)
            {
                shutdown_retained = true;
                if (!shutdown_update_error_reported)
                {
                    ARK_LOG_ERROR("Shutdown update failed: {}; process and resources are retained", error.what());
                    shutdown_update_error_reported = true;
                }
            }
            catch (...)
            {
                shutdown_retained = true;
                if (!shutdown_update_error_reported)
                {
                    ARK_LOG_ERROR(
                        "{}", "Shutdown update failed with an unknown exception; process and resources are retained");
                    shutdown_update_error_reported = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

#ifdef ARK_PLATFORM_WIN
    g_cmd_thread.join();
#endif

    // Complete the final lifecycle diagnostic before returning: the async
    // logger's queue is not drained when the process exits.
    std::fputs("ARK Server stop ...\n", stdout);
    std::fflush(stdout);

    return 0;
}
