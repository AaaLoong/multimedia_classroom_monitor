#include "class_init.h"
#include "device.h"
#include "topic_defs.h"

#include <mqtt/async_client.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>
#include <vector>

/*
 * 这个文件是设备仿真端的主程序。
 * 它负责模拟教室设备接收命令、上报状态、发送心跳、触发告警，并支持多教室同步。
 * 对初学者来说，可以把它理解成“虚拟设备控制台”。
 */

// 把命令行参数里的业务参数筛出来，过滤掉 --headless 这种模式开关。
static QStringList positionalArgs(const QStringList &args)
{
    QStringList filtered;
    if (!args.isEmpty())
        filtered.append(args.first());

    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == "--headless")
            continue;
        filtered.append(args.at(i));
    }
    return filtered;
}

// 把后端基础地址清理成统一格式，避免重复斜杠。
static QString normalizeHttpBaseUrl(QString baseUrl)
{
    baseUrl = baseUrl.trimmed();
    while (baseUrl.endsWith('/'))
        baseUrl.chop(1);
    return baseUrl;
}

// 生成一个带进程号和时间戳的 MQTT 客户端 ID，避免多个仿真实例冲突。
static QString buildSimulatorClientId()
{
    return QString("cmm_device_sim_gui_%1_%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
}

// 单个教室在仿真端的运行状态。
struct SimRoomState {
    QString roomId;
    std::vector<Device> devices;
    QSet<QString> forcedOfflineDevices;
    bool heartbeatPaused = false;
    bool randomDelayEnabled = false;
    int lastDelayMs = 0;
};

/*
 * DeviceSimClient 是仿真端的核心对象。
 * 它一边管理当前有哪些教室，一边接收 MQTT 命令并驱动设备状态变化。
 */
class DeviceSimClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString roomId READ roomId NOTIFY currentRoomIdChanged)
    Q_PROPERTY(QStringList roomIds READ roomIds NOTIFY roomIdsChanged)
    Q_PROPERTY(QString currentRoomId READ currentRoomId WRITE setCurrentRoomId NOTIFY currentRoomIdChanged)
    Q_PROPERTY(QStringList deviceIds READ deviceIds NOTIFY deviceIdsChanged)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId WRITE setSelectedDeviceId NOTIFY selectedDeviceIdChanged)
    Q_PROPERTY(bool heartbeatPaused READ heartbeatPaused NOTIFY heartbeatPausedChanged)
    Q_PROPERTY(bool randomDelayEnabled READ randomDelayEnabled NOTIFY randomDelayEnabledChanged)
    Q_PROPERTY(int lastDelayMs READ lastDelayMs NOTIFY lastDelayChanged)
    Q_PROPERTY(bool roomOperationBusy READ roomOperationBusy NOTIFY roomOperationBusyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
public:
    // 构造函数里完成 MQTT、HTTP 和定时器的初始化。
    DeviceSimClient(const QString &initialRoomId,
                    const QString &broker,
                    const QString &backendBaseUrl,
                    QObject *parent = nullptr)
        : QObject(parent),
          preferredRoomId_(initialRoomId.trimmed()),
          backendBaseUrl_(normalizeHttpBaseUrl(backendBaseUrl)),
          brokerUrl_(broker.trimmed()),
          client_(brokerUrl_.toStdString(), buildSimulatorClientId().toStdString())
    {
        connOpts_.set_clean_session(true);
        connOpts_.set_automatic_reconnect(true);

        client_.set_message_callback([this](mqtt::const_message_ptr msg) {
            handleMessage(QString::fromStdString(msg->get_topic()),
                          QByteArray::fromStdString(msg->to_string()));
        });

        try {
            client_.connect(connOpts_)->wait();
            client_.subscribe("classroom/+/cmd/+", 1)->wait();
            mqttReady_ = true;
            setStatusText("MQTT 已连接，等待教室接入");
        } catch (const mqtt::exception &e) {
            setStatusText(QString("MQTT 连接失败: %1").arg(e.what()));
        }

        connect(&heartbeatTimer_, &QTimer::timeout, this, &DeviceSimClient::sendHeartbeat);
        heartbeatTimer_.start(30000);

        // 如果配置了后端地址，就周期性同步教室列表；否则只启动单教室本地模式。
        if (!backendBaseUrl_.isEmpty()) {
            connect(&roomRefreshTimer_, &QTimer::timeout, this, &DeviceSimClient::refreshRooms);
            roomRefreshTimer_.start(3000);
            QTimer::singleShot(0, this, &DeviceSimClient::refreshRooms);
        } else {
            ensureRoom(preferredRoomId_.isEmpty() ? QStringLiteral("A101") : preferredRoomId_);
        }

        sendHeartbeat();
    }

    // 析构时断开 MQTT，保证进程退出时更干净。
    ~DeviceSimClient() override
    {
        try {
            client_.disconnect()->wait();
        } catch (...) {
        }
    }

    QString roomId() const { return currentRoomId_; }

    QStringList roomIds() const
    {
        return rooms_.keys();
    }

    QString currentRoomId() const { return currentRoomId_; }

    QStringList deviceIds() const
    {
        QStringList ids;
        const auto *room = findRoom(currentRoomId_);
        if (!room)
            return ids;

        for (const auto &device : room->devices)
            ids.append(device.id);
        return ids;
    }

    QString selectedDeviceId() const { return selectedDeviceId_; }

    bool heartbeatPaused() const
    {
        const auto *room = findRoom(currentRoomId_);
        return room ? room->heartbeatPaused : false;
    }

    bool randomDelayEnabled() const
    {
        const auto *room = findRoom(currentRoomId_);
        return room ? room->randomDelayEnabled : false;
    }

    int lastDelayMs() const
    {
        const auto *room = findRoom(currentRoomId_);
        return room ? room->lastDelayMs : 0;
    }

    bool roomOperationBusy() const { return roomOperationBusy_; }
    QString statusText() const { return statusText_; }

    void setCurrentRoomId(const QString &roomId)
    {
        if (roomId.isEmpty() || !rooms_.contains(roomId) || currentRoomId_ == roomId)
            return;

        currentRoomId_ = roomId;
        const QStringList ids = deviceIds();
        const QString nextDeviceId = ids.isEmpty() ? QString() : ids.first();
        const bool selectedChanged = selectedDeviceId_ != nextDeviceId;
        selectedDeviceId_ = nextDeviceId;

        emit currentRoomIdChanged();
        emit deviceIdsChanged();
        if (selectedChanged)
            emit selectedDeviceIdChanged();
        emit heartbeatPausedChanged();
        emit randomDelayEnabledChanged();
        emit lastDelayChanged();
    }

    void setSelectedDeviceId(const QString &deviceId)
    {
        if (selectedDeviceId_ == deviceId)
            return;
        selectedDeviceId_ = deviceId;
        emit selectedDeviceIdChanged();
    }

    // 从后端拉取已注册教室列表，并同步到本地会话。
    Q_INVOKABLE void refreshRooms()
    {
        if (backendBaseUrl_.isEmpty() || refreshInFlight_)
            return;

        refreshInFlight_ = true;
        QNetworkRequest request(QUrl(backendBaseUrl_ + "/classrooms"));
        auto *reply = http_.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            refreshInFlight_ = false;
            const QByteArray payload = reply->readAll();
            const QString error = reply->error() == QNetworkReply::NoError
                ? QString()
                : reply->errorString();
            reply->deleteLater();

            if (!error.isEmpty()) {
                setStatusText(QString("教室列表刷新失败: %1").arg(error));
                if (rooms_.isEmpty())
                    ensureRoom(preferredRoomId_.isEmpty() ? QStringLiteral("A101") : preferredRoomId_);
                return;
            }

            QStringList registeredRooms;
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (doc.isObject()) {
                const QJsonArray classrooms = doc.object().value("classrooms").toArray();
                for (const auto &item : classrooms) {
                    if (item.isObject()) {
                        const QString roomId = item.toObject().value("room_id").toString().trimmed();
                        if (!roomId.isEmpty())
                            registeredRooms.append(roomId);
                    } else if (item.isString()) {
                        registeredRooms.append(item.toString().trimmed());
                    }
                }
            } else if (doc.isArray()) {
                for (const auto &item : doc.array()) {
                    if (item.isString())
                        registeredRooms.append(item.toString().trimmed());
                }
            }

            registeredRooms.removeAll(QString());
            syncRegisteredRooms(registeredRooms);
        });
    }

    // 取消当前选中教室的注册，并清理本地仿真状态。
    Q_INVOKABLE void unregisterCurrentRoom()
    {
        if (roomOperationBusy_)
            return;
        if (backendBaseUrl_.isEmpty()) {
            setStatusText("当前未配置后端地址，无法取消注册");
            return;
        }
        if (currentRoomId_.isEmpty()) {
            setStatusText("当前没有可取消注册的教室");
            return;
        }

        roomOperationBusy_ = true;
        emit roomOperationBusyChanged();

        const QString targetRoomId = currentRoomId_;
        const QString encodedRoomId = QString::fromUtf8(QUrl::toPercentEncoding(targetRoomId));
        QNetworkRequest request(QUrl(QString("%1/classrooms/%2/unregister").arg(backendBaseUrl_, encodedRoomId)));
        auto *reply = http_.post(request, QByteArray());
        connect(reply, &QNetworkReply::finished, this, [this, reply, targetRoomId]() {
            const QByteArray payload = reply->readAll();
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorMessage;
            if (reply->error() != QNetworkReply::NoError) {
                errorMessage = reply->errorString();
            } else if (httpStatus < 200 || httpStatus >= 300) {
                errorMessage = QString("服务错误(%1)").arg(httpStatus);
            }

            if (doc.isObject() && doc.object().contains("error")) {
                const QString backendError = doc.object().value("error").toString();
                if (!backendError.isEmpty())
                    errorMessage = httpStatus >= 400 && httpStatus < 600
                        ? QString("服务错误(%1): %2").arg(httpStatus).arg(backendError)
                        : backendError;
            }

            reply->deleteLater();
            roomOperationBusy_ = false;
            emit roomOperationBusyChanged();

            if (!errorMessage.isEmpty()) {
                setStatusText(QString("教室 %1 取消注册失败: %2").arg(targetRoomId, errorMessage));
                return;
            }

            removeRoom(targetRoomId);
            setStatusText(QString("教室 %1 已取消注册").arg(targetRoomId));
            refreshRooms();
        });
    }

    // 模拟心跳中断/恢复。
    Q_INVOKABLE void toggleHeartbeat()
    {
        auto *room = ensureRoom(currentRoomId_);
        if (!room) {
            setStatusText("当前没有可控制的教室");
            return;
        }

        room->heartbeatPaused = !room->heartbeatPaused;
        emit heartbeatPausedChanged();
        setStatusText(room->heartbeatPaused
            ? QString("教室 %1 链路异常，心跳已停止").arg(room->roomId)
            : QString("教室 %1 链路恢复，心跳已恢复").arg(room->roomId));
    }

    // 开关随机延迟模式，用来模拟网络抖动和设备慢响应。
    Q_INVOKABLE void toggleRandomDelay()
    {
        auto *room = ensureRoom(currentRoomId_);
        if (!room) {
            setStatusText("当前没有可控制的教室");
            return;
        }

        room->randomDelayEnabled = !room->randomDelayEnabled;
        emit randomDelayEnabledChanged();
        setStatusText(room->randomDelayEnabled
            ? QString("教室 %1 随机延迟模式已开启").arg(room->roomId)
            : QString("教室 %1 随机延迟模式已关闭").arg(room->roomId));
    }

    // 让当前选中的设备进入离线或恢复状态。
    Q_INVOKABLE void toggleSelectedDeviceOffline()
    {
        auto *room = ensureRoom(currentRoomId_);
        if (!room) {
            setStatusText("当前没有可控制的教室");
            return;
        }
        if (selectedDeviceId_.isEmpty()) {
            setStatusText("未选择设备");
            return;
        }

        if (room->forcedOfflineDevices.contains(selectedDeviceId_)) {
            room->forcedOfflineDevices.remove(selectedDeviceId_);
            setDeviceStatus(room->roomId, selectedDeviceId_, DeviceStatus::Off);
            setStatusText(QString("教室 %1 设备 %2 通信恢复，状态已回到关机")
                              .arg(room->roomId, selectedDeviceId_));
        } else {
            room->forcedOfflineDevices.insert(selectedDeviceId_);
            setDeviceStatus(room->roomId, selectedDeviceId_, DeviceStatus::Offline);
            publishAlert(room->roomId, selectedDeviceId_, "设备通信中断，已离线", "warning");
            setStatusText(QString("教室 %1 设备 %2 通信中断，已离线")
                              .arg(room->roomId, selectedDeviceId_));
        }
    }

signals:
    void roomIdsChanged();
    void currentRoomIdChanged();
    void deviceIdsChanged();
    void selectedDeviceIdChanged();
    void heartbeatPausedChanged();
    void randomDelayEnabledChanged();
    void lastDelayChanged();
    void roomOperationBusyChanged();
    void statusTextChanged();

private:
    QString preferredRoomId_;
    QString backendBaseUrl_;
    QString brokerUrl_;
    QNetworkAccessManager http_;
    mqtt::async_client client_;
    mqtt::connect_options connOpts_;
    QTimer heartbeatTimer_;
    QTimer roomRefreshTimer_;
    Randomizer rand_;
    QMap<QString, SimRoomState> rooms_;
    QString currentRoomId_;
    QString selectedDeviceId_;
    bool mqttReady_ = false;
    bool refreshInFlight_ = false;
    bool roomOperationBusy_ = false;
    QString statusText_;

    // 统一更新状态文本，避免重复触发信号。
    void setStatusText(const QString &text)
    {
        if (statusText_ == text)
            return;
        statusText_ = text;
        emit statusTextChanged();
    }

    // 查找某个教室是否已经在本地仿真会话中。
    SimRoomState *findRoom(const QString &roomId)
    {
        if (roomId.isEmpty())
            return nullptr;
        auto it = rooms_.find(roomId);
        return it == rooms_.end() ? nullptr : &it.value();
    }

    // 只读版本的查找接口，适合只读逻辑。
    const SimRoomState *findRoom(const QString &roomId) const
    {
        if (roomId.isEmpty())
            return nullptr;
        auto it = rooms_.constFind(roomId);
        return it == rooms_.cend() ? nullptr : &it.value();
    }

    // 如果本地还没有这个教室，就创建一个默认仿真会话。
    SimRoomState *ensureRoom(const QString &roomId)
    {
        const QString normalizedRoomId = roomId.trimmed();
        if (normalizedRoomId.isEmpty())
            return nullptr;

        auto it = rooms_.find(normalizedRoomId);
        if (it != rooms_.end())
            return &it.value();

        SimRoomState room;
        room.roomId = normalizedRoomId;
        const QList<Device> defaults = defaultDevicesForRoom(normalizedRoomId);
        room.devices = std::vector<Device>(defaults.begin(), defaults.end());

        const bool hadRooms = !rooms_.isEmpty();
        auto inserted = rooms_.insert(normalizedRoomId, room);
        emit roomIdsChanged();

        if (!hadRooms || currentRoomId_.isEmpty()) {
            setCurrentRoomId(normalizedRoomId);
        } else if (currentRoomId_ == normalizedRoomId) {
            emit deviceIdsChanged();
        }

        if (mqttReady_) {
            publishAllStates(normalizedRoomId);
            sendHeartbeatForRoom(normalizedRoomId);
        }

        setStatusText(QString("已接入教室 %1 的仿真控制").arg(normalizedRoomId));
        return &inserted.value();
    }

    // 移除一个教室，同时释放当前选中教室和设备选择状态。
    void removeRoom(const QString &roomId)
    {
        if (!rooms_.contains(roomId))
            return;

        const bool wasCurrentRoom = currentRoomId_ == roomId;
        rooms_.remove(roomId);
        emit roomIdsChanged();

        if (rooms_.isEmpty()) {
            currentRoomId_.clear();
            if (!selectedDeviceId_.isEmpty()) {
                selectedDeviceId_.clear();
                emit selectedDeviceIdChanged();
            }
            if (wasCurrentRoom) {
                emit currentRoomIdChanged();
                emit deviceIdsChanged();
                emit heartbeatPausedChanged();
                emit randomDelayEnabledChanged();
                emit lastDelayChanged();
            }
            return;
        }

        if (wasCurrentRoom) {
            currentRoomId_.clear();
            setCurrentRoomId(rooms_.firstKey());
        }
    }

    // 把后端返回的注册教室列表同步成本地可操作的仿真教室列表。
    void syncRegisteredRooms(const QStringList &registeredRooms)
    {
        QStringList normalizedRooms = registeredRooms;
        normalizedRooms.removeAll(QString());
        normalizedRooms.removeDuplicates();

        if (normalizedRooms.isEmpty()) {
            if (rooms_.isEmpty())
                ensureRoom(preferredRoomId_.isEmpty() ? QStringLiteral("A101") : preferredRoomId_);
            return;
        }

        for (const auto &roomId : normalizedRooms)
            ensureRoom(roomId);

        const QSet<QString> targetSet(normalizedRooms.cbegin(), normalizedRooms.cend());
        const QStringList existingRooms = rooms_.keys();
        for (const auto &roomId : existingRooms) {
            if (!targetSet.contains(roomId))
                removeRoom(roomId);
        }

        if (currentRoomId_.isEmpty() && !normalizedRooms.isEmpty())
            setCurrentRoomId(normalizedRooms.first());
    }

    // 在某个教室里查找指定设备。
    Device *findDevice(const QString &roomId, const QString &deviceId)
    {
        auto *room = findRoom(roomId);
        if (!room)
            return nullptr;

        for (auto &device : room->devices) {
            if (device.id == deviceId)
                return &device;
        }
        return nullptr;
    }

    // 根据当前模式计算下一次命令的随机执行延迟。
    int nextCommandDelayMs(SimRoomState &room)
    {
        if (!room.randomDelayEnabled) {
            room.lastDelayMs = rand_.between(500, 1800);
        } else {
            const int maxDelay = 2147483000;
            const int shift = rand_.between(0, 27);
            qint64 lower = 8;
            lower <<= shift;
            if (lower > maxDelay)
                lower = maxDelay;
            const qint64 upper = qMin<qint64>(maxDelay, lower * 2 - 1);
            room.lastDelayMs = rand_.between(int(lower), int(upper));
        }

        if (room.roomId == currentRoomId_)
            emit lastDelayChanged();
        return room.lastDelayMs;
    }

    // 修改设备状态并立即通过 MQTT 上报。
    void setDeviceStatus(const QString &roomId, const QString &deviceId, DeviceStatus status)
    {
        auto *device = findDevice(roomId, deviceId);
        if (!device)
            return;
        device->status = status;
        publishState(roomId, *device);
    }

    // 发送单台设备的状态上报。
    void publishState(const QString &roomId, const Device &device)
    {
        if (!mqttReady_)
            return;

        QJsonObject obj = device.toJson();
        obj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        try {
            auto msg = mqtt::make_message(
                Topics::stateTopic(roomId, device.id).toStdString(),
                QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
            msg->set_qos(1);
            client_.publish(msg);
        } catch (const mqtt::exception &e) {
            setStatusText(QString("教室 %1 状态上报失败: %2").arg(roomId, e.what()));
        }
    }

    // 把当前教室里的全部设备状态都上报一次。
    void publishAllStates(const QString &roomId)
    {
        const auto *room = findRoom(roomId);
        if (!room)
            return;

        for (const auto &device : room->devices)
            publishState(roomId, device);
    }

    // 发布一条告警消息。
    void publishAlert(const QString &roomId,
                      const QString &deviceId,
                      const QString &message,
                      const QString &level = "warning")
    {
        if (!mqttReady_)
            return;

        QJsonObject obj;
        obj["device_id"] = deviceId;
        obj["message"] = message;
        obj["level"] = level;
        obj["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        try {
            auto msg = mqtt::make_message(
                Topics::alertTopic(roomId, deviceId).toStdString(),
                QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
            msg->set_qos(1);
            client_.publish(msg);
        } catch (const mqtt::exception &e) {
            setStatusText(QString("教室 %1 告警上报失败: %2").arg(roomId, e.what()));
        }
    }

    // 处理 MQTT 下发的命令消息。
    void handleMessage(const QString &topic, const QByteArray &payload)
    {
        const auto parts = topic.split('/');
        if (parts.size() < 4)
            return;

        const QString roomId = parts[1].trimmed();
        const QString deviceId = parts[3].trimmed();
        auto *room = ensureRoom(roomId);
        if (!room)
            return;

        if (room->forcedOfflineDevices.contains(deviceId)) {
            setStatusText(QString("教室 %1 设备 %2 当前离线，无响应").arg(roomId, deviceId));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        const QString action = doc.object().value("action").toString();
        auto *device = findDevice(roomId, deviceId);
        if (!device)
            return;

        if (action == "power_on") {
            startDevice(roomId, deviceId);
        } else if (action == "power_off") {
            const int delay = nextCommandDelayMs(*room);
            setStatusText(QString("教室 %1 设备 %2 收到关机指令，预计 %3 ms 执行")
                              .arg(roomId, deviceId)
                              .arg(delay));
            QTimer::singleShot(delay, this, [this, roomId, deviceId]() {
                auto *targetRoom = findRoom(roomId);
                auto *targetDevice = findDevice(roomId, deviceId);
                if (!targetRoom || !targetDevice)
                    return;
                if (targetRoom->forcedOfflineDevices.contains(deviceId)) {
                    targetDevice->status = DeviceStatus::Offline;
                    publishState(roomId, *targetDevice);
                    return;
                }
                targetDevice->status = DeviceStatus::Off;
                publishState(roomId, *targetDevice);
                setStatusText(QString("教室 %1 设备 %2 已关机").arg(roomId, deviceId));
            });
        }
    }

    // 模拟设备开机过程。
    void startDevice(const QString &roomId, const QString &deviceId)
    {
        auto *room = findRoom(roomId);
        auto *device = findDevice(roomId, deviceId);
        if (!room || !device)
            return;

        if (device->status == DeviceStatus::On || device->status == DeviceStatus::Starting)
            return;

        device->status = DeviceStatus::Starting;
        publishState(roomId, *device);

        const int delay = nextCommandDelayMs(*room);
        setStatusText(QString("教室 %1 设备 %2 收到开机指令，预计 %3 ms 执行")
                          .arg(roomId, deviceId)
                          .arg(delay));
        QTimer::singleShot(delay, this, [this, roomId, deviceId]() {
            auto *targetRoom = findRoom(roomId);
            auto *targetDevice = findDevice(roomId, deviceId);
            if (!targetRoom || !targetDevice)
                return;

            if (targetRoom->forcedOfflineDevices.contains(deviceId)) {
                targetDevice->status = DeviceStatus::Offline;
                publishState(roomId, *targetDevice);
                return;
            }
            if (rand_.hitPercent(1.8)) {
                targetDevice->status = DeviceStatus::Fault;
                publishState(roomId, *targetDevice);
                publishAlert(roomId, deviceId, "启动失败，设备故障", "critical");
                setStatusText(QString("教室 %1 设备 %2 启动故障").arg(roomId, deviceId));
            } else {
                targetDevice->status = DeviceStatus::On;
                publishState(roomId, *targetDevice);
                setStatusText(QString("教室 %1 设备 %2 已开机").arg(roomId, deviceId));
            }
        });
    }

    // 给某一个教室发送心跳，并顺手补发状态。
    void sendHeartbeatForRoom(const QString &roomId)
    {
        const auto *room = findRoom(roomId);
        if (!room || room->heartbeatPaused || !mqttReady_)
            return;

        const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        try {
            auto msg = mqtt::make_message(
                Topics::heartbeatTopic(roomId).toStdString(),
                ts.toStdString());
            msg->set_qos(0);
            client_.publish(msg);
            publishAllStates(roomId);
        } catch (const mqtt::exception &e) {
            setStatusText(QString("教室 %1 心跳上报失败: %2").arg(roomId, e.what()));
        }
    }

private slots:
    // 定时对所有教室发送心跳。
    void sendHeartbeat()
    {
        const QStringList allRooms = rooms_.keys();
        for (const auto &roomId : allRooms)
            sendHeartbeatForRoom(roomId);
    }
};

// 仿真端主入口：支持 headless 模式，也支持带 QML 的图形模式。
int main(int argc, char **argv)
{
    ClassInit::setupConsoleUtf8();
    QStringList rawArgs;
    for (int i = 0; i < argc; ++i)
        rawArgs.append(QString::fromLocal8Bit(argv[i]));
    const bool headless = rawArgs.contains("--headless");

    // 无界面模式只跑后台逻辑，不创建 QML 界面，适合命令行调试和自动化测试。
    if (headless) {
        QCoreApplication app(argc, argv);
        const QStringList args = positionalArgs(app.arguments());
        const QString roomId = ClassInit::argOrDefault(args, 1, "A101");
        const QString broker = ClassInit::argOrDefault(args, 2, "tcp://127.0.0.1:1883");
        DeviceSimClient sim(roomId, broker, QString());
        qInfo("device simulator running in headless mode: room=%s broker=%s",
              qPrintable(roomId),
              qPrintable(broker));
        return app.exec();
    }

    // 图形模式会加载 QML 界面，方便手动观察教室切换和设备状态。
    QQuickStyle::setStyle("Material");
    QQuickStyle::setFallbackStyle("Basic");

    QGuiApplication app(argc, argv);
    const QStringList args = positionalArgs(app.arguments());
    const QString roomId = ClassInit::argOrDefault(args, 1, "A101");
    const QString broker = ClassInit::argOrDefault(args, 2, "tcp://127.0.0.1:1883");
    const QString backendUrl = qEnvironmentVariable("SIM_BACKEND_URL", "http://127.0.0.1:8080");

    QQmlApplicationEngine engine;
    DeviceSimClient sim(roomId, broker, backendUrl);
    engine.rootContext()->setContextProperty("sim", &sim);
    engine.load(QUrl(QStringLiteral("qrc:/device/SimulatorApp.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}

#include "device_sim_client_main.moc"
