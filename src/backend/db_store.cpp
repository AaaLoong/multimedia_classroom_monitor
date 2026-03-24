#include "db_store.h"
#include "device.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>

/*
 * 这个文件是数据库访问层。
 * 它把 MySQL 驱动、连接管理、表结构初始化、设备/告警查询，以及命令任务的读写都封装在一起。
 */

DbStore::DbStore()
{
    driverName_ = cfg_.driver;
}

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

    const QString connName = "sim_backend_probe";
    QString lastError;

    for (int attempt = 1; attempt <= retries; ++attempt) {
        bool opened = false;
        {
            auto db = QSqlDatabase::addDatabase(driverName_, connName);
            applyConfig(db);
            if (db.open()) {
                QSqlQuery tz(db);
                tz.exec("SET time_zone = '+00:00'");
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return arr;
    QSqlQuery q(db);
    q.prepare(R"(SELECT room_id, device_id, name, status,
                        DATE_FORMAT(updated_at, '%Y-%m-%dT%H:%i:%sZ')
                 FROM devices)");
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return arr;

    if (limit <= 0)
        limit = 200;

    QSqlQuery q(db);
    q.prepare(R"(SELECT room_id, device_id, message, level,
                        DATE_FORMAT(created_at, '%Y-%m-%dT%H:%i:%sZ')
                 FROM alerts ORDER BY created_at DESC LIMIT ?)");
    q.addBindValue(limit);
    if (!q.exec()) {
        qWarning("查询告警失败: %s", qPrintable(q.lastError().text()));
        return arr;
    }

    while (q.next()) {
        QJsonObject o;
        o["room_id"] = q.value(0).toString();
        o["device_id"] = q.value(1).toString();
        o["message"] = q.value(2).toString();
        o["level"] = q.value(3).toString();
        o["created_at"] = q.value(4).toString();
        arr.append(o);
    }
    return arr;
}

// 读取已注册教室列表，供管理端和仿真端同步使用。
QJsonArray DbStore::loadRegisteredClassrooms()
{
    QJsonArray arr;
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return arr;

    QSqlQuery q(db);
    q.prepare(R"(SELECT room_id,
                        DATE_FORMAT(created_at, '%Y-%m-%dT%H:%i:%sZ'),
                        DATE_FORMAT(updated_at, '%Y-%m-%dT%H:%i:%sZ')
                 FROM classroom_registry
                 ORDER BY room_id ASC)");
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO devices(room_id, device_id, name, status, updated_at)
                VALUES(?,?,?,?,UTC_TIMESTAMP())
                ON DUPLICATE KEY UPDATE name=VALUES(name), status=VALUES(status), updated_at=UTC_TIMESTAMP())");
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO alerts(room_id, device_id, message, level, created_at)
                 VALUES(?,?,?,?,UTC_TIMESTAMP()))");
    q.addBindValue(roomId);
    q.addBindValue(deviceId);
    q.addBindValue(message);
    q.addBindValue(level);
    if (!q.exec()) {
        qWarning("写入告警失败: %s", qPrintable(q.lastError().text()));
    }
}

// 记录一次设备操作及其执行耗时。
void DbStore::recordOperation(const QString &roomId, const QString &deviceId, const QString &action, int delayMs)
{
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO operations(room_id, device_id, action, delay_ms, created_at)
                 VALUES(?,?,?,?,UTC_TIMESTAMP()))");
    q.addBindValue(roomId);
    q.addBindValue(deviceId);
    q.addBindValue(action);
    q.addBindValue(delayMs);
    if (!q.exec()) {
        qWarning("写入操作记录失败: %s", qPrintable(q.lastError().text()));
    }
}

// 保存当前全局锁状态。
void DbStore::setLock(bool locked)
{
    auto db = connectionForThread();
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
    auto db = const_cast<DbStore *>(this)->connectionForThread();
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare(R"(SELECT 1 FROM command_jobs
                 WHERE dedup_key=?
                   AND created_at >= (UTC_TIMESTAMP() - INTERVAL ? SECOND)
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

// 创建一条命令任务记录，后端会在内存和数据库里都保留它。
bool DbStore::createCommandJob(const QString &commandId,
                               const QString &roomId,
                               const QString &deviceId,
                               const QString &action,
                               const QString &state,
                               int maxRetry,
                               int timeoutMs,
                               const QString &dedupKey)
{
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO command_jobs(
                    command_id, room_id, device_id, action, state,
                    retry_count, max_retry, timeout_ms, dedup_key,
                    error_code, error_message, deadline_at, created_at, updated_at
                 ) VALUES(?,?,?,?,?,0,?,?,?, '', '', NULL, UTC_TIMESTAMP(), UTC_TIMESTAMP()))");
    q.addBindValue(commandId);
    q.addBindValue(roomId);
    q.addBindValue(deviceId);
    q.addBindValue(action);
    q.addBindValue(state);
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
                               const QDateTime &deadlineAt)
{
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    if (!deadlineAt.isValid()) {
        q.prepare(R"(UPDATE command_jobs
                     SET state=?, retry_count=?, error_code=?, error_message=?, deadline_at=NULL, updated_at=UTC_TIMESTAMP()
                     WHERE command_id=?)");
        q.addBindValue(state);
        q.addBindValue(retryCount);
        q.addBindValue(errorCode);
        q.addBindValue(errorMessage);
        q.addBindValue(commandId);
    } else {
        q.prepare(R"(UPDATE command_jobs
                     SET state=?, retry_count=?, error_code=?, error_message=?, deadline_at=?, updated_at=UTC_TIMESTAMP()
                     WHERE command_id=?)");
        q.addBindValue(state);
        q.addBindValue(retryCount);
        q.addBindValue(errorCode);
        q.addBindValue(errorMessage);
        q.addBindValue(deadlineAt.toUTC().toString("yyyy-MM-dd HH:mm:ss"));
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return;
    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO command_events(command_id, from_state, to_state, reason, retry_no, created_at)
                 VALUES(?,?,?,?,?,UTC_TIMESTAMP()))");
    q.addBindValue(commandId);
    q.addBindValue(fromState);
    q.addBindValue(toState);
    q.addBindValue(reason);
    q.addBindValue(retryNo);
    if (!q.exec()) {
        qWarning("写入命令事件失败: %s", qPrintable(q.lastError().text()));
    }
}

// 查询单条命令任务详情。
QJsonObject DbStore::loadCommandJob(const QString &commandId)
{
    QJsonObject o;
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return o;
    QSqlQuery q(db);
    q.prepare(R"(SELECT command_id, room_id, device_id, action, state, retry_count, max_retry, timeout_ms,
                        error_code, error_message, dedup_key,
                        DATE_FORMAT(deadline_at, '%Y-%m-%dT%H:%i:%sZ'),
                        DATE_FORMAT(created_at, '%Y-%m-%dT%H:%i:%sZ'),
                        DATE_FORMAT(updated_at, '%Y-%m-%dT%H:%i:%sZ')
                 FROM command_jobs WHERE command_id=? LIMIT 1)");
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
    o["state"] = q.value(4).toString();
    o["retry_count"] = q.value(5).toInt();
    o["max_retry"] = q.value(6).toInt();
    o["timeout_ms"] = q.value(7).toInt();
    o["error_code"] = q.value(8).toString();
    o["error_message"] = q.value(9).toString();
    o["dedup_key"] = q.value(10).toString();
    o["deadline_at"] = q.value(11).toString();
    o["created_at"] = q.value(12).toString();
    o["updated_at"] = q.value(13).toString();
    return o;
}

// 查询可以从数据库恢复到内存中的命令任务。
QJsonArray DbStore::loadRecoverableCommands()
{
    QJsonArray arr;
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return arr;
    QSqlQuery q(db);
    q.prepare(R"(SELECT command_id, room_id, device_id, action, state, retry_count, max_retry, timeout_ms,
                        error_code, error_message, dedup_key,
                        DATE_FORMAT(deadline_at, '%Y-%m-%dT%H:%i:%sZ'),
                        DATE_FORMAT(created_at, '%Y-%m-%dT%H:%i:%sZ'),
                        DATE_FORMAT(updated_at, '%Y-%m-%dT%H:%i:%sZ')
                 FROM command_jobs
                 WHERE state IN ('accepted','queued','sent','timeout')
                 ORDER BY created_at ASC
                 LIMIT 5000)");
    if (!q.exec()) {
        qWarning("查询可恢复命令失败: %s", qPrintable(q.lastError().text()));
        return arr;
    }
    while (q.next()) {
        QJsonObject o;
        o["command_id"] = q.value(0).toString();
        o["room_id"] = q.value(1).toString();
        o["device_id"] = q.value(2).toString();
        o["action"] = q.value(3).toString();
        o["state"] = q.value(4).toString();
        o["retry_count"] = q.value(5).toInt();
        o["max_retry"] = q.value(6).toInt();
        o["timeout_ms"] = q.value(7).toInt();
        o["error_code"] = q.value(8).toString();
        o["error_message"] = q.value(9).toString();
        o["dedup_key"] = q.value(10).toString();
        o["deadline_at"] = q.value(11).toString();
        o["created_at"] = q.value(12).toString();
        o["updated_at"] = q.value(13).toString();
        arr.append(o);
    }
    return arr;
}

// 判断某个教室是否已经注册。
bool DbStore::isClassroomRegistered(const QString &roomId)
{
    auto db = connectionForThread();
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

// 注册教室：写注册表，同时补齐默认设备。
bool DbStore::registerClassroom(const QString &roomId)
{
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return false;

    if (!db.transaction()) {
        qWarning("开启注册事务失败: %s", qPrintable(db.lastError().text()));
        return false;
    }

    auto failAndRollback = [&](const QString &hint, const QSqlQuery &q) {
        qWarning("%s: %s", qPrintable(hint), qPrintable(q.lastError().text()));
        db.rollback();
        return false;
    };

    QSqlQuery registerQuery(db);
    registerQuery.prepare(R"(INSERT INTO classroom_registry(room_id, created_at, updated_at)
                            VALUES(?, UTC_TIMESTAMP(), UTC_TIMESTAMP())
                            ON DUPLICATE KEY UPDATE updated_at=UTC_TIMESTAMP())");
    registerQuery.addBindValue(roomId);
    if (!registerQuery.exec())
        return failAndRollback("写入教室注册信息失败", registerQuery);

    QSqlQuery deviceQuery(db);
    deviceQuery.prepare(R"(INSERT INTO devices(room_id, device_id, name, status, updated_at)
                          VALUES(?,?,?,?,UTC_TIMESTAMP())
                          ON DUPLICATE KEY UPDATE name=VALUES(name))");

    const QList<Device> defaults = defaultDevicesForRoom(roomId);
    for (const auto &device : defaults) {
        deviceQuery.addBindValue(roomId);
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
        return false;
    }
    return true;
}

// 取消注册教室：删除和该教室相关的所有业务记录。
bool DbStore::unregisterClassroom(const QString &roomId)
{
    auto db = connectionForThread();
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

    QSqlQuery deleteEvents(db);
    deleteEvents.prepare(R"(DELETE ce FROM command_events ce
                           INNER JOIN command_jobs cj ON ce.command_id = cj.command_id
                           WHERE cj.room_id = ?)");
    deleteEvents.addBindValue(roomId);
    if (!deleteEvents.exec())
        return failAndRollback("删除命令事件失败", deleteEvents);

    QSqlQuery deleteJobs(db);
    deleteJobs.prepare(R"(DELETE FROM command_jobs WHERE room_id=?)");
    deleteJobs.addBindValue(roomId);
    if (!deleteJobs.exec())
        return failAndRollback("删除命令任务失败", deleteJobs);

    QSqlQuery deleteOperations(db);
    deleteOperations.prepare(R"(DELETE FROM operations WHERE room_id=?)");
    deleteOperations.addBindValue(roomId);
    if (!deleteOperations.exec())
        return failAndRollback("删除操作记录失败", deleteOperations);

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
    auto db = connectionForThread();
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return arr;

    QSqlQuery q(db);
    q.prepare(R"(SELECT username, is_super, can_change_password, enabled,
                        DATE_FORMAT(created_at, '%Y-%m-%dT%H:%i:%sZ'),
                        DATE_FORMAT(password_updated_at, '%Y-%m-%dT%H:%i:%sZ')
                 FROM admin_users
                 ORDER BY is_super DESC, created_at ASC, username ASC)");
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return obj;

    QSqlQuery q(db);
    q.prepare(R"(SELECT id, username, password_salt, password_hash,
                        pbkdf2_iterations, enabled, is_super, can_change_password,
                        DATE_FORMAT(password_updated_at, '%Y-%m-%dT%H:%i:%sZ')
                 FROM admin_users
                 WHERE username=? LIMIT 1)");
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(INSERT INTO admin_users(
                    username, password_salt, password_hash, pbkdf2_iterations,
                    enabled, is_super, can_change_password,
                    created_at, updated_at, password_updated_at
                 ) VALUES(?,?,?,?,1,?,?,UTC_TIMESTAMP(),UTC_TIMESTAMP(),UTC_TIMESTAMP()))");
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
    auto db = connectionForThread();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(R"(UPDATE admin_users
                 SET password_salt=?,
                     password_hash=?,
                     pbkdf2_iterations=?,
                     updated_at=UTC_TIMESTAMP(),
                     password_updated_at=UTC_TIMESTAMP()
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
    auto db = connectionForThread();
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

// 把配置参数应用到 QSqlDatabase 对象上。
void DbStore::applyConfig(QSqlDatabase &db)
{
    db.setHostName(cfg_.host);
    db.setPort(cfg_.port);
    db.setDatabaseName(cfg_.dbName);
    db.setUserName(cfg_.user);
    db.setPassword(cfg_.password);
}

// 每个线程使用独立数据库连接，避免 Qt SQL 的线程安全问题。
QSqlDatabase DbStore::connectionForThread()
{
    if (!driverReady_)
        return {};
    const qulonglong id = reinterpret_cast<qulonglong>(QThread::currentThreadId());
    QMutexLocker locker(&mutex_);
    if (!connections_.contains(id)) {
        const QString connName = QString("sim_backend_%1").arg(id);
        auto db = QSqlDatabase::addDatabase(driverName_, connName);
        applyConfig(db);
        if (!db.open()) {
            qWarning("%s 打开失败: %s", qPrintable(driverName_), qPrintable(db.lastError().text()));
        } else {
            QSqlQuery tz(db);
            tz.exec("SET time_zone = '+00:00'");
            ensureTables(db);
        }
        connections_[id] = connName;
        return db;
    }
    auto db = QSqlDatabase::database(connections_.value(id));
    if (!db.isOpen()) {
        db.open();
        QSqlQuery tz(db);
        tz.exec("SET time_zone = '+00:00'");
    }
    return db;
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
        created_at DATETIME
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS operations(
        id INT AUTO_INCREMENT PRIMARY KEY,
        room_id VARCHAR(64),
        device_id VARCHAR(64),
        action VARCHAR(32),
        delay_ms INT,
        created_at DATETIME
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS meta(
        k VARCHAR(64) PRIMARY KEY,
        v VARCHAR(255)
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS command_jobs(
        command_id VARCHAR(64) PRIMARY KEY,
        room_id VARCHAR(64) NOT NULL,
        device_id VARCHAR(64) NOT NULL,
        action VARCHAR(32) NOT NULL,
        state VARCHAR(16) NOT NULL,
        retry_count INT NOT NULL DEFAULT 0,
        max_retry INT NOT NULL DEFAULT 2,
        timeout_ms INT NOT NULL DEFAULT 5000,
        error_code VARCHAR(32) DEFAULT '',
        error_message VARCHAR(255) DEFAULT '',
        dedup_key VARCHAR(128) NOT NULL,
        deadline_at DATETIME NULL,
        created_at DATETIME NOT NULL,
        updated_at DATETIME NOT NULL,
        KEY idx_command_state_deadline(state, deadline_at),
        KEY idx_command_dedup(dedup_key, created_at),
        KEY idx_command_device(room_id, device_id, created_at)
    ))");
    q.exec(R"(CREATE TABLE IF NOT EXISTS command_events(
        id BIGINT AUTO_INCREMENT PRIMARY KEY,
        command_id VARCHAR(64) NOT NULL,
        from_state VARCHAR(16) DEFAULT '',
        to_state VARCHAR(16) NOT NULL,
        reason VARCHAR(64) DEFAULT '',
        retry_no INT NOT NULL DEFAULT 0,
        created_at DATETIME NOT NULL,
        KEY idx_command_events_id(command_id, created_at)
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
                 "admin_users",
                 "is_super",
                 "ALTER TABLE admin_users ADD COLUMN is_super TINYINT(1) NOT NULL DEFAULT 0 AFTER enabled");
    ensureColumn(db,
                 "admin_users",
                 "can_change_password",
                 "ALTER TABLE admin_users ADD COLUMN can_change_password TINYINT(1) NOT NULL DEFAULT 1 AFTER is_super");

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
}
