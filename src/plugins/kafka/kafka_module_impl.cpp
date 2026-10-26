#include "ark/kafka/kafka_module_impl.hpp"
#include "ark/kafka/kafka_plugin.hpp"
#include "ark/logger.hpp"
#include "kafka_factory.hpp"

namespace ark {

std::unique_ptr<kafka_module> detail::make_kafka_module() {
    return std::make_unique<kafka_module_impl>();
}

bool kafka_module_impl::init() {
    return true;
}

bool kafka_module_impl::update() {
    for (size_t i = 0; i < producers_.get_count(); ++i)
    {
        producers_[i]->poll();
    }

    return true;
}

bool kafka_module_impl::init_producer(std::string const& brokers, std::string const& topic) {
    if (topic.empty() || brokers.empty())
    {
        return false;
    }

    auto* found = producers_.get_element(topic);
    if (found != nullptr)
    {
        return false;
    }

    kafka_producer* producer = new kafka_producer(brokers, topic, this, this);
    producers_.add_element(topic, producer);
    return true;
}

int kafka_module_impl::produce(std::string const& topic, std::string const& payload, void* user_data) {
    auto* producer = producers_.get_element(topic);
    if (producer == nullptr)
    {
        return false;
    }

    return producer->produce(payload, user_data);
}

void kafka_module_impl::dr_cb(RdKafka::Message& msg) {
    //auto task = reinterpret_cast<TaskInfo*>(msg.msg_opaque());
    auto err = msg.err();
    if (err != RdKafka::ERR_NO_ERROR)
    {
        //        errStr_ = RdKafka::err2str(err);
        //        LOG(WARNING) << "Push msg failed, retry later! msgID=" << task->MsgID << " offset=" << task->Offset
        //                     << " infocode=" << task->Infocode << " err=" << errStr_;
        //        ARK_LOG_WARN();
        //        SendAlertMsg(kAlertErrMsgTooLarge, "Kafka produce error: " + errStr_, 1, 60);
        //        svrCfgs_.PushFinishedTask(task); // 消息推送失败，放回任务列表，等待下次重试。
        return;
    }
}

void kafka_module_impl::event_cb(RdKafka::Event& event) {
    switch (event.type())
    {
    case RdKafka::Event::EVENT_ERROR:
        ARK_LOG_WARN("Kafka error! err=[{}]:{} msg={}", static_cast<int>(event.err()), RdKafka::err2str(event.err()),
            event.str());
        break;
    case RdKafka::Event::EVENT_STATS:
        ARK_LOG_INFO("Kafka stats: {}", event.str());
        break;
    case RdKafka::Event::EVENT_LOG:
        ARK_LOG_INFO("Kafka event log! severity={} facility={} msg={}", static_cast<int>(event.severity()), event.fac(),
            event.str());
        break;
    case RdKafka::Event::EVENT_THROTTLE:
        ARK_LOG_INFO("Kafka throttled time={}ms by {} ! brokerID={}", event.throttle_time(), event.broker_name(),
            event.broker_id());
        break;
    default:
        ARK_LOG_INFO("Kafka event. type={} err=[{}] msg={}", static_cast<int>(event.type()),
            RdKafka::err2str(event.err()), event.str());
        break;
    }
}

//bool AFCConsulModule::LoadConfig()
//{
//    AFXml xml(AFConsulPlugin::GetPluginConf());
//    auto root = xml.GetRootNode();
//    ARK_ASSERT_RET_VAL(root.IsValid(), false);
//
//    auto conf_node = root.FindNode("conf");
//    ARK_ASSERT_RET_VAL(conf_node.IsValid(), false);
//
//    consul_ip_ = conf_node.GetString("ip");
//    consul_port_ = conf_node.GetUint32("port");
//
//    return true;
//}

} // namespace ark
