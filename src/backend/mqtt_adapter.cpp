#include "mqtt_adapter.h"
#include "topic_defs.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>

/*
 * 这个文件负责把 MQTT 相关细节全部包起来。
 * 上层只需要知道“连上了没有”“能不能发命令”“收到了什么状态”，
 * 而不需要关心具体的回调、主题格式和线程切换。
 */

MqttAdapter::MqttAdapter(const QString &brokerUrl, QObject *parent)
    : QObject(parent),
      brokerUrl_(brokerUrl.trimmed()),
      clientId_(QString("cmm_gui_%1").arg(QUuid::createUuid().toString(QUuid::Id128))),
      client_(brokerUrl_.toStdString(), clientId_.toStdString())
{
    // 采用断线自动重连，并且保留会话，减少重复订阅和重复初始化。
    connOpts_.set_clean_session(false);
    connOpts_.set_automatic_reconnect(true);

    // MQTT 回调会在别的线程触发，因此这里改用 Qt 的队列连接方式切回对象线程。
    client_.set_message_callback([this](mqtt::const_message_ptr msg) {
        const QString topic = QString::fromStdString(msg->get_topic());
        const auto payload = QByteArray::fromStdString(msg->to_string());
        const auto parts = topic.split('/');
        if (topic.contains("/state/") && parts.size() >= 4) {
            const QString roomId = parts[1];
            const QString deviceId = parts[3];
            QString status = "unknown";
            QString name;
            const auto doc = QJsonDocument::fromJson(payload);
            if (doc.isObject()) {
                const auto obj = doc.object();
                status = obj.value("status").toString(status);
                name = obj.value("name").toString();
            }
            QMetaObject::invokeMethod(this, [this, roomId, deviceId, name, status]() {
                emit stateSignal(roomId, deviceId, name, status);
            }, Qt::QueuedConnection);
        } else if (topic.contains("/alert/") && parts.size() >= 4) {
            const QString roomId = parts[1];
            const QString deviceId = parts[3];
            const auto doc = QJsonDocument::fromJson(payload);
            QString msgText = QString::fromUtf8(payload);
            QString level = "warning";
            if (doc.isObject()) {
                const auto obj = doc.object();
                msgText = obj.value("message").toString(msgText);
                level = obj.value("level").toString(level);
            }
            QMetaObject::invokeMethod(this,
                                      [this, roomId, deviceId, msgText, level]() {
                                          emit alertSignal(roomId, deviceId, msgText, level);
                                      },
                                      Qt::QueuedConnection);
        }
    });

    // 连接丢失时只打印提示，真正的重连由 paho 的自动重连机制处理。
    client_.set_connection_lost_handler([this](const std::string &cause) {
        qWarning("MQTT connection lost: %s", cause.c_str());
    });
}

// 建立与 broker 的连接，并订阅需要监听的主题。
bool MqttAdapter::connect()
{
    QMutexLocker locker(&connectMutex_);
    if (client_.is_connected())
        return subscribeTopics();

    try {
        client_.connect(connOpts_)->wait();
        return subscribeTopics();
    } catch (const mqtt::exception &e) {
        qWarning("MQTT connect failed: %s", e.what());
        return false;
    }
}

// 保证当前对象已经连上 broker。
bool MqttAdapter::ensureConnected()
{
    if (client_.is_connected())
        return true;
    return connect();
}

// 订阅设备状态和告警主题。
bool MqttAdapter::subscribeTopics()
{
    try {
        client_.subscribe("classroom/+/state/+", 1)->wait();
        client_.subscribe("classroom/+/alert/+", 1)->wait();
        return true;
    } catch (const mqtt::exception &e) {
        qWarning("MQTT subscribe failed: %s", e.what());
        return false;
    }
}

// 发布控制命令，供后端转发给仿真端或真实设备端。
bool MqttAdapter::publishCommand(const QString &roomId, const QString &deviceId, const QString &action)
{
    if (!ensureConnected())
        return false;

    QJsonObject obj;
    obj["action"] = action;
    obj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const auto topic = Topics::commandTopic(roomId, deviceId);
    try {
        auto msg = mqtt::make_message(topic.toStdString(),
                                      QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
        msg->set_qos(1);
        client_.publish(msg);
        return true;
    } catch (const mqtt::exception &e) {
        qWarning("MQTT publish command failed: %s", e.what());
        return false;
    }
}

// 发布报修/告警消息，进入后端告警中心。
bool MqttAdapter::publishReport(const QString &roomId, const QString &deviceId, const QString &message)
{
    if (!ensureConnected())
        return false;

    QJsonObject obj;
    obj["device_id"] = deviceId;
    obj["message"] = message;
    obj["level"] = "repair";
    obj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const auto topic = Topics::alertTopic(roomId, deviceId);
    try {
        auto msg = mqtt::make_message(
            topic.toStdString(),
            QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
        msg->set_qos(1);
        client_.publish(msg);
        return true;
    } catch (const mqtt::exception &e) {
        qWarning("MQTT publish report failed: %s", e.what());
        return false;
    }
}

// 发布教室心跳，让后端知道这个教室仍然在线。
bool MqttAdapter::publishHeartbeat(const QString &roomId)
{
    if (!ensureConnected())
        return false;

    QJsonObject obj;
    obj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const auto topic = Topics::heartbeatTopic(roomId);
    try {
        auto msg = mqtt::make_message(
            topic.toStdString(),
            QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
        msg->set_qos(1);
        client_.publish(msg);
        return true;
    } catch (const mqtt::exception &e) {
        qWarning("MQTT heartbeat publish failed: %s", e.what());
        return false;
    }
}
