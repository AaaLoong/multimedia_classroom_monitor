#include "command_service.h"

#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QUuid>
#include <QDebug>

#include <algorithm>
#include <chrono>

#include "class_init.h"
#include "db_store.h"
#include "mqtt_adapter.h"
#include "persist_executor.h"

namespace {

qulonglong randomEpoch()
{
    qulonglong epoch = QRandomGenerator::global()->generate64();
    return epoch == 0 ? 1 : epoch;
}

qulonglong jsonToULongLong(const QJsonValue &value)
{
    bool ok = false;
    const auto asString = value.toString();
    if (!asString.isEmpty()) {
        const qulonglong parsed = asString.toULongLong(&ok);
        if (ok)
            return parsed;
    }

    if (value.isDouble()) {
        const double number = value.toDouble(-1.0);
        return number >= 0.0 ? static_cast<qulonglong>(number) : 0;
    }

    return 0;
}

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

template <typename OptionalCommand>
QString commandIdOf(const OptionalCommand &job)
{
    return job.has_value() ? job->id : QString();
}

template <typename OptionalCommand>
QString commandStateOf(const OptionalCommand &job)
{
    return job.has_value() ? job->state : QString();
}

void logCommandTrace(const QString &phase,
                     const QString &result,
                     const QString &roomId,
                     const QString &deviceId,
                     const QString &action,
                     const QString &commandId,
                     qulonglong epoch,
                     qulonglong version,
                     const QString &desiredStatus,
                     const QString &currentStatus,
                     const QString &pendingState,
                     const QString &pendingCommandId,
                     const QString &activeState,
                     const QString &activeCommandId,
                     int retryCount,
                     const QDateTime &deadlineAt,
                     const QString &detail)
{
    if (!commandTraceEnabled())
        return;

    const QString line = QStringLiteral(
                             "cmd_trace ts=%1 source=backend phase=%2 result=%3 room=%4 device=%5 action=%6 "
                             "command_id=%7 epoch=%8 version=%9 desired=%10 current=%11 pending_state=%12 "
                             "pending_command_id=%13 active_state=%14 active_command_id=%15 retry=%16 deadline=%17 detail=%18")
                             .arg(traceValue(ClassInit::currentShanghaiIsoString()),
                                  traceValue(phase),
                                  traceValue(result),
                                  traceValue(roomId),
                                  traceValue(deviceId),
                                  traceValue(action),
                                  traceValue(commandId),
                                  traceValue(epoch > 0 ? QString::number(epoch) : QString()),
                                  traceValue(version > 0 ? QString::number(version) : QString()),
                                  traceValue(desiredStatus),
                                  traceValue(currentStatus),
                                  traceValue(pendingState),
                                  traceValue(pendingCommandId),
                                  traceValue(activeState),
                                  traceValue(activeCommandId),
                                  traceValue(retryCount >= 0 ? QString::number(retryCount) : QString()),
                                  traceValue(deadlineAt.isValid() ? ClassInit::toShanghaiIsoString(deadlineAt) : QString()),
                                  traceValue(detail));
    qInfo().noquote() << line;
}

} // namespace

CommandService::CommandService(DbStore &db,
                               MqttAdapter &mqtt,
                               PersistExecutor &persistExecutor,
                               int commandWorkerCount,
                               int queueLimit,
                               int dedupWindowSec)
    : db_(db),
      mqtt_(mqtt),
      persistExecutor_(persistExecutor),
      commandWorkerCount_(commandWorkerCount),
      queueLimit_(queueLimit),
      dedupWindowSec_(dedupWindowSec)
{
}

CommandService::~CommandService()
{
    stop();
}

void CommandService::start()
{
    stopWorkers_ = false;
    currentEpoch_ = randomEpoch();
    startWorkers();
    startTimeoutLoop();
}

void CommandService::stop()
{
    stopWorkers_ = true;
    readyCv_.notify_all();

    for (auto &worker : workerThreads_) {
        if (worker.joinable())
            worker.join();
    }
    workerThreads_.clear();

    if (timeoutThread_.joinable())
        timeoutThread_.join();
}

void CommandService::restoreRecoverableCommands()
{
}

QString CommandService::targetStatusForAction(const QString &action) const
{
    const QString a = action.trimmed().toLower();
    if (a == "on" || a == "start" || a == "open" || a == "power_on")
        return "on";
    if (a == "off" || a == "stop" || a == "close" || a == "power_off")
        return "off";
    return a;
}

QString CommandService::deviceKeyFor(const QString &roomId, const QString &deviceId) const
{
    return QString("%1::%2").arg(roomId, deviceId);
}

CommandOrderKey CommandService::orderKeyFor(const CommandRuntime &job) const
{
    return CommandOrderKey{job.commandEpoch, job.deviceCommandVersion};
}

bool CommandService::orderLess(const CommandOrderKey &lhs, const CommandOrderKey &rhs) const
{
    if (lhs.epoch != rhs.epoch)
        return lhs.epoch < rhs.epoch;
    return lhs.version < rhs.version;
}

bool CommandService::orderEqual(const CommandOrderKey &lhs, const CommandOrderKey &rhs) const
{
    return lhs.epoch == rhs.epoch && lhs.version == rhs.version;
}

bool CommandService::orderGreater(const CommandOrderKey &lhs, const CommandOrderKey &rhs) const
{
    return orderLess(rhs, lhs);
}

int CommandService::shardIndexFor(const QString &roomId, const QString &deviceId) const
{
    return static_cast<int>(qHash(deviceKeyFor(roomId, deviceId)) % static_cast<uint>(shards_.size()));
}

void CommandService::startWorkers()
{
    qInfo("command workers starting: %d", commandWorkerCount_);
    workerThreads_.clear();
    for (int i = 0; i < commandWorkerCount_; ++i)
        workerThreads_.emplace_back([this]() { workerLoop(); });
}

void CommandService::startTimeoutLoop()
{
    timeoutThread_ = std::thread([this]() { timeoutLoop(); });
}

void CommandService::timeoutLoop()
{
    qInfo("command timeout loop ready");
    while (!stopWorkers_) {
        QThread::msleep(200);
        onTimeoutTick();
    }
}

void CommandService::enqueueReadyShard(int shardIndex)
{
    std::lock_guard<std::mutex> lock(readyMutex_);
    if (readyShardSet_.contains(shardIndex))
        return;
    readyShards_.push_back(shardIndex);
    readyShardSet_.insert(shardIndex);
    readyCv_.notify_one();
}

void CommandService::enqueueReadyDeviceLocked(int shardIndex, const QString &deviceKey, DeviceRuntimeState &deviceState)
{
    if (!deviceState.pending.has_value() || deviceState.readyQueued)
        return;
    auto &shard = shards_[static_cast<size_t>(shardIndex)];
    shard.readyDevices.enqueue(deviceKey);
    shard.readySet.insert(deviceKey);
    deviceState.readyQueued = true;
    enqueueReadyShard(shardIndex);
}

bool CommandService::popReadyDevice(int &shardIndex, QString &deviceKey)
{
    while (!stopWorkers_) {
        {
            std::unique_lock<std::mutex> lock(readyMutex_);
            readyCv_.wait(lock, [this]() { return stopWorkers_ || !readyShards_.empty(); });
            if (stopWorkers_)
                return false;
            shardIndex = readyShards_.front();
            readyShards_.pop_front();
            readyShardSet_.remove(shardIndex);
        }

        auto &shard = shards_[static_cast<size_t>(shardIndex)];
        QMutexLocker shardLocker(&shard.mutex);
        while (!shard.readyDevices.isEmpty()) {
            const QString key = shard.readyDevices.dequeue();
            shard.readySet.remove(key);
            auto it = shard.devices.find(key);
            if (it == shard.devices.end())
                continue;
            it->readyQueued = false;
            if (!it->pending.has_value())
                continue;
            deviceKey = key;
            if (!shard.readyDevices.isEmpty())
                enqueueReadyShard(shardIndex);
            return true;
        }
    }

    return false;
}

CommandAcceptResult CommandService::acceptCommand(const QString &roomId,
                                                  const QString &deviceId,
                                                  const QString &action)
{
    if (action.trimmed().isEmpty())
        return {400, "empty_action", {}, 0};
    if (currentEpoch_ == 0)
        return {503, "command_epoch_unavailable", {}, 0};

    const QString targetStatus = targetStatusForAction(action);
    const QString deviceKey = deviceKeyFor(roomId, deviceId);
    const int shardIndex = shardIndexFor(roomId, deviceId);
    auto &shard = shards_[static_cast<size_t>(shardIndex)];
    CommandRuntime job;

    {
        QMutexLocker locker(&shard.mutex);
        auto &deviceState = shard.devices[deviceKey];
        deviceState.roomId = roomId;
        deviceState.deviceId = deviceId;

        logCommandTrace(QStringLiteral("http_accept"),
                        QStringLiteral("received"),
                        roomId,
                        deviceId,
                        action,
                        QString(),
                        currentEpoch_,
                        deviceState.nextVersion + 1,
                        targetStatus,
                        deviceState.currentStatus,
                        commandStateOf(deviceState.pending),
                        commandIdOf(deviceState.pending),
                        commandStateOf(deviceState.active),
                        commandIdOf(deviceState.active),
                        deviceState.active.has_value() ? deviceState.active->retryCount : -1,
                        deviceState.active.has_value() ? deviceState.active->deadlineAt : QDateTime(),
                        QStringLiteral("desired_before=%1").arg(traceValue(deviceState.desiredStatus)));

        if ((deviceState.pending.has_value() && deviceState.pending->targetStatus == targetStatus)
            || (deviceState.active.has_value() && deviceState.active->targetStatus == targetStatus)
            || (!deviceState.desiredStatus.isEmpty() && deviceState.desiredStatus == targetStatus)) {
            const QString existingCommandId = deviceState.pending.has_value()
                ? deviceState.pending->id
                : (deviceState.active.has_value() ? deviceState.active->id : QString());
            logCommandTrace(QStringLiteral("http_accept"),
                            QStringLiteral("dedup_accepted"),
                            roomId,
                            deviceId,
                            action,
                            existingCommandId,
                            deviceState.active.has_value() ? deviceState.active->commandEpoch : currentEpoch_,
                            deviceState.active.has_value() ? deviceState.active->deviceCommandVersion : deviceState.nextVersion,
                            targetStatus,
                            deviceState.currentStatus,
                            commandStateOf(deviceState.pending),
                            commandIdOf(deviceState.pending),
                            commandStateOf(deviceState.active),
                            commandIdOf(deviceState.active),
                            deviceState.active.has_value() ? deviceState.active->retryCount : -1,
                            deviceState.active.has_value() ? deviceState.active->deadlineAt : QDateTime(),
                            QStringLiteral("dedup_same_target"));
            return {202, {}, existingCommandId, 0};
        }

        const bool replacingPending = deviceState.pending.has_value();
        if (!replacingPending && queuedCommandCount_.load() >= queueLimit_) {
            logCommandTrace(QStringLiteral("http_accept"),
                            QStringLiteral("rejected"),
                            roomId,
                            deviceId,
                            action,
                            QString(),
                            currentEpoch_,
                            deviceState.nextVersion + 1,
                            targetStatus,
                            deviceState.currentStatus,
                            commandStateOf(deviceState.pending),
                            commandIdOf(deviceState.pending),
                            commandStateOf(deviceState.active),
                            commandIdOf(deviceState.active),
                            deviceState.active.has_value() ? deviceState.active->retryCount : -1,
                            deviceState.active.has_value() ? deviceState.active->deadlineAt : QDateTime(),
                            QStringLiteral("queue_busy"));
            return {503, "queue_busy", {}, 0};
        }

        if (deviceState.pending.has_value()) {
            logCommandTrace(QStringLiteral("queued"),
                            QStringLiteral("superseded_pending"),
                            roomId,
                            deviceId,
                            deviceState.pending->action,
                            deviceState.pending->id,
                            deviceState.pending->commandEpoch,
                            deviceState.pending->deviceCommandVersion,
                            deviceState.pending->targetStatus,
                            deviceState.currentStatus,
                            commandStateOf(deviceState.pending),
                            commandIdOf(deviceState.pending),
                            commandStateOf(deviceState.active),
                            commandIdOf(deviceState.active),
                            deviceState.pending->retryCount,
                            deviceState.pending->deadlineAt,
                            QStringLiteral("superseded_by_new_command"));
            deviceState.pending.reset();
            queuedCommandCount_ -= 1;
        }

        if (deviceState.active.has_value()) {
            logCommandTrace(QStringLiteral("queued"),
                            QStringLiteral("superseded_active"),
                            roomId,
                            deviceId,
                            deviceState.active->action,
                            deviceState.active->id,
                            deviceState.active->commandEpoch,
                            deviceState.active->deviceCommandVersion,
                            deviceState.active->targetStatus,
                            deviceState.currentStatus,
                            commandStateOf(deviceState.pending),
                            commandIdOf(deviceState.pending),
                            commandStateOf(deviceState.active),
                            commandIdOf(deviceState.active),
                            deviceState.active->retryCount,
                            deviceState.active->deadlineAt,
                            QStringLiteral("superseded_by_new_command"));
            deviceState.active.reset();
        }

        job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.roomId = roomId;
        job.deviceId = deviceId;
        job.action = action;
        job.targetStatus = targetStatus;
        job.commandEpoch = currentEpoch_;
        job.deviceCommandVersion = ++deviceState.nextVersion;
        job.maxRetry = 2;
        job.timeoutMs = 5000;
        job.state = QStringLiteral("queued");
        job.createdAt = ClassInit::currentShanghaiDateTime();
        job.updatedAt = job.createdAt;

        deviceState.pending = job;
        deviceState.desiredOrder = orderKeyFor(job);
        deviceState.desiredStatus = targetStatus;
        queuedCommandCount_ += 1;
        enqueueReadyDeviceLocked(shardIndex, deviceKey, deviceState);

        logCommandTrace(QStringLiteral("queued"),
                        QStringLiteral("enqueued"),
                        roomId,
                        deviceId,
                        job.action,
                        job.id,
                        job.commandEpoch,
                        job.deviceCommandVersion,
                        job.targetStatus,
                        deviceState.currentStatus,
                        commandStateOf(deviceState.pending),
                        commandIdOf(deviceState.pending),
                        commandStateOf(deviceState.active),
                        commandIdOf(deviceState.active),
                        job.retryCount,
                        job.deadlineAt,
                        QStringLiteral("queue_size=%1").arg(queuedCommandCount_.load()));
    }

    return {202, {}, job.id, 0};
}

void CommandService::workerLoop()
{
    while (!stopWorkers_) {
        int shardIndex = -1;
        QString deviceKey;
        if (!popReadyDevice(shardIndex, deviceKey))
            return;
        dispatchDeviceCommand(shardIndex, deviceKey);
    }
}

void CommandService::dispatchDeviceCommand(int shardIndex, const QString &deviceKey)
{
    CommandRuntime snapshot;
    {
        auto &shard = shards_[static_cast<size_t>(shardIndex)];
        QMutexLocker locker(&shard.mutex);
        auto it = shard.devices.find(deviceKey);
        if (it == shard.devices.end() || !it->pending.has_value())
            return;

        snapshot = *it->pending;
        logCommandTrace(QStringLiteral("queued"),
                        QStringLiteral("dequeued"),
                        snapshot.roomId,
                        snapshot.deviceId,
                        snapshot.action,
                        snapshot.id,
                        snapshot.commandEpoch,
                        snapshot.deviceCommandVersion,
                        snapshot.targetStatus,
                        it->currentStatus,
                        commandStateOf(it->pending),
                        commandIdOf(it->pending),
                        commandStateOf(it->active),
                        commandIdOf(it->active),
                        snapshot.retryCount,
                        snapshot.deadlineAt,
                        QStringLiteral("dispatch_begin"));
        it->pending.reset();
        queuedCommandCount_ -= 1;
        snapshot.state = QStringLiteral("sent");
        snapshot.updatedAt = ClassInit::currentShanghaiDateTime();
        snapshot.deadlineAt = snapshot.updatedAt.addMSecs(snapshot.timeoutMs);
        snapshot.retryDueAtMs = 0;
        it->active = snapshot;

        logCommandTrace(QStringLiteral("mqtt_publish"),
                        QStringLiteral("dispatching"),
                        snapshot.roomId,
                        snapshot.deviceId,
                        snapshot.action,
                        snapshot.id,
                        snapshot.commandEpoch,
                        snapshot.deviceCommandVersion,
                        snapshot.targetStatus,
                        it->currentStatus,
                        commandStateOf(it->pending),
                        commandIdOf(it->pending),
                        commandStateOf(it->active),
                        commandIdOf(it->active),
                        snapshot.retryCount,
                        snapshot.deadlineAt,
                        QStringLiteral("deadline_ms=%1").arg(snapshot.timeoutMs));
    }

    QJsonObject payload;
    payload["action"] = snapshot.action;
    payload["command_id"] = snapshot.id;
    payload["command_epoch"] = QString::number(snapshot.commandEpoch);
    payload["device_command_version"] = QString::number(snapshot.deviceCommandVersion);
    payload["desired_status"] = snapshot.targetStatus;
    payload["issued_at"] = ClassInit::currentShanghaiIsoString();

    if (!mqtt_.publishCommand(snapshot.roomId, snapshot.deviceId, payload)) {
        logCommandTrace(QStringLiteral("mqtt_publish"),
                        QStringLiteral("failed"),
                        snapshot.roomId,
                        snapshot.deviceId,
                        snapshot.action,
                        snapshot.id,
                        snapshot.commandEpoch,
                        snapshot.deviceCommandVersion,
                        snapshot.targetStatus,
                        QString(),
                        QString(),
                        QString(),
                        QStringLiteral("sent"),
                        snapshot.id,
                        snapshot.retryCount,
                        snapshot.deadlineAt,
                        QStringLiteral("mqtt_publish_failed"));
        handlePublishFailure(shardIndex, deviceKey, snapshot.id, "E_PUBLISH", "mqtt_publish_failed", true);
    } else {
        logCommandTrace(QStringLiteral("mqtt_publish"),
                        QStringLiteral("published"),
                        snapshot.roomId,
                        snapshot.deviceId,
                        snapshot.action,
                        snapshot.id,
                        snapshot.commandEpoch,
                        snapshot.deviceCommandVersion,
                        snapshot.targetStatus,
                        QString(),
                        QString(),
                        QString(),
                        QStringLiteral("sent"),
                        snapshot.id,
                        snapshot.retryCount,
                        snapshot.deadlineAt,
                        QStringLiteral("mqtt_publish_ok"));
    }
}

void CommandService::handlePublishFailure(int shardIndex,
                                          const QString &deviceKey,
                                          const QString &commandId,
                                          const QString &errorCode,
                                          const QString &errorMessage,
                                          bool retryable)
{
    CommandRuntime failure;
    bool persistFailureRow = false;

    auto &shard = shards_[static_cast<size_t>(shardIndex)];
    QMutexLocker locker(&shard.mutex);
    auto it = shard.devices.find(deviceKey);
    if (it == shard.devices.end() || !it->active.has_value() || it->active->id != commandId)
        return;

    auto &active = *it->active;
    active.errorCode = errorCode;
    active.errorMessage = errorMessage;
    active.updatedAt = ClassInit::currentShanghaiDateTime();

    if (retryable && active.retryCount < active.maxRetry) {
        active.retryCount += 1;
        active.state = QStringLiteral("retry_wait");
        active.deadlineAt = {};
        active.retryDueAtMs = QDateTime::currentMSecsSinceEpoch() + qMin(4000, 500 * (1 << active.retryCount));
        logCommandTrace(QStringLiteral("backend_retry_wait"),
                        QStringLiteral("scheduled"),
                        active.roomId,
                        active.deviceId,
                        active.action,
                        active.id,
                        active.commandEpoch,
                        active.deviceCommandVersion,
                        active.targetStatus,
                        it->currentStatus,
                        commandStateOf(it->pending),
                        commandIdOf(it->pending),
                        commandStateOf(it->active),
                        commandIdOf(it->active),
                        active.retryCount,
                        active.deadlineAt,
                        QStringLiteral("%1 retry_due_at_ms=%2").arg(traceValue(errorMessage),
                                                                   QString::number(active.retryDueAtMs)));
        return;
    }

    active.state = QStringLiteral("failed");
    failure = active;
    logCommandTrace(QStringLiteral("backend_failed"),
                    QStringLiteral("failed"),
                    active.roomId,
                    active.deviceId,
                    active.action,
                    active.id,
                    active.commandEpoch,
                    active.deviceCommandVersion,
                    active.targetStatus,
                    it->currentStatus,
                    commandStateOf(it->pending),
                    commandIdOf(it->pending),
                    commandStateOf(it->active),
                    commandIdOf(it->active),
                    active.retryCount,
                    active.deadlineAt,
                    QStringLiteral("%1:%2").arg(traceValue(errorCode), traceValue(errorMessage)));
    it->active.reset();
    it->desiredStatus.clear();
    it->desiredOrder = {};
    persistFailureRow = true;
    locker.unlock();

    if (persistFailureRow)
        persistCommandError(failure, "failed", errorCode, errorMessage);
}

void CommandService::persistCommandError(const CommandRuntime &job,
                                         const QString &state,
                                         const QString &errorCode,
                                         const QString &errorMessage)
{
    CommandFailureRecord record;
    record.commandId = job.id;
    record.roomId = job.roomId;
    record.deviceId = job.deviceId;
    record.action = job.action;
    record.desiredStatus = job.targetStatus;
    record.state = state;
    record.commandEpoch = job.commandEpoch;
    record.deviceCommandVersion = job.deviceCommandVersion;
    record.retryCount = job.retryCount;
    record.maxRetry = job.maxRetry;
    record.timeoutMs = job.timeoutMs;
    record.errorCode = errorCode;
    record.errorMessage = errorMessage;
    persistExecutor_.enqueue([this, record]() { db_.upsertCommandFailure(record); });
}

void CommandService::onTimeoutTick()
{
    const auto now = ClassInit::currentShanghaiDateTime();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    for (int shardIndex = 0; shardIndex < static_cast<int>(shards_.size()); ++shardIndex) {
        auto &shard = shards_[static_cast<size_t>(shardIndex)];
        QVector<std::pair<QString, CommandRuntime>> readyToRetry;
        QVector<CommandRuntime> timeoutWrites;
        QVector<CommandRuntime> failedWrites;

        {
            QMutexLocker locker(&shard.mutex);
            for (auto it = shard.devices.begin(); it != shard.devices.end(); ++it) {
                auto &deviceState = it.value();
                if (!deviceState.active.has_value())
                    continue;

                auto &active = *deviceState.active;
                if (active.state == "sent" && active.deadlineAt.isValid() && active.deadlineAt <= now) {
                    active.updatedAt = now;
                    active.errorCode = QStringLiteral("E_TIMEOUT");
                    active.errorMessage = QStringLiteral("ack_timeout");
                    logCommandTrace(QStringLiteral("backend_timeout"),
                                    QStringLiteral("timeout"),
                                    active.roomId,
                                    active.deviceId,
                                    active.action,
                                    active.id,
                                    active.commandEpoch,
                                    active.deviceCommandVersion,
                                    active.targetStatus,
                                    deviceState.currentStatus,
                                    commandStateOf(deviceState.pending),
                                    commandIdOf(deviceState.pending),
                                    commandStateOf(deviceState.active),
                                    commandIdOf(deviceState.active),
                                    active.retryCount,
                                    active.deadlineAt,
                                    QStringLiteral("ack_timeout"));
                    timeoutWrites.append(active);

                    if (active.retryCount < active.maxRetry) {
                        active.retryCount += 1;
                        active.state = QStringLiteral("retry_wait");
                        active.deadlineAt = {};
                        active.retryDueAtMs = nowMs + qMin(4000, 500 * (1 << active.retryCount));
                        logCommandTrace(QStringLiteral("backend_retry_wait"),
                                        QStringLiteral("scheduled"),
                                        active.roomId,
                                        active.deviceId,
                                        active.action,
                                        active.id,
                                        active.commandEpoch,
                                        active.deviceCommandVersion,
                                        active.targetStatus,
                                        deviceState.currentStatus,
                                        commandStateOf(deviceState.pending),
                                        commandIdOf(deviceState.pending),
                                        commandStateOf(deviceState.active),
                                        commandIdOf(deviceState.active),
                                        active.retryCount,
                                        active.deadlineAt,
                                        QStringLiteral("retry_due_at_ms=%1").arg(QString::number(active.retryDueAtMs)));
                    } else {
                        active.state = QStringLiteral("failed");
                        active.errorMessage = QStringLiteral("ack_timeout_final");
                        logCommandTrace(QStringLiteral("backend_failed"),
                                        QStringLiteral("failed"),
                                        active.roomId,
                                        active.deviceId,
                                        active.action,
                                        active.id,
                                        active.commandEpoch,
                                        active.deviceCommandVersion,
                                        active.targetStatus,
                                        deviceState.currentStatus,
                                        commandStateOf(deviceState.pending),
                                        commandIdOf(deviceState.pending),
                                        commandStateOf(deviceState.active),
                                        commandIdOf(deviceState.active),
                                        active.retryCount,
                                        active.deadlineAt,
                                        QStringLiteral("E_TIMEOUT:ack_timeout_final"));
                        failedWrites.append(active);
                        deviceState.active.reset();
                        deviceState.desiredStatus.clear();
                        deviceState.desiredOrder = {};
                    }
                } else if (active.state == "retry_wait" && active.retryDueAtMs > 0 && active.retryDueAtMs <= nowMs) {
                    active.state = QStringLiteral("queued");
                    active.updatedAt = now;
                    active.deadlineAt = {};
                    active.retryDueAtMs = 0;
                    logCommandTrace(QStringLiteral("queued"),
                                    QStringLiteral("retry_requeued"),
                                    active.roomId,
                                    active.deviceId,
                                    active.action,
                                    active.id,
                                    active.commandEpoch,
                                    active.deviceCommandVersion,
                                    active.targetStatus,
                                    deviceState.currentStatus,
                                    commandStateOf(deviceState.pending),
                                    commandIdOf(deviceState.pending),
                                    commandStateOf(deviceState.active),
                                    commandIdOf(deviceState.active),
                                    active.retryCount,
                                    active.deadlineAt,
                                    QStringLiteral("retry_due_elapsed"));
                    readyToRetry.append({it.key(), active});
                    deviceState.active.reset();
                }
            }

            for (const auto &entry : readyToRetry) {
                auto deviceIt = shard.devices.find(entry.first);
                if (deviceIt == shard.devices.end())
                    continue;
                if (deviceIt->pending.has_value())
                    continue;
                deviceIt->pending = entry.second;
                queuedCommandCount_ += 1;
                enqueueReadyDeviceLocked(shardIndex, entry.first, *deviceIt);
            }
        }

        for (const auto &job : timeoutWrites)
            persistCommandError(job, "timeout", "E_TIMEOUT", "ack_timeout");
        for (const auto &job : failedWrites)
            persistCommandError(job, "failed", "E_TIMEOUT", "ack_timeout_final");
    }
}

void CommandService::tryAckCommands(const QString &roomId,
                                    const QString &deviceId,
                                    const QString &status,
                                    const QString &appliedCommandId,
                                    qulonglong appliedEpoch,
                                    qulonglong appliedVersion)
{
    const int shardIndex = shardIndexFor(roomId, deviceId);
    auto &shard = shards_[static_cast<size_t>(shardIndex)];
    const QString deviceKey = deviceKeyFor(roomId, deviceId);
    const CommandOrderKey appliedOrder{appliedEpoch, appliedVersion};
    const auto now = ClassInit::currentShanghaiDateTime();

    QMutexLocker locker(&shard.mutex);
    auto &deviceState = shard.devices[deviceKey];
    deviceState.roomId = roomId;
    deviceState.deviceId = deviceId;
    deviceState.currentStatus = status;

    if (appliedOrder.isValid() && orderGreater(appliedOrder, deviceState.lastReportedOrder))
        deviceState.lastReportedOrder = appliedOrder;

    const bool hasPending = deviceState.pending.has_value();
    const bool hasActive = deviceState.active.has_value();
    if (!hasPending && !hasActive) {
        logCommandTrace(QStringLiteral("backend_ack"),
                        QStringLiteral("ignore_no_inflight"),
                        roomId,
                        deviceId,
                        QString(),
                        appliedCommandId,
                        appliedEpoch,
                        appliedVersion,
                        deviceState.desiredStatus,
                        deviceState.currentStatus,
                        commandStateOf(deviceState.pending),
                        commandIdOf(deviceState.pending),
                        commandStateOf(deviceState.active),
                        commandIdOf(deviceState.active),
                        deviceState.active.has_value() ? deviceState.active->retryCount : -1,
                        deviceState.active.has_value() ? deviceState.active->deadlineAt : QDateTime(),
                        QStringLiteral("status=%1").arg(traceValue(status)));
        return;
    }

    const CommandRuntime desired = hasPending ? *deviceState.pending : *deviceState.active;
    const CommandOrderKey desiredOrder = orderKeyFor(desired);
    const bool statusMatchesDesired = desired.targetStatus == status;

    bool shouldAck = false;
    bool shouldReassert = false;

    if (appliedOrder.isValid()) {
        if (orderEqual(appliedOrder, desiredOrder) && statusMatchesDesired) {
            shouldAck = true;
        } else if (orderLess(appliedOrder, desiredOrder) && !statusMatchesDesired) {
            shouldReassert = true;
        }
    } else if (statusMatchesDesired) {
        shouldAck = true;
    }

    if (shouldAck) {
        if (deviceState.pending.has_value() && orderEqual(orderKeyFor(*deviceState.pending), desiredOrder)) {
            deviceState.pending.reset();
            queuedCommandCount_ -= 1;
        }
        if (deviceState.active.has_value() && orderEqual(orderKeyFor(*deviceState.active), desiredOrder))
            deviceState.active.reset();
        logCommandTrace(QStringLiteral("backend_ack"),
                        QStringLiteral("acked"),
                        roomId,
                        deviceId,
                        desired.action,
                        desired.id,
                        desired.commandEpoch,
                        desired.deviceCommandVersion,
                        desired.targetStatus,
                        deviceState.currentStatus,
                        commandStateOf(deviceState.pending),
                        commandIdOf(deviceState.pending),
                        commandStateOf(deviceState.active),
                        commandIdOf(deviceState.active),
                        desired.retryCount,
                        desired.deadlineAt,
                        QStringLiteral("applied_command_id=%1 applied_epoch=%2 applied_version=%3")
                            .arg(traceValue(appliedCommandId),
                                 traceValue(appliedEpoch > 0 ? QString::number(appliedEpoch) : QString()),
                                 traceValue(appliedVersion > 0 ? QString::number(appliedVersion) : QString())));
        deviceState.desiredStatus.clear();
        deviceState.desiredOrder = {};
        return;
    }

    if (!shouldReassert) {
        logCommandTrace(QStringLiteral("backend_ack"),
                        QStringLiteral("ignore"),
                        roomId,
                        deviceId,
                        desired.action,
                        desired.id,
                        desired.commandEpoch,
                        desired.deviceCommandVersion,
                        desired.targetStatus,
                        deviceState.currentStatus,
                        commandStateOf(deviceState.pending),
                        commandIdOf(deviceState.pending),
                        commandStateOf(deviceState.active),
                        commandIdOf(deviceState.active),
                        desired.retryCount,
                        desired.deadlineAt,
                        QStringLiteral("status_mismatch_or_no_order applied_command_id=%1 applied_epoch=%2 applied_version=%3")
                            .arg(traceValue(appliedCommandId),
                                 traceValue(appliedEpoch > 0 ? QString::number(appliedEpoch) : QString()),
                                 traceValue(appliedVersion > 0 ? QString::number(appliedVersion) : QString())));
        return;
    }

    if (deviceState.pending.has_value()) {
        logCommandTrace(QStringLiteral("backend_reassert"),
                        QStringLiteral("requeue_existing_pending"),
                        roomId,
                        deviceId,
                        desired.action,
                        desired.id,
                        desired.commandEpoch,
                        desired.deviceCommandVersion,
                        desired.targetStatus,
                        deviceState.currentStatus,
                        commandStateOf(deviceState.pending),
                        commandIdOf(deviceState.pending),
                        commandStateOf(deviceState.active),
                        commandIdOf(deviceState.active),
                        desired.retryCount,
                        desired.deadlineAt,
                        QStringLiteral("stale_state_received"));
        enqueueReadyDeviceLocked(shardIndex, deviceKey, deviceState);
        return;
    }

    if (deviceState.active.has_value() && orderEqual(orderKeyFor(*deviceState.active), desiredOrder)) {
        auto retry = *deviceState.active;
        retry.state = QStringLiteral("queued");
        retry.updatedAt = now;
        retry.deadlineAt = {};
        retry.retryDueAtMs = 0;
        deviceState.active.reset();
        deviceState.pending = retry;
        queuedCommandCount_ += 1;
        logCommandTrace(QStringLiteral("backend_reassert"),
                        QStringLiteral("active_requeued"),
                        roomId,
                        deviceId,
                        retry.action,
                        retry.id,
                        retry.commandEpoch,
                        retry.deviceCommandVersion,
                        retry.targetStatus,
                        deviceState.currentStatus,
                        commandStateOf(deviceState.pending),
                        commandIdOf(deviceState.pending),
                        commandStateOf(deviceState.active),
                        commandIdOf(deviceState.active),
                        retry.retryCount,
                        retry.deadlineAt,
                        QStringLiteral("stale_state_received"));
        enqueueReadyDeviceLocked(shardIndex, deviceKey, deviceState);
    }
}

void CommandService::purgeRoom(const QString &roomId)
{
    for (auto &shard : shards_) {
        QMutexLocker locker(&shard.mutex);
        QStringList removeKeys;
        for (auto it = shard.devices.cbegin(); it != shard.devices.cend(); ++it) {
            if (it.value().roomId == roomId)
                removeKeys.append(it.key());
        }
        for (const auto &key : removeKeys) {
            const auto deviceState = shard.devices.take(key);
            if (deviceState.pending.has_value())
                queuedCommandCount_ -= 1;
            shard.readySet.remove(key);
        }

        if (removeKeys.isEmpty())
            continue;

        QQueue<QString> filtered;
        while (!shard.readyDevices.isEmpty()) {
            const QString key = shard.readyDevices.dequeue();
            if (!removeKeys.contains(key))
                filtered.enqueue(key);
        }
        shard.readyDevices = filtered;
    }
}
