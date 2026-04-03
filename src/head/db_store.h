#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>
#include <QDateTime>

/*
 * DbConfig 保存数据库连接参数。
 * 它把 host、端口、库名、用户名和密码都集中到一个地方，方便通过环境变量切换不同环境。
 */
struct DbConfig {
    QString host = qEnvironmentVariable("SIM_DB_HOST", "127.0.0.1");
    int port = []() {
        bool ok = false;
        int value = qEnvironmentVariableIntValue("SIM_DB_PORT", &ok);
        return ok ? value : 3306;
    }();
    QString dbName = qEnvironmentVariable("SIM_DB_NAME", "cmm_sim");
    QString user = qEnvironmentVariable("SIM_DB_USER");
    QString password = qEnvironmentVariable("SIM_DB_PASS");
    QString driver = qEnvironmentVariable("SIM_DB_DRIVER", "QMYSQL");
};

struct ClassroomBindingInfo {
    bool registered = false;
    QString roomId;
};

struct ClassroomRegisterResult {
    bool success = false;
    bool registered = false;
    QString roomId;
    QString error;
};

struct CommandFailureRecord {
    QString commandId;
    QString roomId;
    QString deviceId;
    QString action;
    QString desiredStatus;
    QString state;
    qulonglong commandEpoch = 0;
    qulonglong deviceCommandVersion = 0;
    int retryCount = 0;
    int maxRetry = 2;
    int timeoutMs = 5000;
    QString errorCode;
    QString errorMessage;
};

/*
 * DbStore 是项目的数据库访问层。
 * 它负责所有和 MySQL 有关的读写操作：设备、告警、命令任务、教室注册信息和全局锁状态。
 */
class DbStore
{
public:
    DbStore();
    ~DbStore();

    // 初始化驱动和数据库连接。
    bool init();
    // 读取设备状态列表。
    QJsonArray loadDevices();
    // 读取告警记录。
    QJsonArray loadAlerts(int limit = 200);
    // 读取已注册教室列表。
    QJsonArray loadRegisteredClassrooms();
    // 下面这些方法负责写入或更新业务数据。
    void upsertDevice(const QString &roomId, const QString &deviceId, const QString &name, const QString &status);
    void insertAlert(const QString &roomId, const QString &deviceId, const QString &message, const QString &level);
    bool resolveAlert(int alertId);
    int resolveAllAlerts();
    void recordOperation(const QString &roomId, const QString &deviceId, const QString &action, int delayMs);
    void setLock(bool locked);
    bool locked() const;
    bool hasRecentCommand(const QString &dedupKey, int windowSeconds);
    bool upsertCommandFailure(const CommandFailureRecord &record);
    bool createCommandJob(const QString &commandId,
                          const QString &roomId,
                          const QString &deviceId,
                          const QString &action,
                          const QString &desiredStatus,
                          const QString &state,
                          qulonglong commandEpoch,
                          qulonglong deviceCommandVersion,
                          int maxRetry,
                          int timeoutMs,
                          const QString &dedupKey);
    bool updateCommandJob(const QString &commandId,
                          const QString &state,
                          int retryCount,
                          const QString &errorCode = QString(),
                          const QString &errorMessage = QString(),
                          const QDateTime &deadlineAt = QDateTime(),
                          const QString &supersededByCommandId = QString());
    void appendCommandEvent(const QString &commandId,
                            const QString &fromState,
                            const QString &toState,
                            const QString &reason,
                            int retryNo);
    QJsonObject loadCommandJob(const QString &commandId);
    QJsonArray loadRecoverableCommands();
    bool isClassroomRegistered(const QString &roomId);
    ClassroomBindingInfo loadClassroomBindingByFingerprint(const QString &fingerprint);
    ClassroomRegisterResult registerClassroom(const QString &roomId, const QString &fingerprint = QString());
    bool unregisterClassroom(const QString &roomId);
    bool hasAdminUsers();
    QJsonArray loadAdminUsers();
    QJsonObject loadAdminUser(const QString &username);
    bool createAdminUser(const QString &username,
                         const QString &passwordSalt,
                         const QString &passwordHash,
                         int pbkdf2Iterations,
                         bool isSuper,
                         bool canChangePassword);
    bool updateAdminPassword(const QString &username,
                             const QString &passwordSalt,
                             const QString &passwordHash,
                             int pbkdf2Iterations);
    bool deleteAdminUser(const QString &username);
    qulonglong initCommandEpoch();
    qulonglong nextDeviceCommandVersion(const QString &roomId, const QString &deviceId);

private:
    DbConfig cfg_;
    QString driverName_;
    bool driverReady_ = true;
    int poolSize_ = 16;
    int activeConnections_ = 0;
    int nextConnectionIndex_ = 1;
    mutable QMutex poolMutex_;
    QWaitCondition poolCv_;
    QHash<quintptr, QString> threadConnections_;
    QHash<quintptr, int> threadLeaseCounts_;
    QHash<QString, quintptr> connectionOwners_;
    QStringList idleConnectionNames_;
    QStringList connectionNames_;

    // 底层连接和建表逻辑都放在私有区域，外部不需要直接调用。
    void applyConfig(QSqlDatabase &db);
    QSqlDatabase acquireConnection();
    void releaseConnection(const QString &connectionName);
    void closeAllConnections();
    void ensureTables(QSqlDatabase &db);
    void migrateLegacyUtcDataToShanghai(QSqlDatabase &db);
};
