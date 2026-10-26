// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "ark/net/net_msg.hpp"

namespace ark::guest_room {

inline constexpr msg_id_t join = 60000;
inline constexpr msg_id_t accepted = 60001;
inline constexpr msg_id_t move = 60002;
inline constexpr msg_id_t snapshot = 60003;
inline constexpr msg_id_t error = 60004;
inline constexpr msg_id_t ping = 60005;
inline constexpr msg_id_t pong = 60006;
inline constexpr msg_id_t unsupported = 60007; // Internal dispatch key, never emitted.
inline constexpr std::uint32_t max_body = 4096;
using json = nlohmann::json;

class header final : public msg_header
{
public:
    explicit header(bool client_requests_only = false)
        : client_requests_only_(client_requests_only) {
    }

    std::uint32_t header_length() const override {
        return 12;
    }

    void parse_from_array(const void* source) override {
        const auto* bytes = static_cast<const unsigned char*>(source);
        id_ = (std::uint16_t(bytes[4]) << 8) | bytes[5];
        length_ = (std::uint32_t(bytes[8]) << 24) | (std::uint32_t(bytes[9]) << 16) | (std::uint32_t(bytes[10]) << 8) |
                  bytes[11];
        if (std::memcmp(bytes, "ARK1", 4) != 0 || bytes[6] != 0 || bytes[7] != 0 || length_ > max_body)
        {
            // Net rejects this size before allocating a message or invoking callbacks.
            length_ = max_body + 1;
        }
        if (client_requests_only_ && id_ != join && id_ != move && id_ != ping)
        {
            // Keep unsupported/reserved IDs out of Net's own registration/heartbeat dispatch.
            id_ = unsupported;
        }
    }

    void serialize_to_string(std::string& output) override {
        output.assign("ARK1", 4);
        output.push_back(static_cast<char>(id_ >> 8));
        output.push_back(static_cast<char>(id_));
        output.append(2, '\0');
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            output.push_back(static_cast<char>(length_ >> shift));
        }
    }

    msg_id_t message_id() const override {
        return id_;
    }

    std::uint32_t message_length() const override {
        return length_;
    }

    void set_message_id(msg_id_t value) override {
        id_ = value;
    }

    void set_message_length(std::uint32_t value) override {
        length_ = value;
    }

    msg_header* create() const override {
        return new header(client_requests_only_);
    }

    msg_header* clone() const override {
        return new header(*this);
    }

private:
    bool client_requests_only_;
    msg_id_t id_{0};
    std::uint32_t length_{0};
};

inline bool uint32_value(const json& value, std::uint32_t& result) {
    if (value.is_number_unsigned())
    {
        const auto number = value.get<std::uint64_t>();
        if (number <= std::numeric_limits<std::uint32_t>::max())
        {
            result = static_cast<std::uint32_t>(number);
            return true;
        }
    }
    else if (value.is_number_integer())
    {
        const auto number = value.get<std::int64_t>();
        if (number >= 0 && number <= std::numeric_limits<std::uint32_t>::max())
        {
            result = static_cast<std::uint32_t>(number);
            return true;
        }
    }
    return false;
}

inline bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64)
    {
        return false;
    }
    // JSON parsing validates UTF-8. Reject Unicode C0/C1 control code points too.
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const auto byte = static_cast<unsigned char>(name[i]);
        if (byte < 0x20 || byte == 0x7f ||
            (byte == 0xc2 && i + 1 < name.size() && static_cast<unsigned char>(name[i + 1]) >= 0x80 &&
                static_cast<unsigned char>(name[i + 1]) <= 0x9f))
        {
            return false;
        }
    }
    return true;
}

} // namespace ark::guest_room
