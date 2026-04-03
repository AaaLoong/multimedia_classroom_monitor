#include "mqtt_adapter.h"
#include "class_init.h"
#include "topic_defs.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>

/*
 * 这个文件负责把 MQTT 相关细节全部包起来。
 * 上层只需要知道“连上了没有”“能不能发命令”“收到了什么状态”，
 * 而不需要关心具体的回调、主题格式和线程切换。
 */

namespace {

bool commandTraceEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("SIM_COMMAND_TRACE")
        && qEnvironmentVariable("SIM_COMMAND_TRACE").trimmed().compare("false", Qt::CaseInsensitive) != 0
        && qEnvironmentVariable("SIM_COMMAND_TRACE").trimmed() != "0";
    return enabled;
}

QString traceValue(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
        return QStringLiteral("-");
    value.replace('\\', '/');
    value.replace('\n', '_');
    value.replace('\r', '_');
    value.replace('\t', '_');
    value.replace(' ', '_');
    return value;
}

void logMqttTrace(const QString &phase,
                  const QString &result,
                  const QString &topic,
                  const QString &roomId,
                  const QString &deviceId,
                  const QJsonObject &payload,
                  const QString &statusOrLevel,
                  const QString &detail)
{
    if (!commandTraceEnabled())
        return;

    const QString line = QStringLiteral(
                             "cmd_trace ts=%1 source=backend phase=%2 result=%3 topic=%4 room=%5 device=%6 command_id=%7 epoch=%8 version=%9 status=%10 detail=%11")
                             .arg(traceValue(ClassInit::currentShanghaiIsoString()),
                                  traceValue(phase),
                                  traceValue(result),
                                  traceValue(topic),
                                  traceValue(roomId),
                                  traceValue(deviceId),
                                  traceValue(payload.value("applied_command_id").toString()),
                                  traceValue(payload.value("applied_command_epoch").toVariant().toString()),
                                  traceValue(payload.value("applied_device_command_version").toVariant().toString()),
                                  traceValue(statusOrLevel),
                                  traceValue(detail));
    qInfo().noquote() << line;
}

} // namespace

MqttAdapter::MqttAdapter(const QString &brokerUrl, QObject *parent)
    : QObject(parent),
      brokerUrl_(brokerUrl.trimmed()),
      clientId_(QString("cmm_gui_%1").arg(QUuid::createUuid().toString(QUuid::Id128))),
      client_(brokerUrl_.toStdString(), clientId_.toStdString())
{
    // 采用断线自动重连，并且保留会话，减少重复订阅和重复初始化。
    connOpts_.set_clean_session(false);
    connOpts_.set_automatic_reconnect(true);

    client_.set_message_callback([this](mqtt::const_message_ptr msg) {
        const QString topic = QString::fromStdString(msg->get_topic());
        const auto payload = QByteArray::fromStdString(msg->to_string());
        const auto parts = topic.split('/');
        if (topic.contains("/state/") && parts.size() >= 4) {
            const QString roomId = parts[1];
            const QString deviceId = parts[3];
            QString status = "unknown";
            QString name;
            QJsonObject obj;
            const auto doc = QJsonDocument::fromJson(payload);
            if (doc.isObject()) {
                obj = doc.object();
                status = obj.value("status").toString(status);
                name = obj.value("name").toString();
            }
            logMqttTrace(QStringLiteral("mqtt_state_rx"),
                         QStringLiteral("received"),
                         topic,
                         roomId,
                         deviceId,
                         obj,
                         status,
                         QStringLiteral("name=%1").arg(traceValue(name)));
            StateHandler handler;
            {
                QMutexLocker locker(&handlerMutex_);
                handler = stateHandler_;
            }
            if (handler)
                handler(roomId, deviceId, name, status, obj);
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
                logMqttTrace(QStringLiteral("mqtt_alert_rx"),
                             QStringLiteral("received"),
                             topic,
                             roomId,
                             deviceId,
                             obj,
                             level,
                             QStringLiteral("message=%1").arg(traceValue(msgText)));
            } else {
                logMqttTrace(QStringLiteral("mqtt_alert_rx"),
                             QStringLiteral("received"),
                             topic,
                             roomId,
                             deviceId,
                             QJsonObject{},
                             level,
                             QStringLiteral("message=%1").arg(traceValue(msgText)));
            }
            AlertHandler handler;
            {
                QMutexLocker locker(&handlerMutex_);
                handler = alertHandler_;
            }
            if (handler)
                handler(roomId, deviceId, msgText, level);
        }
    });

    // 连接丢失时只打印提示，真正的重连由 paho 的自动重连机制处理。
    client_.set_connection_lost_handler([this](const std::string &cause) {
        qWarning("MQTT connection lost: %s", cause.c_str());
    });
}

void MqttAdapter::setStateHandler(StateHandler handler)
{
    QMutexLocker locker(&handlerMutex_);
    stateHandler_ = std::move(handler);
}

void MqttAdapter::setAlertHandler(AlertHandler handler)
{
    QMutexLocker locker(&handlerMutex_);
    alertHandler_ = std::move(handler);
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
bool MqttAdapter::publishCommand(const QString &roomId, const QString &deviceId, const QJsonObject &payload)
{
    if (!ensureConnected())
        return false;
    const auto topic = Topics::commandTopic(roomId, deviceId);
    try {
        auto msg = mqtt::make_message(topic.toStdString(),
                                      QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString());
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
    obj["timestamp"] = ClassInit::currentShanghaiIsoString();
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
