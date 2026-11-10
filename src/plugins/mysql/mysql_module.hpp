// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <arknet/mysql/pool.hpp>

#include "ark/module.hpp"
#include "ark/task.hpp"

namespace ark {

struct mysql_options {
    arknet::mysql::pool_options pool;
    arknet::mysql::query_options queries;
    std::size_t max_operations{64};
    std::size_t max_input_bytes{1024 * 1024};
    std::size_t max_transactions{16};
};

struct mysql_reply {
    arknet::mysql::query_result result;
    std::optional<arknet::mysql::connection_result> statement_close;
    std::exception_ptr statement_close_exception;

    explicit operator bool() const noexcept {
        return bool(result);
    }
};

enum class mysql_transaction_outcome {
    not_started,
    active,
    committed,
    rolled_back,
    unconfirmed
};

struct mysql_transaction_options {
    std::chrono::milliseconds timeout{5000};
    std::chrono::milliseconds cleanup_timeout{1000};
};

// Tokens own no plugin object and are valid only for the issuing module instance.
// An unused transaction rolls back at its original deadline, never auto-commits.
using mysql_transaction_id = std::uint64_t;

struct mysql_transaction_status {
    mysql_transaction_outcome outcome{mysql_transaction_outcome::not_started};
    std::optional<arknet::mysql::connection_errc> rollback_code;
    bool rollback_outcome_unknown{};
    std::exception_ptr cleanup_exception;
    std::exception_ptr cancellation_exception;
    std::exception_ptr lease_return_exception;
    bool discard_requested{};
};

struct mysql_transaction_reply {
    arknet::mysql::query_result result;
    std::optional<arknet::mysql::connection_result> statement_close;
    std::exception_ptr statement_close_exception;
    std::optional<arknet::mysql::query_result> rollback;
    std::exception_ptr initiation_exception;
    mysql_transaction_status transaction_status;
    mysql_transaction_id transaction{};

    explicit operator bool() const noexcept {
        return !initiation_exception && bool(result);
    }
};

// Configure and invoke on the framework thread. Await from manager-owned task
// groups: completion resumes on the shared business executor during update().
// Inputs and native results own storage. Local admission/C++ failures throw;
// server/transport failures remain in the native result.
class mysql_module : public module {
public:
    virtual bool configure(std::string name, mysql_options options) = 0;
    virtual task<mysql_reply> query(
        std::string name, std::string sql, std::chrono::milliseconds timeout = std::chrono::seconds(5)) = 0;
    virtual task<mysql_reply> execute(std::string name, std::string sql,
        std::vector<arknet::mysql::field_value> parameters,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) = 0;
    virtual task<mysql_transaction_reply> begin(std::string name, mysql_transaction_options options = {}) = 0;
    virtual task<mysql_transaction_reply> query(mysql_transaction_id transaction, std::string sql) = 0;
    virtual task<mysql_transaction_reply> execute(
        mysql_transaction_id transaction, std::string sql, std::vector<arknet::mysql::field_value> parameters) = 0;
    virtual task<mysql_transaction_reply> commit(mysql_transaction_id transaction) = 0;
    virtual task<mysql_transaction_reply> rollback(mysql_transaction_id transaction) = 0;
};

} // namespace ark
