#pragma once

#include <exception>
#include <mutex>
#include <string>

#include <librdkafka/rdkafkacpp.h>

#include "ark/noncopyable.hpp"
#include "ark/string_utils.hpp"

namespace ark {

using kafka_err_code = RdKafka::ErrorCode;

namespace {

inline void set_config(RdKafka::Conf* conf, const std::string& key, const std::string& value) {
    std::string error;
    if (conf->set(key, value, error) == RdKafka::Conf::CONF_OK) {
        return;
    }
    throw std::runtime_error("Failed to set '" + key + "' to '" + value + "'! err=" + error);
}

inline void set_config(RdKafka::Conf* conf, const std::string& key, RdKafka::EventCb* cb_obj) {
    std::string error;
    if (conf->set(key, cb_obj, error) == RdKafka::Conf::CONF_OK) {
        return;
    }
    throw std::runtime_error("Failed to set '" + key + "'! err=" + error);
}

inline void set_config(RdKafka::Conf* conf, const std::string& key, RdKafka::OffsetCommitCb* cb_obj) {
    std::string error;
    if (conf->set(key, cb_obj, error) == RdKafka::Conf::CONF_OK) {
        return;
    }
    throw std::runtime_error("Failed to set '" + key + "'! err=" + error);
}

inline void set_config(RdKafka::Conf* conf, const std::string& key, RdKafka::DeliveryReportCb* cb_obj) {
    std::string error;
    if (conf->set(key, cb_obj, error) == RdKafka::Conf::CONF_OK) {
        return;
    }
    throw std::runtime_error("Failed to set '" + key + "'! err=" + error);
}

} // namespace

/**
 * @brief RdKafka::KafkaConsumer的简单封装
 */
class kafka_consumer {
public:
    using message = RdKafka::Message;

public:
    /**
	 * @brief 构造函数
	 * @param brokers 逗号分隔的ip:port列表
	 * @param groupID
	 * @param topics 逗号分隔的topic列表
	 */
    kafka_consumer(const std::string& brokers, const std::string& group_id, const std::string& topics,
        RdKafka::OffsetCommitCb* offset_cb = nullptr, RdKafka::EventCb* evcb = nullptr) {
        if (brokers.empty() || group_id.empty() || topics.empty()) {
            throw std::runtime_error("Invalid parameters!");
        }

        auto conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        std::unique_ptr<RdKafka::Conf> conf_for_auto_del(conf); // for auto delete when throwing exception

        set_config(conf, "metadata.broker.list", brokers);
        set_config(conf, "group.id", group_id);
        set_config(conf, "auto.offset.reset", "smallest");
        set_config(conf, "enable.auto.commit", "false");
        set_config(conf, "enable.auto.offset.store", "false");
        if (evcb) {
            set_config(conf, "event_cb", evcb);
        }
        if (offset_cb) {
            set_config(conf, "offset_commit_cb", offset_cb);
        }

        std::string error;
        std::unique_ptr<RdKafka::KafkaConsumer> consumer(RdKafka::KafkaConsumer::create(conf, error));
        if (!consumer) {
            throw std::runtime_error("Failed to create consumer! err=" + error);
        }

        std::vector<std::string> all_topics;
        string_utils::split(all_topics, topics, ',');
        auto err = consumer->subscribe(all_topics);
        if (err) {
            throw std::runtime_error("Failed to subscribe to '" + topics + "'! err=" + RdKafka::err2str(err));
        }

        consumer_ = consumer.release();
    }

    ~kafka_consumer() {
        //consumer_->close(); // 会卡死，先注释掉，后续看情况调整
        delete consumer_;
    }

    /**
	 * @brief 获取一条消息或者错误事件,触发回调函数
	 * @remark 使用delete释放消息内存
	 * @returns One of:
	 *  - proper message (Message::err() is ERR_NO_ERROR)
	 *  - error event (Message::err() is != ERR_NO_ERROR)
	 *  - timeout due to no message or event in timeoutMS
	 *    (RdKafka::Message::err() is ERR__TIMED_OUT)
	 */
    message* consume(int timeout_ms) {
        return consumer_->consume(timeout_ms);
    }

    /**
	 * @brief 异步提交Offset
	 */
    void async_commit(message* msg) {
        consumer_->commitAsync(msg);
    }

private:
    RdKafka::KafkaConsumer* consumer_;
};

/**
 * @brief RdKafka::Producer的简单封装
 */
class kafka_producer {
public:
    /**
	 * @brief 构造函数
	 * @param brokers 逗号分隔的ip:port列表
	 * @param topic
	 */
    kafka_producer(const std::string& brokers, const std::string& topic,
        RdKafka::DeliveryReportCb* delivery_report_cb = nullptr, RdKafka::EventCb* event_cb = nullptr) {
        auto conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        std::unique_ptr<RdKafka::Conf> conf_for_auto_del(conf); // for auto delete when throwing exception

        set_config(conf, "metadata.broker.list", brokers);
        if (event_cb != nullptr) {
            set_config(conf, "event_cb", event_cb);
        }
        if (delivery_report_cb != nullptr) {
            set_config(conf, "dr_cb", delivery_report_cb);
        }

        std::string error;
        auto tmp_producer = RdKafka::Producer::create(conf, error);
        if (!tmp_producer) {
            throw std::runtime_error("Failed to create producer! err=" + error);
        }
        std::unique_ptr<RdKafka::Producer> producer(tmp_producer); // for auto delete when throwing exception

        topic_ = RdKafka::Topic::create(tmp_producer, topic, nullptr, error);
        if (topic_ == nullptr) {
            throw std::runtime_error("Failed to create topic! topic=" + topic + " error=" + error);
        }

        producer_ = producer.release();
    }

    ~kafka_producer() {
        producer_->flush(10000);
        delete topic_;
        delete producer_;
    }

    /**
	 * @brief 异步往kafka broker发送一条消息
	 * @param payload 发送给kafka broker的消息内容
	 * @param msgOpaque 调用送达通知回调函数dr_cb时，传递给该函数
	 * @return 表示成功或者失败的错误码，取值如下：
	 *  - ERR_NO_ERROR           - 成功写入队列
	 *  - ERR__QUEUE_FULL        - 队列满：queue.buffering.max.message
	 *  - ERR_MSG_SIZE_TOO_LARGE - 消息太大：messages.max.bytes
	 *  - ERR__UNKNOWN_PARTITION
	 *  - ERR__UNKNOWN_TOPIC
	 */
    kafka_err_code produce(const std::string& payload, void* msg_opaque = nullptr) {
        // 只有当送达通知回调函数返回之后，该消息才会被移出队列。
        // 所以produce返回队列满时，不能马上重试，只能在定期调用Poll()函数后再重试。
        return producer_->produce(topic_, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
            const_cast<char*>(payload.data()), payload.size(), nullptr, msg_opaque);
    }

    /**
	* @brief 同步往kafka broker发送一条消息
	* @param payload 发送给kafka broker的消息内容
	* @return 表示成功或者失败的错误码
	*/
    kafka_err_code sync_produce(const std::string& payload) {
        kafka_err_code err = static_cast<kafka_err_code>(-12345);

        producer_->produce(topic_, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
            const_cast<char*>(payload.data()), payload.size(), nullptr, &err);
        while (err == static_cast<kafka_err_code>(-12345)) {
            poll(1000);
        }

        return err;
    }

    /**
	 * @brief 触发已完成事件的回调函数。程序必须定期调用该函数，以触发事件回调函数。
	 * @param timeoutMS 函数等待事件的时间（毫秒），0表示不等待，-1表示永久等待
	 *
	 * 事件包括：
	 *   - 送达通知，回调dr_cb
	 *   - 其他事件，调用event_cb
	 * @returns 返回事件个数
	 */
    int poll(int timeout_ms = 0) {
        return producer_->poll(timeout_ms);
    }

private:
    RdKafka::Producer* producer_;
    RdKafka::Topic* topic_;
};

/**
* @brief KafkaProducer对象池，线程安全。
*/
class kafka_producer_pool : private noncopyable {
public:
    /**
	* @brief 构造Oracle操作对象
	* @throw occi::SQLException
	* @note 多线程环境下，使用者必须保证drcb和evcb是线程安全的
	*/
    kafka_producer_pool(size_t max_pooled_producers, const std::string& brokers, const std::string& topic,
        RdKafka::DeliveryReportCb* delivery_report_cb = nullptr, RdKafka::EventCb* event_cb = nullptr)
        : max_pooled_producers_(max_pooled_producers)
        , brokers_(brokers)
        , topic_(topic)
        , delivery_report_cb_(delivery_report_cb)
        , event_cb_(event_cb) {
    }

    ~kafka_producer_pool() {
        for (auto cli : pooled_producers_) {
            delete cli;
        }
    }

    /**
	* @brief 获取KafkaProducer对象，使用完毕后，要调用Put接口返还。
	*/
    kafka_producer* get() {
        lock_.lock();
        if (!pooled_producers_.empty()) {
            auto cli = pooled_producers_.front();
            pooled_producers_.pop_front();
            lock_.unlock();
            return cli;
        }
        lock_.unlock();

        try {
            return new kafka_producer(brokers_, topic_, delivery_report_cb_, event_cb_);
        } catch (...) {
        }

        return nullptr;
    }

    /**
	* @brief 返还通过Get接口获取的KafkaProducer对象。
	*/
    void put(kafka_producer* producer) {
        lock_.lock();
        if (pooled_producers_.size() < max_pooled_producers_) {
            pooled_producers_.emplace_back(producer);
            lock_.unlock();
        } else {
            lock_.unlock();
            delete producer;
        }
    }

private:
    const size_t max_pooled_producers_;
    const std::string brokers_;
    const std::string topic_;
    RdKafka::DeliveryReportCb* const delivery_report_cb_;
    RdKafka::EventCb* const event_cb_;

    std::mutex lock_;
    std::list<kafka_producer*> pooled_producers_;
};

} // namespace ark