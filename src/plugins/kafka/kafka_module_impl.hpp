#pragma once

#include "ark/container/array_map.hpp"
#include "ark/kafka/kafka.hpp"
#include "ark/kafka/kafka_module.hpp"
#include "ark/module_base.hpp"

namespace ark {

class kafka_module_impl
    : public module_base<kafka_module>
    , public RdKafka::DeliveryReportCb
    , public RdKafka::EventCb
{
public:
    bool init() override;
    bool update() override;

    bool init_producer(std::string const& brokers, std::string const& topic) override;
    int produce(std::string const& topic, std::string const& payload, void* user_data) override;

protected:
    void dr_cb(RdKafka::Message& message) override;
    void event_cb(RdKafka::Event& event) override;

private:
    array_map<std::string, kafka_producer> producers_;
};

} // namespace ark
