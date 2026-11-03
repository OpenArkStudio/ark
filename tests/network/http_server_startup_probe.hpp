// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <arknet/http/http_server.hpp>

namespace arknet {

inline http_server* startup_observed_server{};
inline unsigned startup_native_constructions{};

// The production translation unit uses this derived probe only to publish the
// completion of native construction. All start/stop/IO behavior is inherited.
class startup_probe_http_server final : public http_server
{
public:
    startup_probe_http_server(std::size_t initial, std::size_t maximum, std::size_t workers)
        : http_server(initial, maximum, workers) {
        startup_observed_server = this;
        ++startup_native_constructions;
    }
};

} // namespace arknet
