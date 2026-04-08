#include "db_store.h"
#include "class_init.h"
#include "device.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QThread>

#include <functional>

/*
 * 这个文件是数据库访问层。
 * 它把 MySQL 驱动、连接管理、表结构初始化、设备/告警查询，以及命令任务的读写都封装在一起。
 */

DbStore::DbStore()
{
    driverName_ = cfg_.driver;
}

DbStore::~DbStore()
{
    closeAllConnections();
}

namespace {

struct ScopedDbLease {
    std::function<void(const QString &)> releaser;
    QSqlDatabase db;

    ScopedDbLease(std::function<void(const QString &)> releaseFn, QSqlDatabase connection)
        : releaser(std::move(releaseFn)), db(std::move(connection))
    {
    }

    ~ScopedDbLease()
    {
        if (releaser && db.isValid())
            releaser(db.connectionName());
    }

    ScopedDbLease(const ScopedDbLease &) = delete;
    ScopedDbLease &operator=(const ScopedDbLease &) = delete;
};

QString deviceVersionMetaKey(const QString &roomId, const QString &deviceId)
{
    return QStringLiteral("device_command_version:%1:%2").arg(roomId, deviceId);
}

qulonglong parseMetaUnsigned(const QVariant &value)
{
    bool ok = false;
    const qulonglong parsed = value.toULongLong(&ok);
    return ok ? parsed : 0;
}

bool isTransientCommandState(const QString &state)
{
    return state == "accepted" || state == "queued" || state == "sent" || state == "timeout";
}

QString isoDateTimeExpr(const char *columnName)
{
    return QStringLiteral("DATE_FORMAT(%1, '%Y-%m-%dT%H:%i:%s+08:00')").arg(QString::fromLatin1(columnName));
}

} // namespace

static bool columnExists(QSqlDatabase &db, const QString &tableName, const QString &columnName)
{
    QSqlQuery q(db);
    q.prepare(R"(SELECT COUNT(*) FROM information_schema.columns
                 WHERE table_schema = DATABASE()
                   AND table_name = ?
                   AND column_name = ?)");
    q.addBindValue(tableName);
    q.addBindValue(columnName);
    if (!q.exec()) {
        qWarning("查询列信息失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.next() && q.value(0).toInt() > 0;
}

static void ensureColumn(QSqlDatabase &db,
                         const QString &tableName,
                         const QString &columnName,
                         const QString &ddl)
{
    if (columnExists(db, tableName, columnName))
        return;

    QSqlQuery q(db);
    if (!q.exec(ddl))
        qWarning("补齐字段 %s.%s 失败: %s", qPrintable(tableName), qPrintable(columnName), qPrintable(q.lastError().text()));
}

// 初始化数据库驱动并尝试建立可用连接。
bool DbStore::init()
{
    if (!QSqlDatabase::isDriverAvailable(driverName_)) {
        qWarning("未找到数据库驱动 %s，请安装对应 Qt SQL 插件", qPrintable(driverName_));
        driverReady_ = false;
        return false;
    }

    bool ok = false;
    const int maxRetry = qMax(1, qEnvironmentVariableIntValue("SIM_DB_INIT_RETRIES", &ok));
    const int retries = ok ? maxRetry : 20;
    ok = false;
    const int retryDelayMs = qMax(100, qEnvironmentVariableIntValue("SIM_DB_INIT_RETRY_DELAY_MS", &ok));
    const int delayMs = ok ? retryDelayMs : 300;
    ok = false;
    const int configuredPoolSize = qEnvironmentVariableIntValue("SIM_DB_POOL_SIZE", &ok);
    poolSize_ = qMax(1, ok ? configuredPoolSize : 20);

    const QString connName = "sim_backend_probe";
    QString lastError;

    for (int attempt = 1; attempt <= retries; ++attempt) {
        bool opened = false;
        {
            auto db = QSqlDatabase::addDatabase(driverName_, connName);
            applyConfig(db);
            if (db.open()) {
                QSqlQuery tz(db);
                tz.exec(QStringLiteral("SET time_zone = '%1'").arg(ClassInit::databaseSessionTimeZone()));
                ensureTables(db);
                db.close();
                opened = true;
            } else {
                lastError = db.lastError().text();
            }
        }
        QSqlDatabase::removeDatabase(connName);

        if (opened) {
            driverReady_ = true;
            return true;
        }

        qWarning("%s 连接失败(第 %d/%d 次): host=%s port=%d db=%s err=%s",
                 qPrintable(driverName_),
                 attempt,
                 retries,
                 qPrintable(cfg_.host),
                 cfg_.port,
                 qPrintable(cfg_.dbName),
                 qPrintable(lastError));
        const QString lowered = lastError.toLower();
        if (lowered.contains("access denied") || lowered.contains("authentication")) {
            qWarning("检测到认证失败，停止重试。请检查 SIM_DB_USER / SIM_DB_PASS 或数据库授权。");
            break;
        }
        if (attempt < retries)
            QThread::msleep(delayMs);
    }

    driverReady_ = false;
    return false;
}

// 读取设备表，返回给管理端用于展示当前状态。
QJsonArray DbStore::loadDevices()
{
    QJsonArray arr;
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return arr;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(SELECT room_id, device_id, name, status,
                        %1
                 FROM devices)")
                  .arg(isoDateTimeExpr("updated_at")));
    if (!q.exec()) {
        qWarning("查询设备失败: %s", qPrintable(q.lastError().text()));
        return arr;
    }
    while (q.next()) {
        QJsonObject o;
        o["room_id"] = q.value(0).toString();
        o["device_id"] = q.value(1).toString();
        o["name"] = q.value(2).toString();
        o["status"] = q.value(3).toString();
        o["updated"] = q.value(4).toString();
        arr.append(o);
    }
    return arr;
}

// 读取告警表，按时间倒序返回最近的告警记录。
QJsonArray DbStore::loadAlerts(int limit)
{
    QJsonArray arr;
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return arr;

    if (limit <= 0)
        limit = 200;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(SELECT id, room_id, device_id, message, level,
                        %1
                 FROM alerts
                 WHERE processed=0
                 ORDER BY created_at DESC
                 LIMIT ?)")
                  .arg(isoDateTimeExpr("created_at")));
    q.addBindValue(limit);
    if (!q.exec()) {
        qWarning("查询告警失败: %s", qPrintable(q.lastError().text()));
        return arr;
    }

    while (q.next()) {
        QJsonObject o;
        o["id"] = q.value(0).toInt();
        o["room_id"] = q.value(1).toString();
        o["device_id"] = q.value(2).toString();
        o["message"] = q.value(3).toString();
        o["level"] = q.value(4).toString();
        o["created_at"] = q.value(5).toString();
        arr.append(o);
    }
    return arr;
}

// 读取已注册教室列表，供管理端和仿真端同步使用。
QJsonArray DbStore::loadRegisteredClassrooms()
{
    QJsonArray arr;
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return arr;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(SELECT room_id,
                        %1,
                        %2
                 FROM classroom_registry
                 ORDER BY room_id ASC)")
                  .arg(isoDateTimeExpr("created_at"),
                       isoDateTimeExpr("updated_at")));
    if (!q.exec()) {
        qWarning("查询教室列表失败: %s", qPrintable(q.lastError().text()));
        return arr;
    }

    while (q.next()) {
        QJsonObject o;
        o["room_id"] = q.value(0).toString();
        o["created_at"] = q.value(1).toString();
        o["updated_at"] = q.value(2).toString();
        arr.append(o);
    }

    return arr;
}

// 新增或更新一条设备状态记录。
void DbStore::upsertDevice(const QString &roomId, const QString &deviceId, const QString &name, const QString &status)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO devices(room_id, device_id, name, status, updated_at)
                VALUES(?,?,?,?,CURRENT_TIMESTAMP())
                ON DUPLICATE KEY UPDATE name=VALUES(name), status=VALUES(status), updated_at=CURRENT_TIMESTAMP())");
    q.addBindValue(roomId);
    q.addBindValue(deviceId);
    q.addBindValue(name);
    q.addBindValue(status);
    if (!q.exec()) {
        qWarning("写入设备失败: %s", qPrintable(q.lastError().text()));
    }
}

// 往告警表插入一条记录。
void DbStore::insertAlert(const QString &roomId, const QString &deviceId, const QString &message, const QString &level)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO alerts(room_id, device_id, message, level, processed, created_at)
                 VALUES(?,?,?,?,0,CURRENT_TIMESTAMP()))");
    q.addBindValue(roomId);
    q.addBindValue(deviceId);
    q.addBindValue(message);
    q.addBindValue(level);
    if (!q.exec()) {
        qWarning("写入告警失败: %s", qPrintable(q.lastError().text()));
    }
}

bool DbStore::resolveAlert(int alertId)
{
    if (alertId <= 0)
        return false;
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(UPDATE alerts
                 SET processed=1, processed_at=CURRENT_TIMESTAMP()
                 WHERE id=? AND processed=0)");
    q.addBindValue(alertId);
    if (!q.exec()) {
        qWarning("处理告警失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.numRowsAffected() > 0;
}

int DbStore::resolveAllAlerts()
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return 0;

    QSqlQuery q(db);
    q.prepare(R"(UPDATE alerts
                 SET processed=1, processed_at=CURRENT_TIMESTAMP()
                 WHERE processed=0)");
    if (!q.exec()) {
        qWarning("全部处理告警失败: %s", qPrintable(q.lastError().text()));
        return 0;
    }
    return q.numRowsAffected();
}

// 保存当前全局锁状态。
void DbStore::setLock(bool locked)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO meta(k, v) VALUES('locked', ?) ON DUPLICATE KEY UPDATE v=VALUES(v))");
    q.addBindValue(locked ? "1" : "0");
    if (!q.exec()) {
        qWarning("写入锁状态失败: %s", qPrintable(q.lastError().text()));
    }
}

// 读取当前全局锁状态。
bool DbStore::locked() const
{
    ScopedDbLease lease([this](const QString &name) { const_cast<DbStore *>(this)->releaseConnection(name); },
                        const_cast<DbStore *>(this)->acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare("SELECT v FROM meta WHERE k='locked' LIMIT 1");
    if (!q.exec())
        return false;
    if (q.next())
        return q.value(0).toString() == "1";
    return false;
}

// 用于命令去重，避免短时间内重复下发相同指令。
bool DbStore::hasRecentCommand(const QString &dedupKey, int windowSeconds)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare(R"(SELECT 1 FROM command_jobs
                 WHERE dedup_key=?
                   AND created_at >= (CURRENT_TIMESTAMP() - INTERVAL ? SECOND)
                   AND state IN ('accepted','queued','sent','timeout')
                 LIMIT 1)");
    q.addBindValue(dedupKey);
    q.addBindValue(windowSeconds);
    if (!q.exec()) {
        qWarning("查询命令幂等窗口失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.next();
}

bool DbStore::upsertCommandFailure(const CommandFailureRecord &record)
{
    if (record.commandId.trimmed().isEmpty()
        || record.state.trimmed().isEmpty()
        || (record.state != "timeout" && record.state != "failed")) {
        return false;
    }

    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO command_jobs(
                    command_id, room_id, device_id, action, desired_status, state,
                    command_epoch, device_command_version,
                    retry_count, max_retry, timeout_ms, dedup_key,
                    error_code, error_message, superseded_by_command_id,
                    deadline_at, created_at, updated_at
                 ) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?, '', NULL, CURRENT_TIMESTAMP(), CURRENT_TIMESTAMP())
                 ON DUPLICATE KEY UPDATE
                    state=VALUES(state),
                    retry_count=VALUES(retry_count),
                    max_retry=VALUES(max_retry),
                    timeout_ms=VALUES(timeout_ms),
                    error_code=VALUES(error_code),
                    error_message=VALUES(error_message),
                    updated_at=CURRENT_TIMESTAMP())");
    q.addBindValue(record.commandId);
    q.addBindValue(record.roomId);
    q.addBindValue(record.deviceId);
    q.addBindValue(record.action);
    q.addBindValue(record.desiredStatus);
    q.addBindValue(record.state);
    q.addBindValue(QString::number(record.commandEpoch));
    q.addBindValue(QString::number(record.deviceCommandVersion));
    q.addBindValue(record.retryCount);
    q.addBindValue(record.maxRetry);
    q.addBindValue(record.timeoutMs);
    q.addBindValue(QStringLiteral(""));
    q.addBindValue(record.errorCode);
    q.addBindValue(record.errorMessage);
    if (!q.exec()) {
        qWarning("写入命令错误日志失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

// 创建一条命令任务记录，后端会在内存和数据库里都保留它。
bool DbStore::createCommandJob(const QString &commandId,
                               const QString &roomId,
                               const QString &deviceId,
                               const QString &action,
                               const QString &desiredStatus,
                               const QString &state,
                               qulonglong commandEpoch,
                               qulonglong deviceCommandVersion,
                               int maxRetry,
                               int timeoutMs,
                               const QString &dedupKey)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO command_jobs(
                    command_id, room_id, device_id, action, desired_status, state,
                    command_epoch, device_command_version,
                    retry_count, max_retry, timeout_ms, dedup_key,
                    error_code, error_message, superseded_by_command_id,
                    deadline_at, created_at, updated_at
                 ) VALUES(?,?,?,?,?,?,?,?,0,?,?,?, '', '', '', NULL, CURRENT_TIMESTAMP(), CURRENT_TIMESTAMP()))");
    q.addBindValue(commandId);
    q.addBindValue(roomId);
    q.addBindValue(deviceId);
    q.addBindValue(action);
    q.addBindValue(desiredStatus);
    q.addBindValue(state);
    q.addBindValue(QString::number(commandEpoch));
    q.addBindValue(QString::number(deviceCommandVersion));
    q.addBindValue(maxRetry);
    q.addBindValue(timeoutMs);
    q.addBindValue(dedupKey);
    if (!q.exec()) {
        qWarning("写入命令任务失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

// 更新命令任务状态、重试次数和错误信息。
bool DbStore::updateCommandJob(const QString &commandId,
                               const QString &state,
                               int retryCount,
                               const QString &errorCode,
                               const QString &errorMessage,
                               const QDateTime &deadlineAt,
                               const QString &supersededByCommandId)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    if (!deadlineAt.isValid()) {
        q.prepare(R"(UPDATE command_jobs
                     SET state=?, retry_count=?, error_code=?, error_message=?, superseded_by_command_id=?, deadline_at=NULL, updated_at=CURRENT_TIMESTAMP()
                     WHERE command_id=?)");
        q.addBindValue(state);
        q.addBindValue(retryCount);
        q.addBindValue(errorCode);
        q.addBindValue(errorMessage);
        q.addBindValue(supersededByCommandId);
        q.addBindValue(commandId);
    } else {
        q.prepare(R"(UPDATE command_jobs
                     SET state=?, retry_count=?, error_code=?, error_message=?, superseded_by_command_id=?, deadline_at=?, updated_at=CURRENT_TIMESTAMP()
                     WHERE command_id=?)");
        q.addBindValue(state);
        q.addBindValue(retryCount);
        q.addBindValue(errorCode);
        q.addBindValue(errorMessage);
        q.addBindValue(supersededByCommandId);
        q.addBindValue(ClassInit::toDatabaseDateTimeString(deadlineAt));
        q.addBindValue(commandId);
    }
    if (!q.exec()) {
        qWarning("更新命令任务失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.numRowsAffected() > 0;
}

// 给命令任务追加一条状态流转事件，方便追踪任务生命周期。
void DbStore::appendCommandEvent(const QString &commandId,
                                 const QString &fromState,
                                 const QString &toState,
                                 const QString &reason,
                                 int retryNo)
{
    Q_UNUSED(commandId);
    Q_UNUSED(fromState);
    Q_UNUSED(toState);
    Q_UNUSED(reason);
    Q_UNUSED(retryNo);
}

// 查询单条命令任务详情。
QJsonObject DbStore::loadCommandJob(const QString &commandId)
{
    QJsonObject o;
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return o;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(SELECT command_id, room_id, device_id, action, desired_status, state,
                        command_epoch, device_command_version,
                        retry_count, max_retry, timeout_ms,
                        error_code, error_message, dedup_key, superseded_by_command_id,
                        %1,
                        %2,
                        %3
                 FROM command_jobs WHERE command_id=? LIMIT 1)")
                  .arg(isoDateTimeExpr("deadline_at"),
                       isoDateTimeExpr("created_at"),
                       isoDateTimeExpr("updated_at")));
    q.addBindValue(commandId);
    if (!q.exec()) {
        qWarning("查询命令任务失败: %s", qPrintable(q.lastError().text()));
        return o;
    }
    if (!q.next())
        return o;
    o["command_id"] = q.value(0).toString();
    o["room_id"] = q.value(1).toString();
    o["device_id"] = q.value(2).toString();
    o["action"] = q.value(3).toString();
    o["desired_status"] = q.value(4).toString();
    o["state"] = q.value(5).toString();
    o["command_epoch"] = QString::number(parseMetaUnsigned(q.value(6)));
    o["device_command_version"] = QString::number(parseMetaUnsigned(q.value(7)));
    o["retry_count"] = q.value(8).toInt();
    o["max_retry"] = q.value(9).toInt();
    o["timeout_ms"] = q.value(10).toInt();
    o["error_code"] = q.value(11).toString();
    o["error_message"] = q.value(12).toString();
    o["dedup_key"] = q.value(13).toString();
    o["superseded_by_command_id"] = q.value(14).toString();
    o["deadline_at"] = q.value(15).toString();
    o["created_at"] = q.value(16).toString();
    o["updated_at"] = q.value(17).toString();
    return o;
}

// 查询可以从数据库恢复到内存中的命令任务。
QJsonArray DbStore::loadRecoverableCommands()
{
    return {};
}

// 判断某个教室是否已经注册。
bool DbStore::isClassroomRegistered(const QString &roomId)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare(R"(SELECT 1 FROM classroom_registry WHERE room_id=? LIMIT 1)");
    q.addBindValue(roomId);
    if (!q.exec()) {
        qWarning("查询教室注册状态失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.next();
}

ClassroomBindingInfo DbStore::loadClassroomBindingByFingerprint(const QString &fingerprint)
{
    ClassroomBindingInfo info;
    const QString normalized = fingerprint.trimmed();
    if (normalized.isEmpty())
        return info;

    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return info;

    QSqlQuery q(db);
    q.prepare(R"(SELECT room_id FROM classroom_registry
                 WHERE client_fingerprint = ?
                 LIMIT 1)");
    q.addBindValue(normalized);
    if (!q.exec()) {
        qWarning("按特征码查询教室绑定失败: %s", qPrintable(q.lastError().text()));
        return info;
    }

    if (q.next()) {
        info.registered = true;
        info.roomId = q.value(0).toString();
    }
    return info;
}

// 注册教室：写注册表，同时补齐默认设备，并在需要时绑定终端特征码。
ClassroomRegisterResult DbStore::registerClassroom(const QString &roomId, const QString &fingerprint)
{
    ClassroomRegisterResult result;
    result.roomId = roomId.trimmed();
    result.registered = true;
    const QString normalizedFingerprint = fingerprint.trimmed();
    if (result.roomId.isEmpty()) {
        result.error = QStringLiteral("invalid_room");
        return result;
    }
    if (normalizedFingerprint.size() > 64) {
        result.error = QStringLiteral("invalid_fingerprint");
        return result;
    }

    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return result;

    if (!db.transaction()) {
        qWarning("开启注册事务失败: %s", qPrintable(db.lastError().text()));
        return result;
    }

    auto failAndRollback = [&](const QString &hint, const QSqlQuery &q) {
        qWarning("%s: %s", qPrintable(hint), qPrintable(q.lastError().text()));
        db.rollback();
        result.error = QStringLiteral("register_failed");
        return result;
    };

    if (!normalizedFingerprint.isEmpty()) {
        QSqlQuery fingerprintQuery(db);
        fingerprintQuery.prepare(R"(SELECT room_id FROM classroom_registry
                                    WHERE client_fingerprint = ?
                                    LIMIT 1)");
        fingerprintQuery.addBindValue(normalizedFingerprint);
        if (!fingerprintQuery.exec())
            return failAndRollback("查询终端特征码绑定失败", fingerprintQuery);
        if (fingerprintQuery.next()) {
            const QString boundRoomId = fingerprintQuery.value(0).toString();
            if (boundRoomId != result.roomId) {
                db.rollback();
                result.error = QStringLiteral("fingerprint_already_bound");
                return result;
            }
        }
    }

    bool roomExists = false;
    QString currentFingerprint;
    QSqlQuery roomQuery(db);
    roomQuery.prepare(R"(SELECT client_fingerprint FROM classroom_registry
                         WHERE room_id = ?
                         LIMIT 1)");
    roomQuery.addBindValue(result.roomId);
    if (!roomQuery.exec())
        return failAndRollback("查询教室注册信息失败", roomQuery);
    if (roomQuery.next()) {
        roomExists = true;
        currentFingerprint = roomQuery.value(0).toString().trimmed();
    }

    if (!normalizedFingerprint.isEmpty() && roomExists && !currentFingerprint.isEmpty()
            && currentFingerprint != normalizedFingerprint) {
        db.rollback();
        result.error = QStringLiteral("room_already_bound");
        return result;
    }

    QSqlQuery registerQuery(db);
    if (!normalizedFingerprint.isEmpty()) {
        if (roomExists) {
            if (currentFingerprint.isEmpty()) {
                registerQuery.prepare(R"(UPDATE classroom_registry
                                         SET client_fingerprint = ?, updated_at = CURRENT_TIMESTAMP()
                                         WHERE room_id = ?)");
                registerQuery.addBindValue(normalizedFingerprint);
                registerQuery.addBindValue(result.roomId);
            } else {
                registerQuery.prepare(R"(UPDATE classroom_registry
                                         SET updated_at = CURRENT_TIMESTAMP()
                                         WHERE room_id = ?)");
                registerQuery.addBindValue(result.roomId);
            }
        } else {
            registerQuery.prepare(R"(INSERT INTO classroom_registry(room_id, client_fingerprint, created_at, updated_at)
                                     VALUES(?, ?, CURRENT_TIMESTAMP(), CURRENT_TIMESTAMP()))");
            registerQuery.addBindValue(result.roomId);
            registerQuery.addBindValue(normalizedFingerprint);
        }
    } else if (roomExists) {
        registerQuery.prepare(R"(UPDATE classroom_registry
                                 SET updated_at = CURRENT_TIMESTAMP()
                                 WHERE room_id = ?)");
        registerQuery.addBindValue(result.roomId);
    } else {
        registerQuery.prepare(R"(INSERT INTO classroom_registry(room_id, created_at, updated_at)
                                 VALUES(?, CURRENT_TIMESTAMP(), CURRENT_TIMESTAMP()))");
        registerQuery.addBindValue(result.roomId);
    }
    if (!registerQuery.exec())
        return failAndRollback("写入教室注册信息失败", registerQuery);

    QSqlQuery deviceQuery(db);
    deviceQuery.prepare(R"(INSERT INTO devices(room_id, device_id, name, status, updated_at)
                          VALUES(?,?,?,?,CURRENT_TIMESTAMP())
                          ON DUPLICATE KEY UPDATE name=VALUES(name))");

    const QList<Device> defaults = defaultDevicesForRoom(result.roomId);
    for (const auto &device : defaults) {
        deviceQuery.addBindValue(result.roomId);
        deviceQuery.addBindValue(device.id);
        deviceQuery.addBindValue(device.name);
        deviceQuery.addBindValue(QStringLiteral("off"));
        if (!deviceQuery.exec())
            return failAndRollback("写入教室默认设备失败", deviceQuery);
        deviceQuery.finish();
    }

    if (!db.commit()) {
        qWarning("提交注册事务失败: %s", qPrintable(db.lastError().text()));
        db.rollback();
        result.error = QStringLiteral("register_failed");
        return result;
    }

    result.success = true;
    return result;
}

// 取消注册教室：删除和该教室相关的所有业务记录。
bool DbStore::unregisterClassroom(const QString &roomId)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;

    if (!db.transaction()) {
        qWarning("开启注销事务失败: %s", qPrintable(db.lastError().text()));
        return false;
    }

    auto failAndRollback = [&](const QString &hint, const QSqlQuery &q) {
        qWarning("%s: %s", qPrintable(hint), qPrintable(q.lastError().text()));
        db.rollback();
        return false;
    };

    QSqlQuery deleteJobs(db);
    deleteJobs.prepare(R"(DELETE FROM command_jobs WHERE room_id=?)");
    deleteJobs.addBindValue(roomId);
    if (!deleteJobs.exec())
        return failAndRollback("删除命令任务失败", deleteJobs);

    QSqlQuery deleteAlerts(db);
    deleteAlerts.prepare(R"(DELETE FROM alerts WHERE room_id=?)");
    deleteAlerts.addBindValue(roomId);
    if (!deleteAlerts.exec())
        return failAndRollback("删除告警记录失败", deleteAlerts);

    QSqlQuery deleteDevices(db);
    deleteDevices.prepare(R"(DELETE FROM devices WHERE room_id=?)");
    deleteDevices.addBindValue(roomId);
    if (!deleteDevices.exec())
        return failAndRollback("删除设备状态失败", deleteDevices);

    QSqlQuery deleteRegistry(db);
    deleteRegistry.prepare(R"(DELETE FROM classroom_registry WHERE room_id=?)");
    deleteRegistry.addBindValue(roomId);
    if (!deleteRegistry.exec())
        return failAndRollback("删除教室注册信息失败", deleteRegistry);

    if (!db.commit()) {
        qWarning("提交注销事务失败: %s", qPrintable(db.lastError().text()));
        db.rollback();
        return false;
    }
    return true;
}

bool DbStore::hasAdminUsers()
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(SELECT 1 FROM admin_users WHERE enabled=1 LIMIT 1)");
    if (!q.exec()) {
        qWarning("查询管理员失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.next();
}

QJsonArray DbStore::loadAdminUsers()
{
    QJsonArray arr;
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return arr;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(SELECT username, is_super, can_change_password, enabled,
                        %1,
                        %2
                 FROM admin_users
                 ORDER BY is_super DESC, created_at ASC, username ASC)")
                  .arg(isoDateTimeExpr("created_at"),
                       isoDateTimeExpr("password_updated_at")));
    if (!q.exec()) {
        qWarning("读取管理员列表失败: %s", qPrintable(q.lastError().text()));
        return arr;
    }

    while (q.next()) {
        QJsonObject obj;
        obj["username"] = q.value(0).toString();
        obj["is_super"] = q.value(1).toBool();
        obj["can_change_password"] = q.value(2).toBool();
        obj["enabled"] = q.value(3).toBool();
        obj["created_at"] = q.value(4).toString();
        obj["password_updated_at"] = q.value(5).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonObject DbStore::loadAdminUser(const QString &username)
{
    QJsonObject obj;
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return obj;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(R"(SELECT id, username, password_salt, password_hash,
                        pbkdf2_iterations, enabled, is_super, can_change_password,
                        %1
                 FROM admin_users
                 WHERE username=? LIMIT 1)")
                  .arg(isoDateTimeExpr("password_updated_at")));
    q.addBindValue(username);
    if (!q.exec()) {
        qWarning("读取管理员失败: %s", qPrintable(q.lastError().text()));
        return obj;
    }
    if (!q.next())
        return obj;

    obj["id"] = q.value(0).toInt();
    obj["username"] = q.value(1).toString();
    obj["password_salt"] = q.value(2).toString();
    obj["password_hash"] = q.value(3).toString();
    obj["pbkdf2_iterations"] = q.value(4).toInt();
    obj["enabled"] = q.value(5).toBool();
    obj["is_super"] = q.value(6).toBool();
    obj["can_change_password"] = q.value(7).toBool();
    obj["password_updated_at"] = q.value(8).toString();
    return obj;
}

bool DbStore::createAdminUser(const QString &username,
                              const QString &passwordSalt,
                              const QString &passwordHash,
                              int pbkdf2Iterations,
                              bool isSuper,
                              bool canChangePassword)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO admin_users(
                    username, password_salt, password_hash, pbkdf2_iterations,
                    enabled, is_super, can_change_password,
                    created_at, updated_at, password_updated_at
                 ) VALUES(?,?,?,?,1,?,?,CURRENT_TIMESTAMP(),CURRENT_TIMESTAMP(),CURRENT_TIMESTAMP()))");
    q.addBindValue(username);
    q.addBindValue(passwordSalt);
    q.addBindValue(passwordHash);
    q.addBindValue(pbkdf2Iterations);
    q.addBindValue(isSuper ? 1 : 0);
    q.addBindValue(canChangePassword ? 1 : 0);
    if (!q.exec()) {
        qWarning("创建管理员失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

bool DbStore::updateAdminPassword(const QString &username,
                                  const QString &passwordSalt,
                                  const QString &passwordHash,
                                  int pbkdf2Iterations)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(UPDATE admin_users
                 SET password_salt=?,
                     password_hash=?,
                     pbkdf2_iterations=?,
                     updated_at=CURRENT_TIMESTAMP(),
                     password_updated_at=CURRENT_TIMESTAMP()
                 WHERE username=? LIMIT 1)");
    q.addBindValue(passwordSalt);
    q.addBindValue(passwordHash);
    q.addBindValue(pbkdf2Iterations);
    q.addBindValue(username);
    if (!q.exec()) {
        qWarning("更新管理员密码失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool DbStore::deleteAdminUser(const QString &username)
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(DELETE FROM admin_users
                 WHERE username=? AND is_super=0
                 LIMIT 1)");
    q.addBindValue(username);
    if (!q.exec()) {
        qWarning("删除管理员失败: %s", qPrintable(q.lastError().text()));
        return false;
    }
    return q.numRowsAffected() > 0;
}

qulonglong DbStore::initCommandEpoch()
{
    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return 0;

    if (!db.transaction()) {
        qWarning("开启命令 epoch 事务失败: %s", qPrintable(db.lastError().text()));
        return 0;
    }

    QSqlQuery readQuery(db);
    readQuery.prepare("SELECT v FROM meta WHERE k=? LIMIT 1 FOR UPDATE");
    readQuery.addBindValue(QStringLiteral("command_epoch"));
    if (!readQuery.exec()) {
        qWarning("读取命令 epoch 失败: %s", qPrintable(readQuery.lastError().text()));
        db.rollback();
        return 0;
    }

    qulonglong nextEpoch = 1;
    if (readQuery.next())
        nextEpoch = parseMetaUnsigned(readQuery.value(0)) + 1;

    QSqlQuery writeQuery(db);
    writeQuery.prepare(R"(INSERT INTO meta(k, v)
                          VALUES(?, ?)
                          ON DUPLICATE KEY UPDATE v=VALUES(v))");
    writeQuery.addBindValue(QStringLiteral("command_epoch"));
    writeQuery.addBindValue(QString::number(nextEpoch));
    if (!writeQuery.exec()) {
        qWarning("写入命令 epoch 失败: %s", qPrintable(writeQuery.lastError().text()));
        db.rollback();
        return 0;
    }

    if (!db.commit()) {
        qWarning("提交命令 epoch 失败: %s", qPrintable(db.lastError().text()));
        db.rollback();
        return 0;
    }

    return nextEpoch;
}

qulonglong DbStore::nextDeviceCommandVersion(const QString &roomId, const QString &deviceId)
{
    if (roomId.trimmed().isEmpty() || deviceId.trimmed().isEmpty())
        return 0;

    ScopedDbLease lease([this](const QString &name) { releaseConnection(name); }, acquireConnection());
    auto db = lease.db;
    if (!db.isValid() || !db.isOpen())
        return 0;

    if (!db.transaction()) {
        qWarning("开启设备命令版号事务失败: %s", qPrintable(db.lastError().text()));
        return 0;
    }

    const QString key = deviceVersionMetaKey(roomId.trimmed(), deviceId.trimmed());
    QSqlQuery readQuery(db);
    readQuery.prepare("SELECT v FROM meta WHERE k=? LIMIT 1 FOR UPDATE");
    readQuery.addBindValue(key);
    if (!readQuery.exec()) {
        qWarning("读取设备命令版号失败: %s", qPrintable(readQuery.lastError().text()));
        db.rollback();
        return 0;
    }

    qulonglong nextVersion = 1;
    if (readQuery.next())
        nextVersion = parseMetaUnsigned(readQuery.value(0)) + 1;

    QSqlQuery writeQuery(db);
    writeQuery.prepare(R"(INSERT INTO meta(k, v)
                          VALUES(?, ?)
                          ON DUPLICATE KEY UPDATE v=VALUES(v))");
    writeQuery.addBindValue(key);
    writeQuery.addBindValue(QString::number(nextVersion));
    if (!writeQuery.exec()) {
        qWarning("写入设备命令版号失败: %s", qPrintable(writeQuery.lastError().text()));
        db.rollback();
        return 0;
    }

    if (!db.commit()) {
        qWarning("提交设备命令版号失败: %s", qPrintable(db.lastError().text()));
        db.rollback();
        return 0;
    }

    return nextVersion;
}

// 把配置参数应用到 QSqlDatabase 对象上。
void DbStore::applyConfig(QSqlDatabase &db)
{
    db.setHostName(cfg_.host);
    db.setPort(cfg_.port);
    db.setDatabaseName(cfg_.dbName);
    db.setUserName(cfg_.user);
    db.setPassword(cfg_.password);
}

QSqlDatabase DbStore::acquireConnection()
{
    if (!driverReady_)
        return {};

    const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    QString connName;
    bool newlyAssigned = false;

    while (connName.isEmpty()) {
        QString evictedConnName;
        {
            QMutexLocker locker(&poolMutex_);
            auto existing = threadConnections_.constFind(threadId);
            if (existing != threadConnections_.cend()) {
                connName = existing.value();
                threadLeaseCounts_[threadId] = threadLeaseCounts_.value(threadId, 0) + 1;
                break;
            }

            if (activeConnections_ < poolSize_) {
                connName = QString("sim_backend_pool_%1_%2")
                               .arg(threadId)
                               .arg(nextConnectionIndex_++);
                threadConnections_.insert(threadId, connName);
                connectionOwners_.insert(connName, threadId);
                threadLeaseCounts_.insert(threadId, 1);
                connectionNames_.append(connName);
                activeConnections_ += 1;
                newlyAssigned = true;
                break;
            }

            if (!idleConnectionNames_.isEmpty()) {
                evictedConnName = idleConnectionNames_.takeFirst();
                const quintptr ownerThreadId = connectionOwners_.take(evictedConnName);
                threadConnections_.remove(ownerThreadId);
                threadLeaseCounts_.remove(ownerThreadId);
                connectionNames_.removeAll(evictedConnName);
                activeConnections_ = qMax(0, activeConnections_ - 1);
            } else {
                poolCv_.wait(&poolMutex_);
                continue;
            }
        }

        if (!evictedConnName.isEmpty()) {
            if (QSqlDatabase::contains(evictedConnName))
                QSqlDatabase::removeDatabase(evictedConnName);
        }
    }

    QSqlDatabase db = QSqlDatabase::contains(connName)
        ? QSqlDatabase::database(connName, false)
        : QSqlDatabase::addDatabase(driverName_, connName);
    if (!db.isOpen())
        applyConfig(db);

    if (!db.isOpen() && !db.open()) {
        qWarning("%s 打开失败: %s", qPrintable(driverName_), qPrintable(db.lastError().text()));
        db = QSqlDatabase();
        QMutexLocker rollbackLocker(&poolMutex_);
        threadConnections_.remove(threadId);
        threadLeaseCounts_.remove(threadId);
        connectionOwners_.remove(connName);
        idleConnectionNames_.removeAll(connName);
        connectionNames_.removeAll(connName);
        activeConnections_ = qMax(0, activeConnections_ - 1);
        poolCv_.wakeOne();
        if (QSqlDatabase::contains(connName))
            QSqlDatabase::removeDatabase(connName);
        return {};
    }

    if (newlyAssigned) {
        QSqlQuery tz(db);
        tz.exec(QStringLiteral("SET time_zone = '%1'").arg(ClassInit::databaseSessionTimeZone()));
    }
    return db;
}

void DbStore::releaseConnection(const QString &connectionName)
{
    if (connectionName.isEmpty())
        return;

    const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    QMutexLocker locker(&poolMutex_);
    auto leaseIt = threadLeaseCounts_.find(threadId);
    if (leaseIt == threadLeaseCounts_.end())
        return;

    *leaseIt -= 1;
    if (*leaseIt > 0)
        return;

    threadLeaseCounts_.remove(threadId);
    if (!idleConnectionNames_.contains(connectionName))
        idleConnectionNames_.append(connectionName);
    poolCv_.wakeOne();
}

void DbStore::closeAllConnections()
{
    QStringList names;
    {
        QMutexLocker locker(&poolMutex_);
        names = connectionNames_;
        threadConnections_.clear();
        threadLeaseCounts_.clear();
        connectionOwners_.clear();
        idleConnectionNames_.clear();
        connectionNames_.clear();
        activeConnections_ = 0;
        poolCv_.wakeAll();
    }

    for (const auto &name : names) {
        if (!QSqlDatabase::contains(name))
            continue;
        QSqlDatabase::removeDatabase(name);
    }
}

// 初始化业务表结构，表不存在时就自动创建。
void DbStore::ensureTables(QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.exec(R"(CREATE TABLE IF NOT EXISTS devices(
        room_id VARCHAR(64) NOT NULL,
        device_id VARCHAR(64) NOT NULL,
        name VARCHAR(128) DEFAULT '',
        status VARCHAR(32) DEFAULT 'off',
        updated_at DATETIME,
        PRIMARY KEY(room_id, device_id)
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS alerts(
        id INT AUTO_INCREMENT PRIMARY KEY,
        room_id VARCHAR(64),
        device_id VARCHAR(64),
        message TEXT,
        level VARCHAR(32),
        processed TINYINT(1) NOT NULL DEFAULT 0,
        processed_at DATETIME NULL,
        created_at DATETIME
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS meta(
        k VARCHAR(255) PRIMARY KEY,
        v VARCHAR(255)
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS command_jobs(
        command_id VARCHAR(64) PRIMARY KEY,
        room_id VARCHAR(64) NOT NULL,
        device_id VARCHAR(64) NOT NULL,
        action VARCHAR(32) NOT NULL,
        desired_status VARCHAR(32) NOT NULL DEFAULT '',
        state VARCHAR(16) NOT NULL,
        command_epoch BIGINT UNSIGNED NOT NULL DEFAULT 0,
        device_command_version BIGINT UNSIGNED NOT NULL DEFAULT 0,
        retry_count INT NOT NULL DEFAULT 0,
        max_retry INT NOT NULL DEFAULT 2,
        timeout_ms INT NOT NULL DEFAULT 5000,
        error_code VARCHAR(32) DEFAULT '',
        error_message VARCHAR(255) DEFAULT '',
        dedup_key VARCHAR(128) NOT NULL,
        superseded_by_command_id VARCHAR(64) DEFAULT '',
        deadline_at DATETIME NULL,
        created_at DATETIME NOT NULL,
        updated_at DATETIME NOT NULL,
        KEY idx_command_state_deadline(state, deadline_at),
        KEY idx_command_dedup(dedup_key, created_at),
        KEY idx_command_device(room_id, device_id, created_at),
        KEY idx_command_order(room_id, device_id, command_epoch, device_command_version)
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS classroom_registry(
        room_id VARCHAR(64) PRIMARY KEY,
        created_at DATETIME NOT NULL,
        updated_at DATETIME NOT NULL
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS admin_users(
        id INT AUTO_INCREMENT PRIMARY KEY,
        username VARCHAR(64) NOT NULL UNIQUE,
        password_salt VARCHAR(255) NOT NULL,
        password_hash VARCHAR(255) NOT NULL,
        pbkdf2_iterations INT NOT NULL,
        enabled TINYINT(1) NOT NULL DEFAULT 1,
        created_at DATETIME NOT NULL,
        updated_at DATETIME NOT NULL,
        password_updated_at DATETIME NOT NULL
    ))");

    ensureColumn(db,
                 "alerts",
                 "processed",
                 "ALTER TABLE alerts ADD COLUMN processed TINYINT(1) NOT NULL DEFAULT 0 AFTER level");
    ensureColumn(db,
                 "alerts",
                 "processed_at",
                 "ALTER TABLE alerts ADD COLUMN processed_at DATETIME NULL AFTER processed");
    ensureColumn(db,
                 "classroom_registry",
                 "client_fingerprint",
                 "ALTER TABLE classroom_registry ADD COLUMN client_fingerprint VARCHAR(64) NULL UNIQUE AFTER room_id");
    ensureColumn(db,
                 "admin_users",
                 "is_super",
                 "ALTER TABLE admin_users ADD COLUMN is_super TINYINT(1) NOT NULL DEFAULT 0 AFTER enabled");
    ensureColumn(db,
                 "admin_users",
                 "can_change_password",
                 "ALTER TABLE admin_users ADD COLUMN can_change_password TINYINT(1) NOT NULL DEFAULT 1 AFTER is_super");
    q.exec(R"(ALTER TABLE meta MODIFY COLUMN k VARCHAR(255) NOT NULL)");
    ensureColumn(db,
                 "command_jobs",
                 "desired_status",
                 "ALTER TABLE command_jobs ADD COLUMN desired_status VARCHAR(32) NOT NULL DEFAULT '' AFTER action");
    ensureColumn(db,
                 "command_jobs",
                 "command_epoch",
                 "ALTER TABLE command_jobs ADD COLUMN command_epoch BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER state");
    ensureColumn(db,
                 "command_jobs",
                 "device_command_version",
                 "ALTER TABLE command_jobs ADD COLUMN device_command_version BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER command_epoch");
    ensureColumn(db,
                 "command_jobs",
                 "superseded_by_command_id",
                 "ALTER TABLE command_jobs ADD COLUMN superseded_by_command_id VARCHAR(64) DEFAULT '' AFTER dedup_key");
    q.exec(R"(DROP TABLE IF EXISTS operations)");
    q.exec(R"(DROP TABLE IF EXISTS command_events)");
    q.exec(R"(DELETE FROM meta WHERE k='command_epoch' OR k LIKE 'device_command_version:%')");

    QSqlQuery superCount(db);
    superCount.prepare(R"(SELECT COUNT(*) FROM admin_users WHERE is_super=1)");
    int count = 0;
    if (superCount.exec() && superCount.next())
        count = superCount.value(0).toInt();

    if (count == 0) {
        QSqlQuery promote(db);
        promote.exec(R"(UPDATE admin_users
                        SET is_super=1, can_change_password=0
                        WHERE id = (
                            SELECT id FROM (
                                SELECT id FROM admin_users ORDER BY id ASC LIMIT 1
                            ) AS bootstrap_admin
                        ))");
    }

    QSqlQuery fixSuper(db);
    fixSuper.exec(R"(UPDATE admin_users SET can_change_password=0 WHERE is_super=1)");

    migrateLegacyUtcDataToShanghai(db);
}

void DbStore::migrateLegacyUtcDataToShanghai(QSqlDatabase &db)
{
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT v FROM meta WHERE k=? LIMIT 1");
    checkQuery.addBindValue(QStringLiteral("migration_shanghai_time_v1"));
    if (!checkQuery.exec()) {
        qWarning("查询上海时区迁移标记失败: %s", qPrintable(checkQuery.lastError().text()));
        return;
    }
    if (checkQuery.next() && checkQuery.value(0).toString() == QLatin1String("done"))
        return;

    if (!db.transaction()) {
        qWarning("开启上海时区迁移事务失败: %s", qPrintable(db.lastError().text()));
        return;
    }

    auto execMigration = [&](const QString &sql, const char *hint) -> bool {
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            qWarning("%s: %s", hint, qPrintable(q.lastError().text()));
            db.rollback();
            return false;
        }
        return true;
    };

    if (!execMigration("UPDATE devices SET updated_at = DATE_ADD(updated_at, INTERVAL 8 HOUR) WHERE updated_at IS NOT NULL",
                       "迁移 devices.updated_at 失败")
        || !execMigration("UPDATE alerts SET created_at = DATE_ADD(created_at, INTERVAL 8 HOUR) WHERE created_at IS NOT NULL",
                          "迁移 alerts.created_at 失败")
        || !execMigration("UPDATE alerts SET processed_at = DATE_ADD(processed_at, INTERVAL 8 HOUR) WHERE processed_at IS NOT NULL",
                          "迁移 alerts.processed_at 失败")
        || !execMigration("UPDATE command_jobs SET deadline_at = DATE_ADD(deadline_at, INTERVAL 8 HOUR) WHERE deadline_at IS NOT NULL",
                          "迁移 command_jobs.deadline_at 失败")
        || !execMigration("UPDATE command_jobs SET created_at = DATE_ADD(created_at, INTERVAL 8 HOUR) WHERE created_at IS NOT NULL",
                          "迁移 command_jobs.created_at 失败")
        || !execMigration("UPDATE command_jobs SET updated_at = DATE_ADD(updated_at, INTERVAL 8 HOUR) WHERE updated_at IS NOT NULL",
                          "迁移 command_jobs.updated_at 失败")
        || !execMigration("UPDATE classroom_registry SET created_at = DATE_ADD(created_at, INTERVAL 8 HOUR) WHERE created_at IS NOT NULL",
                          "迁移 classroom_registry.created_at 失败")
        || !execMigration("UPDATE classroom_registry SET updated_at = DATE_ADD(updated_at, INTERVAL 8 HOUR) WHERE updated_at IS NOT NULL",
                          "迁移 classroom_registry.updated_at 失败")
        || !execMigration("UPDATE admin_users SET created_at = DATE_ADD(created_at, INTERVAL 8 HOUR) WHERE created_at IS NOT NULL",
                          "迁移 admin_users.created_at 失败")
        || !execMigration("UPDATE admin_users SET updated_at = DATE_ADD(updated_at, INTERVAL 8 HOUR) WHERE updated_at IS NOT NULL",
                          "迁移 admin_users.updated_at 失败")
        || !execMigration("UPDATE admin_users SET password_updated_at = DATE_ADD(password_updated_at, INTERVAL 8 HOUR) WHERE password_updated_at IS NOT NULL",
                          "迁移 admin_users.password_updated_at 失败")) {
        return;
    }

    QSqlQuery markQuery(db);
    markQuery.prepare(R"(INSERT INTO meta(k, v)
                         VALUES(?, 'done')
                         ON DUPLICATE KEY UPDATE v=VALUES(v))");
    markQuery.addBindValue(QStringLiteral("migration_shanghai_time_v1"));
    if (!markQuery.exec()) {
        qWarning("写入上海时区迁移标记失败: %s", qPrintable(markQuery.lastError().text()));
        db.rollback();
        return;
    }

    if (!db.commit()) {
        qWarning("提交上海时区迁移事务失败: %s", qPrintable(db.lastError().text()));
        db.rollback();
        return;
    }

    qInfo("数据库历史时间已迁移为上海时区。");
}
