// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/kernel_module.hpp>
#include <ark/kernel/map_module.hpp>
#include <ark/meta_define.hpp>
#include <ark/plugin_manager.hpp>

namespace ark {

class entity_operations_benchmark
{
public:
    entity_operations_benchmark(kernel_module& kernel, bool timed, std::ostream& output)
        : kernel_module_(&kernel)
        , timed_(timed)
        , output_(output) {
    }

    void query_string_test(const guid_t& guid, size_t count, uint32_t index);
    void query_int_test(const guid_t& guid, size_t count, uint32_t index);
    void set_data_test(const guid_t& guid, size_t count, uint32_t index);

private:
    auto now() const {
        return timed_ ? std::chrono::steady_clock::now().time_since_epoch() : std::chrono::steady_clock::duration{};
    }

    kernel_module* kernel_module_;
    bool timed_;
    std::ostream& output_;
};

void entity_operations_benchmark::query_string_test(const guid_t& guid, size_t count, const uint32_t index) {
    auto entity_ptr = kernel_module_->get_entity(guid);
    if (nullptr == entity_ptr)
    {
        return;
    }

    output_ << "----------[" << index << "] time [query string] test start----------" << std::endl;

    // query string data with int key
    auto begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto& value = entity_ptr->get_string(entity_meta_player::name_index());
    }
    auto end = now();
    auto time_int_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    // query string data with string key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto& value = entity_ptr->get_string(entity_meta_player::name());
    }
    end = now();
    auto time_string_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    output_ << "[" << count << "] query with [int key] time = " << time_int_key << " ms"
            << " with [string key] time = " << time_string_key << std::endl;

    // query string data with int key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto obj_ptr = kernel_module_->get_entity(guid);
        if (nullptr == obj_ptr)
        {
            return;
        }

        auto& value = obj_ptr->get_string(entity_meta_player::name_index());
    }
    end = now();
    time_int_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    // query string data with string key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto obj_ptr = kernel_module_->get_entity(guid);
        if (nullptr == obj_ptr)
        {
            return;
        }

        auto& value = obj_ptr->get_string(entity_meta_player::name());
    }
    end = now();
    time_string_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    output_ << "[" << count << "] find then query with [int key] time = " << time_int_key << " ms"
            << " with [string key] time = " << time_string_key << std::endl;

    output_ << "----------[" << index << "] time [query string] test end----------" << std::endl;
}

void entity_operations_benchmark::query_int_test(const guid_t& guid, size_t count, const uint32_t index) {
    auto entity_ptr = kernel_module_->get_entity(guid);
    if (nullptr == entity_ptr)
    {
        return;
    }

    output_ << "----------[" << index << "] time [query int] test start----------" << std::endl;

    // query string data with int key
    auto begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto value = entity_ptr->get_int32(entity_meta_player::gender_index());
    }
    auto end = now();
    auto time_int_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    // query string data with string key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto value = entity_ptr->get_int32(entity_meta_player::gender());
    }
    end = now();
    auto time_string_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    output_ << "[" << count << "] query with [int key] time = " << time_int_key << " ms"
            << " with [string key] time = " << time_string_key << std::endl;

    // query string data with int key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto obj_ptr = kernel_module_->get_entity(guid);
        if (nullptr == obj_ptr)
        {
            return;
        }

        auto value = obj_ptr->get_int32(entity_meta_player::gender_index());
    }
    end = now();
    time_int_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    // query string data with string key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto obj_ptr = kernel_module_->get_entity(guid);
        if (nullptr == obj_ptr)
        {
            return;
        }

        auto value = obj_ptr->get_int32(entity_meta_player::gender());
    }
    end = now();
    time_string_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    output_ << "[" << count << "] find then query with [int key] time = " << time_int_key << " ms"
            << " with [string key] time = " << time_string_key << std::endl;

    output_ << "----------[" << index << "] time [query int] test end----------" << std::endl;
}

void entity_operations_benchmark::set_data_test(const guid_t& guid, size_t count, const uint32_t index) {
    auto entity_ptr = kernel_module_->get_entity(guid);
    if (nullptr == entity_ptr)
    {
        return;
    }

    output_ << "----------[" << index << "] time [set int] test start----------" << std::endl;

    // set data with int key
    auto begin = now();
    for (size_t i = 0; i < count; i++)
    {
        entity_ptr->set_int32(entity_meta_player::gender_index(), 22);
    }
    auto end = now();
    auto time_int_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    // set data with string key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        entity_ptr->set_int32(entity_meta_player::gender(), 22);
    }
    end = now();
    auto time_string_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    output_ << "[" << count << "] set with [int key] time = " << time_int_key << " ms"
            << " with [string key] time = " << time_string_key << std::endl;

    // set data with int key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto obj_ptr = kernel_module_->get_entity(guid);
        if (nullptr == obj_ptr)
        {
            return;
        }

        obj_ptr->set_int32(entity_meta_player::gender_index(), 11);
    }
    end = now();
    time_int_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    // set data with string key
    begin = now();
    for (size_t i = 0; i < count; i++)
    {
        auto obj_ptr = kernel_module_->get_entity(guid);
        if (nullptr == obj_ptr)
        {
            return;
        }

        obj_ptr->set_int32(entity_meta_player::gender(), 11);
    }
    end = now();
    time_string_key = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    output_ << "[" << count << "] find then set with [int key] time = " << time_int_key << " ms"
            << " with [string key] time = " << time_string_key << std::endl;

    output_ << "----------[" << index << "] time [set int] test end----------" << std::endl;
}

} // namespace ark

int main(int argc, char* argv[]) {
    if ((argc != 4 && argc != 5) || std::string(argv[1]) != "--plugin-dir")
    {
        std::cerr << "Usage: ark_entity_operations_benchmark --plugin-dir DIR --smoke | --iterations N" << std::endl;
        return 2;
    }
    const bool smoke = argc == 4 && std::string(argv[3]) == "--smoke";
    std::size_t iterations = 8;
    if (!smoke)
    {
        if (argc != 5 || std::string(argv[3]) != "--iterations")
        {
            return 2;
        }
        const std::string text = argv[4];
        const auto result = std::from_chars(text.data(), text.data() + text.size(), iterations);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || iterations == 0)
        {
            return 2;
        }
    }
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& text) { std::cout << text << '\n'; });
    ark::plugin_manager manager;
    if (!manager.start(0, std::string(argv[2]) + "/", "conf/",
            {{"ark_kernel_plugin", "kernel.plugin.conf"}, {"ark_utility_plugin", ""}}))
    {
        std::cerr << "Benchmark dependency startup failed: " << manager.get_startup_failure() << std::endl;
        std::_Exit(1);
    }
    auto* kernel = manager.find_module<ark::kernel_module>();
    auto* maps = manager.find_module<ark::map_module>();
    if (!kernel || !maps || !maps->create_map(1))
    {
        std::_Exit(1);
    }
    auto player = kernel->create_entity(
        ark::null_guid, 1, 0, ark::entity_meta_player::self_name(), ark::null_int, ark::data_list_impl{});
    if (!player || !player->set_string(ark::entity_meta_player::name(), "Xiaoming") ||
        !player->set_int32(ark::entity_meta_player::gender(), 1))
    {
        std::_Exit(1);
    }
    std::ostringstream quiet;
    ark::entity_operations_benchmark workload(*kernel, !smoke, smoke ? quiet : std::cout);
    workload.query_string_test(player->get_id(), iterations, 1);
    workload.query_int_test(player->get_id(), iterations, 1);
    workload.set_data_test(player->get_id(), iterations, 1);
    if (player->get_string(ark::entity_meta_player::name_index()) != "Xiaoming" ||
        player->get_int32(ark::entity_meta_player::gender()) != 11)
    {
        std::_Exit(1);
    }
    const auto id = player->get_id();
    player.reset();
    if (!kernel->destroy_entity(id))
    {
        std::_Exit(1);
    }
    const auto shutdown_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::int64_t tick = 0;
    const auto fail_shutdown = [&](const char* reason) {
        std::cerr << "Benchmark dependency shutdown failed: " << reason
                  << "; state=" << static_cast<int>(manager.get_shutdown_state())
                  << "; phase=" << manager.get_shutdown_failure() << std::endl;
        // Do not unwind retained callbacks/DSOs or report timeout as success.
        std::_Exit(1);
    };
    try
    {
        while (!manager.stop_until(++tick, shutdown_deadline))
        {
            if (const auto error = manager.get_shutdown_exception())
            {
                std::rethrow_exception(error);
            }
            const auto state = manager.get_shutdown_state();
            if ((state != ark::plugin_manager::shutdown_state::draining &&
                    state != ark::plugin_manager::shutdown_state::stopping_acceptance) ||
                !manager.get_shutdown_failure().empty())
            {
                fail_shutdown("terminal shutdown state");
            }
            if (std::chrono::steady_clock::now() >= shutdown_deadline)
            {
                fail_shutdown("cooperative deadline reached");
            }
            // Startup used the synthetic timestamp zero. Keep its clock domain
            // while pumping accepted completions; never jump to wall-clock time.
            if (!manager.update(++tick))
            {
                fail_shutdown("owner update failed during drain");
            }
            std::this_thread::yield();
        }
    }
    catch (const std::exception& error)
    { fail_shutdown(error.what()); }
    catch (...)
    { fail_shutdown("nonstandard exception during drain"); }
    std::cout << (smoke ? "Entity workload smoke passed: 8 iterations per loop; timing disabled"
                        : "Entity workload completed")
              << std::endl;
}
