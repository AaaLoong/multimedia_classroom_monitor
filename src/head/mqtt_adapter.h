#pragma once

#include <mqtt/async_client.h>
#include <QJsonObject>
#include <QObject>
#include <QMutex>
#include <QUuid>

#include <functional>

/*
 * MqttAdapter 是后端和 MQTT Broker 之间的适配层。
 * 它把底层 paho-mqtt-cpp 的连接、订阅、发布和回调封装成 Qt 风格接口，
 * 这样后端业务就不用直接处理 MQTT 的细节。
 */
class MqttAdapter : public QObject
{
    Q_OBJECT
public:
    using StateHandler = std::function<void(const QString &, const QString &, const QString &, const QString &, const QJsonObject &)>;
    using AlertHandler = std::function<void(const QString &, const QString &, const QString &, const QString &)>;

    explicit MqttAdapter(const QString &brokerUrl, QObject *parent = nullptr);

    // 连接 broker 并订阅需要的主题。
    Q_INVOKABLE bool connect();
    // 发布控制命令。
    Q_INVOKABLE bool publishCommand(const QString &roomId, const QString &deviceId, const QJsonObject &payload);
    // 发布报修/告警消息。
    Q_INVOKABLE bool publishReport(const QString &roomId, const QString &deviceId, const QString &message);
    void setStateHandler(StateHandler handler);
    void setAlertHandler(AlertHandler handler);

signals:
    // 设备状态上报后由这个信号通知后端更新数据库。
    void stateSignal(const QString &roomId,
                     const QString &deviceId,
                     const QString &name,
                     const QString &status,
                     const QJsonObject &payload);
    // 告警或报修上报后由这个信号通知后端写入告警表。
    void alertSignal(const QString &roomId, const QString &deviceId, const QString &message, const QString &level);

private:
    QString clientId_;
    QString brokerUrl_;
    mqtt::async_client client_;
    mqtt::connect_options connOpts_;
    QMutex connectMutex_;
    QMutex handlerMutex_;
    StateHandler stateHandler_;
    AlertHandler alertHandler_;

    // 内部只做保活和订阅。
    bool ensureConnected();
    bool subscribeTopics();
};
