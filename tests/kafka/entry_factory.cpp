// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>

#include <ark/kafka/kafka_plugin.hpp>
#include <ark/module_base.hpp>
#include "kafka_factory.hpp"

namespace {

unsigned created{};
unsigned destroyed{};
unsigned updated{};

class entry_kafka final : public ark::module_base<ark::kafka_module>
{
public:
    entry_kafka() {
        ++created;
    }

    ~entry_kafka() override {
        ++destroyed;
    }

    bool init() override {
        return ark::kafka_plugin::get_plugin_conf() == "conf/entry-probe.toml";
    }

    bool update() override {
        ++updated;
        return true;
    }

    bool init_producer(const std::string&, const std::string&) override {
        return false;
    }

    int produce(const std::string&, const std::string&, void*) override {
        return 0;
    }
};

} // namespace

namespace ark::detail {

std::unique_ptr<kafka_module> make_kafka_module() {
    return std::make_unique<entry_kafka>();
}

} // namespace ark::detail

ARK_EXPORT_FUNC unsigned kafka_entry_events(unsigned event) {
    switch (event)
    {
    case 0:
        return created;
    case 1:
        return destroyed;
    default:
        return updated;
    }
}
