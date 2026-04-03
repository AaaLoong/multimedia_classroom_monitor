#include <httplib.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QReadLocker>
#include <QReadWriteLock>
#include <QSet>
#include <QWriteLocker>
#include <QtConcurrent>
#include <QtGlobal>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "backend_auth.h"
#include "class_init.h"
#include "command_service.h"
#include "db_store.h"
#include "device.h"
#include "http_routes.h"
#include "mqtt_adapter.h"
#include "persist_executor.h"
#include "sse_hub.h"

struct DeviceState {
    QString name;
    QString status;
    QString updated;
};

struct RoomRuntime {
    mutable QReadWriteLock lock;
    QHash<QString, DeviceState> devices;
};

struct PendingStateUpdate {
    QString roomId;
    QString deviceId;
    DeviceState state;
    qint64 dueAtMs = 0;
};

struct StateEnvelope {
    QString roomId;
    QString deviceId;
    QString name;
    QString status;
    QJsonObject payload;
};

struct StateWorker {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<StateEnvelope> queue;
    std::thread thread;
};

static bool commandTraceEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("SIM_COMMAND_TRACE")
        && qEnvironmentVariable("SIM_COMMAND_TRACE").trimmed().compare("false", Qt::CaseInsensitive) != 0
        && qEnvironmentVariable("SIM_COMMAND_TRACE").trimmed() != "0";
    return enabled;
}

static QString traceValue(QString value)
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

static void logBackendTrace(const QString &phase,
                            const QString &result,
                            const QString &roomId,
                            const QString &deviceId,
                            const QString &commandId,
                            qulonglong epoch,
                            qulonglong version,
                            const QString &status,
                            const QString &detail)
{
    if (!commandTraceEnabled())
        return;

    const QString line = QStringLiteral(
                             "cmd_trace ts=%1 source=backend phase=%2 result=%3 room=%4 device=%5 command_id=%6 epoch=%7 version=%8 status=%9 detail=%10")
                             .arg(traceValue(ClassInit::currentShanghaiIsoString()),
                                  traceValue(phase),
                                  traceValue(result),
                                  traceValue(roomId),
                                  traceValue(deviceId),
                                  traceValue(commandId),
                                  traceValue(epoch > 0 ? QString::number(epoch) : QString()),
                                  traceValue(version > 0 ? QString::number(version) : QString()),
                                  traceValue(status),
                                  traceValue(detail));
    qInfo().noquote() << line;
}

static QString normalizeDeviceStatus(const QString &rawStatus)
{
    const QString status = rawStatus.trimmed().toLower();
    if (status == "on" || rawStatus == "开机")
        return "on";
    if (status == "off" || rawStatus == "关机")
        return "off";
    if (status == "starting" || rawStatus == "启动中")
        return "starting";
    if (status == "offline" || rawStatus == "离线")
        return "offline";
    if (status == "fault" || rawStatus == "设备故障")
        return "fault";
    return status;
}

static int readCommandWorkerCount()
{
    bool ok = false;
    const int workerFromEnv = qEnvironmentVariableIntValue("SIM_WORKER_THREADS", &ok);
    return qMax(1, ok ? workerFromEnv : 8);
}

static int readHttpWorkerCount()
{
    bool ok = false;
    const int workerFromEnv = qEnvironmentVariableIntValue("SIM_HTTP_WORKER_THREADS", &ok);
    return qMax(1, ok ? workerFromEnv : 128);
}

static int readHttpKeepAliveMaxCount()
{
    bool ok = false;
    const int keepAliveMaxFromEnv = qEnvironmentVariableIntValue("SIM_HTTP_KEEP_ALIVE_MAX_COUNT", &ok);
    return qMax(1, ok ? keepAliveMaxFromEnv : 4);
}

static int readHttpKeepAliveTimeoutSec()
{
    bool ok = false;
    const int keepAliveTimeoutFromEnv = qEnvironmentVariableIntValue("SIM_HTTP_KEEP_ALIVE_TIMEOUT_SEC", &ok);
    return qMax(1, ok ? keepAliveTimeoutFromEnv : 2);
}

static int readPersistWorkerCount()
{
    bool ok = false;
    const int workerFromEnv = qEnvironmentVariableIntValue("SIM_PERSIST_WORKER_THREADS", &ok);
    return qMax(1, ok ? workerFromEnv : 4);
}

static int readStateWorkerCount()
{
    bool ok = false;
    const int workerFromEnv = qEnvironmentVariableIntValue("SIM_STATE_WORKER_THREADS", &ok);
    return qMax(1, ok ? workerFromEnv : 4);
}

static int readQueueLimit()
{
    bool ok = false;
    const int queueFromEnv = qEnvironmentVariableIntValue("SIM_QUEUE_MAX", &ok);
    return qMax(500, ok ? queueFromEnv : 5000);
}

static int readDedupWindowSec()
{
    bool ok = false;
    const int dedupFromEnv = qEnvironmentVariableIntValue("SIM_DEDUP_WINDOW_SEC", &ok);
    return qMax(1, ok ? dedupFromEnv : 3);
}

static int readBackendPort()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("SIM_BACKEND_PORT", &ok);
    return (!ok || port <= 0) ? 8080 : port;
}

static void respondJson(httplib::Response &res, const QJsonDocument &doc, int status = 200)
{
    res.status = status;
    res.set_content(doc.toJson(QJsonDocument::Compact).toStdString(), "application/json");
}

static void respondError(httplib::Response &res, const QString &error, int status)
{
    respondJson(res, QJsonDocument(QJsonObject{{"error", error}}), status);
}

class BackendServer
{
public:
    BackendServer()
        : mqttUrl_(qEnvironmentVariable("SIM_MQTT_URL", "tcp://127.0.0.1:1883")),
          mqtt_(mqttUrl_),
          server_(std::make_unique<httplib::Server>()),
          auth_(db_),
          commandService_(db_,
                          mqtt_,
                          persistExecutor_,
                          readCommandWorkerCount(),
                          readQueueLimit(),
                          readDedupWindowSec()),
          httpRoutes_(HttpRouteContext{
              db_,
              mqtt_,
              auth_,
              sseHub_,
              commandService_,
              [this]() { return lockedState(); },
              [this](bool locked) { setLockedState(locked); },
              [this](const QString &roomId) { return isRoomRegistered(roomId); },
              [this](const QString &roomId, bool registered) { markRoomRegistered(roomId, registered); },
              [this](const QString &roomId) { seedRuntimeDevicesForRoom(roomId); },
              [this](const QString &roomId) { purgeRuntimeForRoom(roomId); },
              [this](const QString &roomId) { sseHub_.broadcastDevicesChanged(roomId); },
              [this](const QString &roomId, const QString &deviceId) {
                  sseHub_.broadcastAlertsChanged(roomId, deviceId);
              },
              [this](const QString &roomId, bool registered) {
                  sseHub_.broadcastRegistrationChanged(roomId, registered);
              },
              [this]() { sseHub_.broadcastLockChanged(lockedState()); },
              [this](const httplib::Request &req, httplib::Response &res) {
                  pushDevicesResponse(req, res);
              }
          }),
          bindHost_(qEnvironmentVariable("SIM_BIND_HOST", "127.0.0.1").trimmed()),
          port_(readBackendPort())
    {
        mqtt_.setStateHandler([this](const QString &roomId,
                                     const QString &deviceId,
                                     const QString &name,
                                     const QString &status,
                                     const QJsonObject &payload) {
            enqueueStateEnvelope(StateEnvelope{roomId, deviceId, name, status, payload});
        });
        mqtt_.setAlertHandler([this](const QString &roomId,
                                     const QString &deviceId,
                                     const QString &message,
                                     const QString &level) {
            handleAlertEnvelope(roomId, deviceId, message, level);
        });
        server_->set_keep_alive_max_count(static_cast<size_t>(readHttpKeepAliveMaxCount()));
        server_->set_keep_alive_timeout(readHttpKeepAliveTimeoutSec());
        server_->set_tcp_nodelay(true);
        server_->new_task_queue = [] { return new httplib::ThreadPool(static_cast<size_t>(readHttpWorkerCount())); };
        server_->set_exception_handler([](const httplib::Request &req, httplib::Response &res, std::exception_ptr ep) {
            QString detail = QStringLiteral("unknown");
            try {
                if (ep)
                    std::rethrow_exception(ep);
            } catch (const std::exception &ex) {
                detail = QString::fromUtf8(ex.what());
            } catch (...) {
                detail = QStringLiteral("non_std_exception");
            }

            qCritical().noquote()
                << QStringLiteral("route_exception method=%1 path=%2 detail=%3")
                       .arg(QString::fromStdString(req.method),
                            QString::fromStdString(req.path),
                            traceValue(detail));
            respondJson(res, QJsonDocument(QJsonObject{
                             {"error", "internal_error"},
                             {"detail", "route_exception"}
                         }),
                        500);
        });
    }

    ~BackendServer()
    {
        stopStateWorkers();
        commandService_.stop();
        persistExecutor_.stop();
    }

    void start()
    {
        if (!db_.init()) {
            qCritical("MySQL 驱动不可用或连接失败。");
            QCoreApplication::quit();
            return;
        }

        if (!auth_.bootstrapAdminUser()) {
            QCoreApplication::quit();
            return;
        }

        persistExecutor_.start(readPersistWorkerCount());
        loadInitialRuntimeState();
        startStateWorkers();
        commandService_.start();
        commandService_.restoreRecoverableCommands();

        if (!mqtt_.connect())
            qWarning("MQTT 初始连接失败，后端会依赖后续自动重连。");

        httpRoutes_.registerRoutes(*server_);
        qInfo("backend listen on http://%s:%d", qPrintable(bindHost_), port_);
        if (!server_->listen(bindHost_.toStdString(), port_)) {
            qCritical("后端监听失败: %s:%d", qPrintable(bindHost_), port_);
            QCoreApplication::quit();
        }
    }

private:
    int stateWorkerIndexFor(const QString &roomId, const QString &deviceId) const
    {
        const uint hash = qHash(roomId + "::" + deviceId);
        return static_cast<int>(hash % static_cast<uint>(stateWorkers_.size()));
    }

    void startStateWorkers()
    {
        stopStateWorkers_ = false;
        const int workerCount = readStateWorkerCount();
        for (int i = 0; i < workerCount; ++i) {
            stateWorkers_.emplace_back();
            stateWorkers_.back().thread = std::thread([this, i]() { stateWorkerLoop(i); });
        }
        stateFlushThread_ = std::thread([this]() { stateFlushLoop(); });
    }

    void stopStateWorkers()
    {
        stopStateWorkers_ = true;
        for (auto &worker : stateWorkers_)
            worker.cv.notify_all();

        if (stateFlushThread_.joinable())
            stateFlushThread_.join();

        for (auto &worker : stateWorkers_) {
            if (worker.thread.joinable())
                worker.thread.join();
        }
        stateWorkers_.clear();
    }

    void enqueueStateEnvelope(const StateEnvelope &envelope)
    {
        if (stateWorkers_.empty())
            return;
        auto &worker = stateWorkers_[static_cast<size_t>(stateWorkerIndexFor(envelope.roomId, envelope.deviceId))];
        {
            std::lock_guard<std::mutex> lock(worker.mutex);
            worker.queue.push_back(envelope);
        }
        worker.cv.notify_one();
    }

    void stateWorkerLoop(int workerIndex)
    {
        auto &worker = stateWorkers_[static_cast<size_t>(workerIndex)];
        while (!stopStateWorkers_) {
            StateEnvelope envelope;
            {
                std::unique_lock<std::mutex> lock(worker.mutex);
                worker.cv.wait(lock, [this, &worker]() {
                    return stopStateWorkers_ || !worker.queue.empty();
                });
                if (worker.queue.empty()) {
                    if (stopStateWorkers_)
                        return;
                    continue;
                }
                envelope = worker.queue.front();
                worker.queue.pop_front();
            }

            handleStateEnvelope(envelope);
        }
    }

    void stateFlushLoop()
    {
        while (!stopStateWorkers_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            QVector<PendingStateUpdate> readyUpdates;
            QStringList readyRooms;
            {
                QMutexLocker locker(&pendingStateMutex_);
                QStringList readyKeys;
                for (auto it = pendingStateUpdates_.cbegin(); it != pendingStateUpdates_.cend(); ++it) {
                    if (it.value().dueAtMs <= nowMs)
                        readyKeys.append(it.key());
                }

                for (const auto &key : readyKeys) {
                    const auto pending = pendingStateUpdates_.take(key);
                    readyUpdates.append(pending);
                    pendingDeviceBroadcastRooms_.insert(pending.roomId);
                }

                readyRooms = pendingDeviceBroadcastRooms_.values();
                pendingDeviceBroadcastRooms_.clear();
            }

            for (const auto &pending : readyUpdates) {
                if (!isRoomRegistered(pending.roomId))
                    continue;
                persistExecutor_.enqueue([this, pending]() {
                    db_.upsertDevice(pending.roomId, pending.deviceId, pending.state.name, pending.state.status);
                });
            }

            for (const auto &roomId : readyRooms)
                sseHub_.broadcastDevicesChanged(roomId);
        }
    }

    void handleStateEnvelope(const StateEnvelope &envelope)
    {
        if (!isRoomRegistered(envelope.roomId))
            return;

        DeviceState state;
        state.name = envelope.name;
        state.status = normalizeDeviceStatus(envelope.status);
        state.updated = ClassInit::currentShanghaiIsoString();
        setRuntimeDeviceState(envelope.roomId, envelope.deviceId, state);

        logBackendTrace(QStringLiteral("backend_state_rx"),
                        QStringLiteral("received"),
                        envelope.roomId,
                        envelope.deviceId,
                        envelope.payload.value("applied_command_id").toString(),
                        envelope.payload.value("applied_command_epoch").toVariant().toULongLong(),
                        envelope.payload.value("applied_device_command_version").toVariant().toULongLong(),
                        state.status,
                        QStringLiteral("name=%1").arg(traceValue(envelope.name)));

        PendingStateUpdate pending;
        pending.roomId = envelope.roomId;
        pending.deviceId = envelope.deviceId;
        pending.state = state;
        pending.dueAtMs = QDateTime::currentMSecsSinceEpoch() + 500;
        {
            QMutexLocker locker(&pendingStateMutex_);
            pendingStateUpdates_.insert(envelope.roomId + "::" + envelope.deviceId, pending);
        }

        commandService_.tryAckCommands(envelope.roomId,
                                       envelope.deviceId,
                                       state.status,
                                       envelope.payload.value("applied_command_id").toString(),
                                       envelope.payload.value("applied_command_epoch").toVariant().toULongLong(),
                                       envelope.payload.value("applied_device_command_version").toVariant().toULongLong());
    }

    void handleAlertEnvelope(const QString &roomId,
                             const QString &deviceId,
                             const QString &message,
                             const QString &level)
    {
        if (!isRoomRegistered(roomId))
            return;

        persistExecutor_.enqueue([this, roomId, deviceId, message, level]() {
            db_.insertAlert(roomId, deviceId, message, level);
            sseHub_.broadcastAlertsChanged(roomId, deviceId);
        });
    }

    void loadInitialRuntimeState()
    {
        QMutexLocker locker(&dataMutex_);
        locked_ = db_.locked();

        const auto classrooms = db_.loadRegisteredClassrooms();
        for (const auto &item : classrooms) {
            const auto obj = item.toObject();
            const QString roomId = obj.value("room_id").toString();
            if (roomId.isEmpty())
                continue;
            registeredRooms_.insert(roomId);
            ensureRoomRuntimeLocked(roomId);
        }

        const auto devices = db_.loadDevices();
        for (const auto &item : devices) {
            const auto obj = item.toObject();
            const QString roomId = obj.value("room_id").toString();
            const QString deviceId = obj.value("device_id").toString();
            auto runtime = ensureRoomRuntimeLocked(roomId);

            DeviceState state;
            state.name = obj.value("name").toString();
            state.status = obj.value("status").toString();
            state.updated = obj.value("updated").toString();
            runtime->devices.insert(deviceId, state);
        }
    }

    std::shared_ptr<RoomRuntime> ensureRoomRuntimeLocked(const QString &roomId)
    {
        auto runtime = roomRuntimes_.value(roomId);
        if (!runtime) {
            runtime = std::make_shared<RoomRuntime>();
            roomRuntimes_.insert(roomId, runtime);
        }
        return runtime;
    }

    std::shared_ptr<RoomRuntime> roomRuntime(const QString &roomId) const
    {
        QMutexLocker locker(&dataMutex_);
        return roomRuntimes_.value(roomId);
    }

    void markRoomRegistered(const QString &roomId, bool registered)
    {
        QMutexLocker locker(&dataMutex_);
        if (registered) {
            registeredRooms_.insert(roomId);
            ensureRoomRuntimeLocked(roomId);
        } else {
            registeredRooms_.remove(roomId);
            roomRuntimes_.remove(roomId);
        }
    }

    bool isRoomRegistered(const QString &roomId) const
    {
        QMutexLocker locker(&dataMutex_);
        return registeredRooms_.contains(roomId);
    }

    bool lockedState() const
    {
        QMutexLocker locker(&dataMutex_);
        return locked_;
    }

    void setLockedState(bool locked)
    {
        QMutexLocker locker(&dataMutex_);
        locked_ = locked;
    }

    void setRuntimeDeviceState(const QString &roomId, const QString &deviceId, const DeviceState &state)
    {
        std::shared_ptr<RoomRuntime> runtime;
        {
            QMutexLocker locker(&dataMutex_);
            runtime = ensureRoomRuntimeLocked(roomId);
        }
        QWriteLocker roomLocker(&runtime->lock);
        runtime->devices.insert(deviceId, state);
    }

    void removeRuntimeRoom(const QString &roomId)
    {
        QMutexLocker locker(&dataMutex_);
        roomRuntimes_.remove(roomId);
    }

    QHash<QString, DeviceState> deviceSnapshotForRoom(const QString &roomId) const
    {
        const auto runtime = roomRuntime(roomId);
        if (!runtime)
            return {};

        QReadLocker locker(&runtime->lock);
        return runtime->devices;
    }

    QList<QPair<QString, QHash<QString, DeviceState>>> deviceSnapshotAll() const
    {
        QVector<QPair<QString, std::shared_ptr<RoomRuntime>>> runtimes;
        {
            QMutexLocker locker(&dataMutex_);
            QStringList roomIds = roomRuntimes_.keys();
            std::sort(roomIds.begin(), roomIds.end());
            runtimes.reserve(roomIds.size());
            for (const auto &roomId : roomIds) {
                const auto runtime = roomRuntimes_.value(roomId);
                if (runtime)
                    runtimes.push_back(qMakePair(roomId, runtime));
            }
        }

        QList<QPair<QString, QHash<QString, DeviceState>>> snapshots;
        snapshots.reserve(runtimes.size());
        for (const auto &entry : runtimes) {
            QReadLocker roomLocker(&entry.second->lock);
            snapshots.push_back(qMakePair(entry.first, entry.second->devices));
        }
        return snapshots;
    }

    QJsonArray deviceJsonForRoomSnapshot(const QString &roomId, const QHash<QString, DeviceState> &snapshot) const
    {
        QJsonArray arr;
        QStringList deviceIds = snapshot.keys();
        std::sort(deviceIds.begin(), deviceIds.end());
        for (const auto &deviceId : deviceIds) {
            const auto state = snapshot.value(deviceId);
            QJsonObject obj;
            obj["room_id"] = roomId;
            obj["device_id"] = deviceId;
            obj["name"] = state.name;
            obj["status"] = state.status;
            obj["updated"] = state.updated;
            arr.append(obj);
        }
        return arr;
    }

    QJsonArray deviceJsonForAllSnapshot(const QList<QPair<QString, QHash<QString, DeviceState>>> &snapshot) const
    {
        QJsonArray arr;
        for (const auto &roomSnapshot : snapshot) {
            const auto roomArr = deviceJsonForRoomSnapshot(roomSnapshot.first, roomSnapshot.second);
            for (const auto &item : roomArr)
                arr.append(item);
        }
        return arr;
    }

    void pushDevicesResponse(const httplib::Request &req, httplib::Response &res)
    {
        const bool isAdmin = auth_.isAuthorized(req);
        const QString roomFilter = req.has_param("room_id")
            ? QString::fromStdString(req.get_param_value("room_id")).trimmed()
            : QString();
        if (!isAdmin && roomFilter.isEmpty()) {
            respondError(res, "unauthorized", 401);
            return;
        }

        const bool lockedCopy = lockedState();
        if (!roomFilter.isEmpty() && !isAdmin && !isRoomRegistered(roomFilter)) {
            respondJson(res, QJsonDocument(QJsonObject{
                             {"locked", lockedCopy},
                             {"devices", QJsonArray()}
                         }));
            return;
        }

        QJsonArray devices;
        if (!lockedCopy || isAdmin) {
            devices = roomFilter.isEmpty()
                ? deviceJsonForAllSnapshot(deviceSnapshotAll())
                : deviceJsonForRoomSnapshot(roomFilter, deviceSnapshotForRoom(roomFilter));
        }

        respondJson(res, QJsonDocument(QJsonObject{
                         {"locked", lockedCopy},
                         {"devices", devices}
                     }));
    }

    void purgeRuntimeForRoom(const QString &roomId)
    {
        removeRuntimeRoom(roomId);
        commandService_.purgeRoom(roomId);
    }

    void seedRuntimeDevicesForRoom(const QString &roomId)
    {
        const QList<Device> defaults = defaultDevicesForRoom(roomId);
        const QString updatedAt = ClassInit::currentShanghaiIsoString();
        auto runtime = roomRuntime(roomId);
        if (!runtime) {
            QMutexLocker locker(&dataMutex_);
            runtime = ensureRoomRuntimeLocked(roomId);
        }
        QWriteLocker roomLocker(&runtime->lock);
        for (const auto &device : defaults) {
            if (runtime->devices.contains(device.id))
                continue;

            DeviceState state;
            state.name = device.name;
            state.status = "off";
            state.updated = updatedAt;
            runtime->devices.insert(device.id, state);
        }
    }

    QString mqttUrl_;
    MqttAdapter mqtt_;
    std::unique_ptr<httplib::Server> server_;
    DbStore db_;
    BackendAuth auth_;
    SseHub sseHub_;
    PersistExecutor persistExecutor_;
    CommandService commandService_;
    HttpRoutes httpRoutes_;
    mutable QMutex dataMutex_;
    mutable QMutex pendingStateMutex_;
    QHash<QString, std::shared_ptr<RoomRuntime>> roomRuntimes_;
    QSet<QString> registeredRooms_;
    QHash<QString, PendingStateUpdate> pendingStateUpdates_;
    QSet<QString> pendingDeviceBroadcastRooms_;
    std::deque<StateWorker> stateWorkers_;
    std::thread stateFlushThread_;
    std::atomic_bool stopStateWorkers_ = false;
    bool locked_ = false;
    QString bindHost_;
    int port_ = 8080;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ClassInit::loadRuntimeEnvDefaults();
    BackendServer server;
    const auto future = QtConcurrent::run([&]() { server.start(); });
    Q_UNUSED(future);
    return app.exec();
}
