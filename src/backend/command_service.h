#pragma once

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QString>

#include <atomic>
#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class DbStore;
class MqttAdapter;
class PersistExecutor;

struct CommandOrderKey {
    qulonglong epoch = 0;
    qulonglong version = 0;

    bool isValid() const { return epoch > 0 && version > 0; }
};

struct CommandAcceptResult {
    int statusCode = 202;
    QString error;
    QString commandId;
    int delayMs = 0;
};

class CommandService
{
public:
    CommandService(DbStore &db,
                   MqttAdapter &mqtt,
                   PersistExecutor &persistExecutor,
                   int commandWorkerCount,
                   int queueLimit,
                   int dedupWindowSec);
    ~CommandService();

    void start();
    void stop();
    void restoreRecoverableCommands();

    CommandAcceptResult acceptCommand(const QString &roomId,
                                      const QString &deviceId,
                                      const QString &action);
    void tryAckCommands(const QString &roomId,
                        const QString &deviceId,
                        const QString &status,
                        const QString &appliedCommandId = QString(),
                        qulonglong appliedEpoch = 0,
                        qulonglong appliedVersion = 0);
    void purgeRoom(const QString &roomId);

private:
    static constexpr int kShardCount = 64;

    struct CommandRuntime {
        QString id;
        QString roomId;
        QString deviceId;
        QString action;
        QString targetStatus;
        qulonglong commandEpoch = 0;
        qulonglong deviceCommandVersion = 0;
        int retryCount = 0;
        int maxRetry = 2;
        int timeoutMs = 5000;
        QString state;
        QString errorCode;
        QString errorMessage;
        QDateTime createdAt;
        QDateTime updatedAt;
        QDateTime deadlineAt;
        qint64 retryDueAtMs = 0;
    };

    struct DeviceRuntimeState {
        QString roomId;
        QString deviceId;
        QString currentStatus;
        CommandOrderKey lastReportedOrder;
        CommandOrderKey desiredOrder;
        QString desiredStatus;
        qulonglong nextVersion = 0;
        std::optional<CommandRuntime> pending;
        std::optional<CommandRuntime> active;
        bool readyQueued = false;
    };

    struct ShardState {
        QMutex mutex;
        QHash<QString, DeviceRuntimeState> devices;
        QQueue<QString> readyDevices;
        QSet<QString> readySet;
    };

    QString targetStatusForAction(const QString &action) const;
    QString deviceKeyFor(const QString &roomId, const QString &deviceId) const;
    CommandOrderKey orderKeyFor(const CommandRuntime &job) const;
    bool orderLess(const CommandOrderKey &lhs, const CommandOrderKey &rhs) const;
    bool orderEqual(const CommandOrderKey &lhs, const CommandOrderKey &rhs) const;
    bool orderGreater(const CommandOrderKey &lhs, const CommandOrderKey &rhs) const;
    int shardIndexFor(const QString &roomId, const QString &deviceId) const;
    void startWorkers();
    void startTimeoutLoop();
    void workerLoop();
    void timeoutLoop();
    bool popReadyDevice(int &shardIndex, QString &deviceKey);
    void enqueueReadyDeviceLocked(int shardIndex, const QString &deviceKey, DeviceRuntimeState &deviceState);
    void enqueueReadyShard(int shardIndex);
    void dispatchDeviceCommand(int shardIndex, const QString &deviceKey);
    void handlePublishFailure(int shardIndex,
                              const QString &deviceKey,
                              const QString &commandId,
                              const QString &errorCode,
                              const QString &errorMessage,
                              bool retryable);
    void onTimeoutTick();
    void persistCommandError(const CommandRuntime &job, const QString &state, const QString &errorCode, const QString &errorMessage);

    DbStore &db_;
    MqttAdapter &mqtt_;
    PersistExecutor &persistExecutor_;
    std::array<ShardState, kShardCount> shards_;
    std::mutex readyMutex_;
    std::condition_variable readyCv_;
    std::deque<int> readyShards_;
    QSet<int> readyShardSet_;
    std::vector<std::thread> workerThreads_;
    std::thread timeoutThread_;
    std::atomic_bool stopWorkers_ = false;
    std::atomic_int queuedCommandCount_ = 0;
    int commandWorkerCount_ = 8;
    int queueLimit_ = 2000;
    int dedupWindowSec_ = 3;
    qulonglong currentEpoch_ = 0;
};
