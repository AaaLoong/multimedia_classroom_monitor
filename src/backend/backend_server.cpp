#include <httplib.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QRandomGenerator>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QWaitCondition>
#include <QPasswordDigestor>
#include <QtGlobal>
#include <QtConcurrent>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "db_store.h"
#include "device.h"
#include "mqtt_adapter.h"

/*
 * 这个文件是后端服务的主入口，也是业务最集中的地方。
 * 它把 HTTP 接口、MQTT 通信、数据库读写、命令队列、超时重试和状态回写串在一起，
 * 形成一个完整的“教室管理后端”。
 */

struct DeviceState {
    QString name;
    QString status;
    QString updated;
};

// 命令任务在运行时需要保存的全部状态。
struct CommandRuntime {
    QString id;
    QString roomId;
    QString deviceId;
    QString action;
    QString targetStatus;
    QString dedupKey;
    QString state; // 命令状态：已接收、排队、已发送、已确认、超时、失败
    int retryCount = 0;
    int maxRetry = 2;
    int timeoutMs = 5000;
    QString errorCode;
    QString errorMessage;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime deadlineAt;
};

struct AdminSession {
    QString token;
    QString username;
    QDateTime issuedAt;
    QDateTime expiresAt;
};

// 把设备状态转换成系统内部统一使用的英文状态码。
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

static QByteArray randomBytes(int length)
{
    QByteArray bytes(length, Qt::Uninitialized);
    for (int i = 0; i < length; ++i)
        bytes[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return bytes;
}

static QByteArray derivePasswordHash(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                              password.toUtf8(),
                                              salt,
                                              iterations,
                                              32);
}

static bool constantTimeEquals(const QByteArray &lhs, const QByteArray &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    unsigned char diff = 0;
    for (int i = 0; i < lhs.size(); ++i)
        diff |= static_cast<unsigned char>(lhs.at(i) ^ rhs.at(i));
    return diff == 0;
}

static constexpr int kPasswordSaltLength = 16;
static constexpr int kPasswordHashLength = 32;
static constexpr int kPasswordIterations = 210000;
static constexpr int kSessionTtlSeconds = 8 * 60 * 60;

class BackendServer : public QObject
{
    Q_OBJECT
public:
    // 构造函数只做对象初始化和路由绑定，不真正启动服务。
    BackendServer(QObject *parent = nullptr)
        : QObject(parent),
          mqttUrl_(qEnvironmentVariable("SIM_MQTT_URL", "tcp://127.0.0.1:1883")),
          mqtt_(mqttUrl_)
    {
        connect(&mqtt_, &MqttAdapter::stateSignal, this, &BackendServer::onState);
        connect(&mqtt_, &MqttAdapter::alertSignal, this, &BackendServer::onAlert);

        bool okThreads = false;
        int workerFromEnv = qEnvironmentVariableIntValue("SIM_WORKER_THREADS", &okThreads);
        commandWorkerCount_ = qMax(8, okThreads ? workerFromEnv : 16);
        okThreads = false;
        int queueFromEnv = qEnvironmentVariableIntValue("SIM_QUEUE_MAX", &okThreads);
        queueLimit_ = qMax(500, okThreads ? queueFromEnv : 5000);
        okThreads = false;
        int dedupFromEnv = qEnvironmentVariableIntValue("SIM_DEDUP_WINDOW_SEC", &okThreads);
        dedupWindowSec_ = qMax(1, okThreads ? dedupFromEnv : 3);

        bindHost_ = qEnvironmentVariable("SIM_BIND_HOST", "127.0.0.1").trimmed();
        port_ = qEnvironmentVariableIntValue("SIM_BACKEND_PORT", &okThreads);
        if (!okThreads || port_ <= 0)
            port_ = 8080;
        server_ = std::make_unique<httplib::Server>();
        bindRoutes();
    }

    // 析构时停止工作线程，防止后台线程和 Qt 对象生命周期冲突。
    ~BackendServer() override
    {
        stopWorkers_ = true;
        commandCv_.wakeAll();
        for (auto &t : workerThreads_) {
            if (t.joinable())
                t.join();
        }
        if (timeoutThread_.joinable())
            timeoutThread_.join();
    }

    // 真正启动后端：初始化数据库、恢复任务、启动线程、开始监听端口。
    void start()
    {
        if (!db_.init()) {
            qCritical("MySQL 驱动不可用或连接失败。");
            QCoreApplication::quit();
            return;
        }

        if (!bootstrapAdminUser()) {
            QCoreApplication::quit();
            return;
        }

        if (!mqtt_.connect())
            qWarning("MQTT 初始连接失败，后端会依赖后续自动重连。");

        {
            QMutexLocker locker(&dataMutex_);
            locked_ = db_.locked();
            const auto arr = db_.loadDevices();
            for (const auto &item : arr) {
                const auto obj = item.toObject();
                DeviceState s;
                s.name = obj.value("name").toString();
                s.status = obj.value("status").toString();
                s.updated = obj.value("updated").toString();
                const QString key = obj.value("room_id").toString() + "::" + obj.value("device_id").toString();
                devices_[key] = s;
            }
        }

        restoreRecoverableCommands();
        startWorkers();
        startTimeoutLoop();

        qInfo("backend listen on http://%s:%d", qPrintable(bindHost_), port_);
        if (!server_->listen(bindHost_.toStdString(), port_)) {
            qCritical("后端监听失败: %s:%d", qPrintable(bindHost_), port_);
            QCoreApplication::quit();
        }
    }

private:
    QString mqttUrl_;
    MqttAdapter mqtt_;
    std::unique_ptr<httplib::Server> server_;
    DbStore db_;
    mutable QMutex dataMutex_;
    mutable QMutex commandMutex_;
    mutable QMutex sessionMutex_;
    QWaitCondition commandCv_;
    QHash<QString, DeviceState> devices_;
    QHash<QString, CommandRuntime> commandJobs_;
    QHash<QString, AdminSession> sessions_;
    QQueue<QString> commandQueue_;
    std::vector<std::thread> workerThreads_;
    std::thread timeoutThread_;
    std::atomic_bool stopWorkers_ = false;
    bool locked_ = false;
    int commandWorkerCount_ = 8;
    int queueLimit_ = 2000;
    int dedupWindowSec_ = 3;
    QString bindHost_;
    int port_ = 8080;

    /*
     * HTTP 路由注册区。
     * 这里按功能分成：认证/锁定、设备与告警查询、教室注册、设备控制、报修上报。
     */
    void bindRoutes()
    {
        // 锁状态查询和健康检查是最基础的两条公共接口。
        server_->Get("/health", [&](const httplib::Request &, httplib::Response &res) {
            respondJson(res, QJsonDocument(QJsonObject{
                             {"status", "ok"},
                             {"transport", "http"}
                         }));
        });

        server_->Get("/lock", [&](const httplib::Request &, httplib::Response &res) {
            QJsonObject obj;
            {
                QMutexLocker locker(&dataMutex_);
                obj["locked"] = locked_;
            }
            respondJson(res, QJsonDocument(obj));
        });

        server_->Post("/login", [&](const httplib::Request &req, httplib::Response &res) {
            const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
            const QString user = body.value("username").toString().trimmed();
            const QString pwd = body.value("password").toString();
            if (!authenticateAdmin(user, pwd)) {
                respondError(res, "invalid_credentials", 401);
                return;
            }

            const QString token = issueSession(user);
            QMutexLocker locker(&sessionMutex_);
            const auto session = sessions_.value(token);
            respondJson(res, QJsonDocument(QJsonObject{
                             {"token", token},
                             {"username", session.username},
                             {"expires_at", session.expiresAt.toUTC().toString(Qt::ISODate)}
                         }));
        });

        server_->Post("/logout", [&](const httplib::Request &req, httplib::Response &res) {
            const QString token = bearerTokenFromRequest(req);
            if (token.isEmpty() || !revokeSession(token)) {
                respondError(res, "unauthorized", 401);
                return;
            }
            respondJson(res, QJsonDocument(QJsonObject{{"status", "logged_out"}}));
        });

        server_->Post("/lock", [&](const httplib::Request &req, httplib::Response &res) {
            if (!isAuthorized(req)) {
                respondError(res, "unauthorized", 401);
                return;
            }
            {
                QMutexLocker locker(&dataMutex_);
                locked_ = true;
            }
            db_.setLock(true);
            respondJson(res, QJsonDocument(QJsonObject{{"status", "locked"}, {"locked", true}}));
        });

        server_->Post("/unlock", [&](const httplib::Request &req, httplib::Response &res) {
            if (!isAuthorized(req)) {
                respondError(res, "unauthorized", 401);
                return;
            }
            {
                QMutexLocker locker(&dataMutex_);
                locked_ = false;
            }
            db_.setLock(false);
            respondJson(res, QJsonDocument(QJsonObject{{"status", "unlocked"}, {"locked", false}}));
        });

        // 设备列表和告警列表供管理端页面直接展示。
        server_->Get("/devices", [&](const httplib::Request &req, httplib::Response &res) {
            pushDevicesResponse(req, res);
        });

        server_->Get("/alerts", [&](const httplib::Request &req, httplib::Response &res) {
            if (!isAuthorized(req)) {
                respondError(res, "unauthorized", 401);
                return;
            }
            int limit = 200;
            if (req.has_param("limit")) {
                bool ok = false;
                const int parsed = QString::fromStdString(req.get_param_value("limit")).toInt(&ok);
                if (ok && parsed > 0)
                    limit = parsed;
            }
            respondJson(res, QJsonDocument(db_.loadAlerts(limit)));
        });

        // 教室注册列表和注册状态查询，供教室端和仿真端同步使用。
        server_->Get("/classrooms", [&](const httplib::Request &, httplib::Response &res) {
            QJsonObject resp;
            resp["classrooms"] = db_.loadRegisteredClassrooms();
            respondJson(res, QJsonDocument(resp));
        });

        server_->Post(R"(/heartbeat/([\w\-]+))", [&](const httplib::Request &req, httplib::Response &res) {
            const QString roomId = QString::fromStdString(req.matches[1]);
            if (!db_.isClassroomRegistered(roomId)) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "room_not_registered"}}), 403);
                return;
            }
            mqtt_.publishHeartbeat(roomId);
            respondJson(res, QJsonDocument(QJsonObject{{"status", "sent"}}));
        });

        server_->Get(R"(/classrooms/([\w\-]+)/registration)", [&](const httplib::Request &req, httplib::Response &res) {
            const QString roomId = QString::fromStdString(req.matches[1]);
            const bool registered = db_.isClassroomRegistered(roomId);
            respondJson(res, QJsonDocument(QJsonObject{
                             {"room_id", roomId},
                             {"registered", registered}
                         }));
        });

        server_->Post(R"(/classrooms/([\w\-]+)/register)", [&](const httplib::Request &req, httplib::Response &res) {
            const QString roomId = QString::fromStdString(req.matches[1]);
            if (roomId.isEmpty()) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "invalid_room"}}), 400);
                return;
            }
            const bool ok = db_.registerClassroom(roomId);
            if (!ok) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "register_failed"}}), 500);
                return;
            }
            seedRuntimeDevicesForRoom(roomId);
            respondJson(res, QJsonDocument(QJsonObject{
                             {"room_id", roomId},
                             {"registered", true},
                             {"status", "registered"}
                         }));
        });

        server_->Post(R"(/classrooms/([\w\-]+)/unregister)", [&](const httplib::Request &req, httplib::Response &res) {
            const QString roomId = QString::fromStdString(req.matches[1]);
            if (roomId.isEmpty()) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "invalid_room"}}), 400);
                return;
            }
            const bool ok = db_.unregisterClassroom(roomId);
            if (!ok) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "unregister_failed"}}), 500);
                return;
            }
            purgeRuntimeForRoom(roomId);
            respondJson(res, QJsonDocument(QJsonObject{
                             {"room_id", roomId},
                             {"registered", false},
                             {"status", "unregistered"}
                         }));
        });

        // 设备控制命令会进入命令队列，由后台工作线程异步发送到 MQTT。
        server_->Post(R"(/devices/([\w\-]+)/([\w\-]+)/command)", [&](const httplib::Request &req, httplib::Response &res) {
            {
                QMutexLocker locker(&dataMutex_);
                if (locked_) {
                    respondJson(res, QJsonDocument(QJsonObject{{"error", "locked"}}), 423);
                    return;
                }
            }

            const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
            const QString action = body.value("action").toString();
            const QString roomId = QString::fromStdString(req.matches[1]);
            const QString deviceId = QString::fromStdString(req.matches[2]);
            if (!db_.isClassroomRegistered(roomId)) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "room_not_registered"}}), 403);
                return;
            }
            if (action.isEmpty()) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "empty_action"}}), 400);
                return;
            }
            const QString dedupKey = QString("%1::%2::%3").arg(roomId, deviceId, action.toLower());
            if (db_.hasRecentCommand(dedupKey, dedupWindowSec_)) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "duplicate_command"}}), 409);
                return;
            }

            CommandRuntime job;
            job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            job.roomId = roomId;
            job.deviceId = deviceId;
            job.action = action;
            job.targetStatus = targetStatusForAction(action);
            job.dedupKey = dedupKey;
            job.state = "accepted";
            job.maxRetry = 2;
            job.timeoutMs = 5000;
            job.createdAt = QDateTime::currentDateTimeUtc();
            job.updatedAt = job.createdAt;

            {
                QMutexLocker locker(&commandMutex_);
                if (commandQueue_.size() >= queueLimit_) {
                    respondJson(res, QJsonDocument(QJsonObject{{"error", "queue_busy"}}), 503);
                    return;
                }
                commandJobs_[job.id] = job;
            }

            db_.createCommandJob(job.id, roomId, deviceId, action, "accepted", job.maxRetry, job.timeoutMs, dedupKey);
            db_.appendCommandEvent(job.id, "", "accepted", "api_accept", 0);
            moveToQueued(job.id, "api_accept");

            respondJson(res, QJsonDocument(QJsonObject{
                             {"status", "accepted"},
                             {"delay_ms", 0},
                             {"command_id", job.id}
                         }),
                        202);
        });

        server_->Get(R"(/commands/([\w\-]+))", [&](const httplib::Request &req, httplib::Response &res) {
            if (!isAuthorized(req)) {
                respondError(res, "unauthorized", 401);
                return;
            }
            const QString commandId = QString::fromStdString(req.matches[1]);
            const auto obj = db_.loadCommandJob(commandId);
            if (obj.isEmpty()) {
                res.status = 404;
                return;
            }
            respondJson(res, QJsonDocument(obj));
        });

        // 报修接口会同时写告警和 MQTT 告警，保证管理端和仿真端都能看到。
        server_->Post(R"(/devices/([\w\-]+)/([\w\-]+)/report)", [&](const httplib::Request &req, httplib::Response &res) {
            {
                QMutexLocker locker(&dataMutex_);
                if (locked_) {
                    respondJson(res, QJsonDocument(QJsonObject{{"error", "locked"}}), 423);
                    return;
                }
            }
            const QString roomId = QString::fromStdString(req.matches[1]);
            const QString deviceId = QString::fromStdString(req.matches[2]);
            if (!db_.isClassroomRegistered(roomId)) {
                respondJson(res, QJsonDocument(QJsonObject{{"error", "room_not_registered"}}), 403);
                return;
            }
            const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
            const QString msg = body.value("message").toString("报修");
            mqtt_.publishReport(roomId, deviceId, msg);
            db_.insertAlert(roomId, deviceId, msg, "repair");
            respondJson(res, QJsonDocument(QJsonObject{{"status", "submitted"}}));
        });
    }

    // 首次启动时引导创建管理员账号，彻底移除代码里的默认明文口令。
    bool bootstrapAdminUser()
    {
        if (db_.hasAdminUsers())
            return true;

        const QString username = qEnvironmentVariable("SIM_ADMIN_INIT_USERNAME").trimmed();
        const QString password = qEnvironmentVariable("SIM_ADMIN_INIT_PASSWORD");
        if (username.isEmpty() || password.isEmpty()) {
            qCritical("未发现管理员账号，请设置 SIM_ADMIN_INIT_USERNAME 和 SIM_ADMIN_INIT_PASSWORD 后重启后端。");
            return false;
        }

        const QByteArray salt = randomBytes(kPasswordSaltLength);
        const QByteArray hash = derivePasswordHash(password, salt, kPasswordIterations);
        const bool ok = db_.createAdminUser(username,
                                            QString::fromUtf8(salt.toBase64()),
                                            QString::fromUtf8(hash.toBase64()),
                                            kPasswordIterations);
        if (!ok) {
            qCritical("初始化管理员账号失败。");
            return false;
        }

        qInfo("已初始化首个管理员账号: %s", qPrintable(username));
        return true;
    }

    // 程序重启后，把数据库里未完成的命令恢复回内存队列。
    void restoreRecoverableCommands()
    {
        const QJsonArray arr = db_.loadRecoverableCommands();
        const auto now = QDateTime::currentDateTimeUtc();
        for (const auto &item : arr) {
            const auto obj = item.toObject();
            CommandRuntime job;
            job.id = obj.value("command_id").toString();
            job.roomId = obj.value("room_id").toString();
            job.deviceId = obj.value("device_id").toString();
            job.action = obj.value("action").toString();
            job.targetStatus = targetStatusForAction(job.action);
            job.state = obj.value("state").toString();
            job.retryCount = obj.value("retry_count").toInt();
            job.maxRetry = obj.value("max_retry").toInt(2);
            job.timeoutMs = obj.value("timeout_ms").toInt(5000);
            job.errorCode = obj.value("error_code").toString();
            job.errorMessage = obj.value("error_message").toString();
            job.dedupKey = obj.value("dedup_key").toString();
            job.createdAt = QDateTime::fromString(obj.value("created_at").toString(), Qt::ISODate);
            job.updatedAt = QDateTime::fromString(obj.value("updated_at").toString(), Qt::ISODate);
            job.deadlineAt = QDateTime::fromString(obj.value("deadline_at").toString(), Qt::ISODate);

            {
                QMutexLocker locker(&commandMutex_);
                commandJobs_[job.id] = job;
            }

            if (job.state == "accepted" || job.state == "queued" || job.state == "timeout") {
                moveToQueued(job.id, "recover_queue");
            } else if (job.state == "sent" && (!job.deadlineAt.isValid() || job.deadlineAt <= now)) {
                handleCommandTimeout(job.id);
            }
        }
    }

    // 启动命令工作线程池，负责从队列里取任务并向 MQTT 发布。
    void startWorkers()
    {
        stopWorkers_ = false;
        qInfo("command workers starting: %d", commandWorkerCount_);
        workerThreads_.clear();
        for (int i = 0; i < commandWorkerCount_; ++i) {
            workerThreads_.emplace_back([this]() { workerLoop(); });
        }
    }

    // 启动超时巡检线程，定时检查命令是否迟迟没有 ACK。
    void startTimeoutLoop()
    {
        timeoutThread_ = std::thread([this]() {
            qInfo("command timeout loop ready");
            while (!stopWorkers_) {
                QThread::msleep(250);
                onCommandTimeoutTick();
            }
        });
    }

    // 把前端动作名转换成设备最终应该达到的状态码。
    QString targetStatusForAction(const QString &action) const
    {
        const QString a = action.trimmed().toLower();
        if (a == "on" || a == "start" || a == "open" || a == "power_on")
            return "on";
        if (a == "off" || a == "stop" || a == "close" || a == "power_off")
            return "off";
        return a;
    }

    // 从 Authorization 头中提取 Bearer Token。
    QString bearerTokenFromRequest(const httplib::Request &req) const
    {
        if (!req.has_header("Authorization"))
            return {};

        const QString header = QString::fromStdString(req.get_header_value("Authorization")).trimmed();
        if (!header.startsWith("Bearer "))
            return {};
        return header.mid(7).trimmed();
    }

    // 每次鉴权前顺手清理过期会话，避免内存里残留无效 token。
    void pruneExpiredSessions()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QMutexLocker locker(&sessionMutex_);
        QStringList expiredTokens;
        for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
            if (it.value().expiresAt <= now)
                expiredTokens.append(it.key());
        }
        for (const auto &token : expiredTokens)
            sessions_.remove(token);
    }

    // 为登录成功的管理员发放一个 8 小时有效的随机会话 token。
    QString issueSession(const QString &username)
    {
        pruneExpiredSessions();

        AdminSession session;
        session.token = QString::fromUtf8(randomBytes(32).toBase64(QByteArray::Base64UrlEncoding |
                                                                   QByteArray::OmitTrailingEquals));
        session.username = username;
        session.issuedAt = QDateTime::currentDateTimeUtc();
        session.expiresAt = session.issuedAt.addSecs(kSessionTtlSeconds);

        QMutexLocker locker(&sessionMutex_);
        sessions_.insert(session.token, session);
        return session.token;
    }

    // 注销指定 token，会在登出时调用。
    bool revokeSession(const QString &token)
    {
        pruneExpiredSessions();
        QMutexLocker locker(&sessionMutex_);
        return sessions_.remove(token) > 0;
    }

    // 登录校验基于数据库中的 PBKDF2 哈希，不再比较明文字符串。
    bool authenticateAdmin(const QString &username, const QString &password)
    {
        const QJsonObject user = db_.loadAdminUser(username);
        const bool userExists = !user.isEmpty() && user.value("enabled").toBool(false);
        const QByteArray storedSalt = userExists
            ? QByteArray::fromBase64(user.value("password_salt").toString().toUtf8())
            : QByteArray(kPasswordSaltLength, '\0');
        const QByteArray storedHash = userExists
            ? QByteArray::fromBase64(user.value("password_hash").toString().toUtf8())
            : QByteArray(kPasswordHashLength, '\0');
        const int iterations = userExists
            ? qMax(1, user.value("pbkdf2_iterations").toInt(kPasswordIterations))
            : kPasswordIterations;

        const QByteArray safeSalt = storedSalt.size() == kPasswordSaltLength
            ? storedSalt
            : QByteArray(kPasswordSaltLength, '\0');
        const QByteArray safeHash = storedHash.size() == kPasswordHashLength
            ? storedHash
            : QByteArray(kPasswordHashLength, '\0');
        const QByteArray actualHash = derivePasswordHash(password, safeSalt, iterations);
        const bool match = constantTimeEquals(actualHash, safeHash);
        return userExists && match;
    }

    // Bearer Token 校验基于内存会话表。
    bool isAuthorized(const httplib::Request &req)
    {
        pruneExpiredSessions();
        const QString token = bearerTokenFromRequest(req);
        if (token.isEmpty())
            return false;
        QMutexLocker locker(&sessionMutex_);
        return sessions_.contains(token);
    }

    // 统一把 QJsonDocument 写回 HTTP 响应。
    void respondJson(httplib::Response &res, const QJsonDocument &doc, int status = 200)
    {
        res.status = status;
        res.set_content(doc.toJson(QJsonDocument::Compact).toStdString(), "application/json");
    }

    // 常见错误响应统一走这里，保证前端能拿到一致的 error 字段。
    void respondError(httplib::Response &res, const QString &error, int status)
    {
        respondJson(res, QJsonDocument(QJsonObject{{"error", error}}), status);
    }

    // 设备列表响应的拼装逻辑：支持管理员全量查看，也支持按教室过滤。
    void pushDevicesResponse(const httplib::Request &req, httplib::Response &res)
    {
        const bool isAdmin = isAuthorized(req);
        const QString roomFilter = req.has_param("room_id")
            ? QString::fromStdString(req.get_param_value("room_id")).trimmed()
            : QString();
        if (!isAdmin && roomFilter.isEmpty()) {
            respondError(res, "unauthorized", 401);
            return;
        }
        if (!roomFilter.isEmpty() && !isAdmin && !db_.isClassroomRegistered(roomFilter)) {
            bool lockedCopy = false;
            {
                QMutexLocker locker(&dataMutex_);
                lockedCopy = locked_;
            }
            QJsonObject resp;
            resp["locked"] = lockedCopy;
            resp["devices"] = QJsonArray();
            respondJson(res, QJsonDocument(resp));
            return;
        }
        QJsonArray arr;
        bool lockedCopy = false;
        {
            QMutexLocker locker(&dataMutex_);
            lockedCopy = locked_;
            if (!locked_ || isAdmin) {
                for (auto it = devices_.cbegin(); it != devices_.cend(); ++it) {
                    const auto parts = it.key().split("::");
                    if (parts.size() != 2)
                        continue;
                    if (!roomFilter.isEmpty() && parts.at(0) != roomFilter)
                        continue;
                    QJsonObject o;
                    o["room_id"] = parts.at(0);
                    o["device_id"] = parts.at(1);
                    o["name"] = it.value().name;
                    o["status"] = it.value().status;
                    o["updated"] = it.value().updated;
                    arr.append(o);
                }
            }
        }
        QJsonObject resp;
        resp["locked"] = lockedCopy;
        resp["devices"] = arr;
        respondJson(res, QJsonDocument(resp));
    }

    // 教室取消注册时，要把该教室的运行态、命令和队列一起清理掉。
    void purgeRuntimeForRoom(const QString &roomId)
    {
        {
            QMutexLocker locker(&dataMutex_);
            const QString prefix = roomId + "::";
            QStringList keysToRemove;
            for (auto it = devices_.cbegin(); it != devices_.cend(); ++it) {
                if (it.key().startsWith(prefix))
                    keysToRemove.append(it.key());
            }
            for (const auto &k : keysToRemove)
                devices_.remove(k);
        }

        {
            QMutexLocker locker(&commandMutex_);
            QSet<QString> removeIds;
            for (auto it = commandJobs_.cbegin(); it != commandJobs_.cend(); ++it) {
                if (it.value().roomId == roomId)
                    removeIds.insert(it.key());
            }
            for (const auto &id : removeIds)
                commandJobs_.remove(id);

            if (!removeIds.isEmpty()) {
                QQueue<QString> filtered;
                while (!commandQueue_.isEmpty()) {
                    const QString id = commandQueue_.dequeue();
                    if (!removeIds.contains(id))
                        filtered.enqueue(id);
                }
                commandQueue_ = filtered;
            }
        }
    }

    // 教室刚注册时，给它补一份默认设备运行态，避免列表为空。
    void seedRuntimeDevicesForRoom(const QString &roomId)
    {
        const QList<Device> defaults = defaultDevicesForRoom(roomId);
        const QString updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        QMutexLocker locker(&dataMutex_);
        for (const auto &device : defaults) {
            const QString key = roomId + "::" + device.id;
            if (devices_.contains(key))
                continue;

            DeviceState state;
            state.name = device.name;
            state.status = "off";
            state.updated = updatedAt;
            devices_.insert(key, state);
        }
    }

    // 工作线程主循环：阻塞等待命令，然后派发到 MQTT。
    void workerLoop()
    {
        while (!stopWorkers_) {
            QString commandId;
            {
                QMutexLocker locker(&commandMutex_);
                if (commandQueue_.isEmpty() && !stopWorkers_) {
                    commandCv_.wait(&commandMutex_, 200);
                }
                if (stopWorkers_)
                    return;
                if (commandQueue_.isEmpty())
                    continue;
                commandId = commandQueue_.dequeue();
            }
            dispatchCommand(commandId);
        }
    }

    // 把命令任务从当前状态推进到 queued，并送进工作队列。
    void moveToQueued(const QString &commandId, const QString &reason)
    {
        {
            QMutexLocker locker(&commandMutex_);
            if (!commandJobs_.contains(commandId))
                return;
            auto &job = commandJobs_[commandId];
            const QString fromState = job.state;
            job.state = "queued";
            job.updatedAt = QDateTime::currentDateTimeUtc();
            db_.updateCommandJob(job.id, job.state, job.retryCount);
            db_.appendCommandEvent(job.id, fromState, "queued", reason, job.retryCount);
            commandQueue_.enqueue(job.id);
        }
        commandCv_.wakeOne();
    }

    // 从队列里取出命令，真正发布到 MQTT。
    void dispatchCommand(const QString &commandId)
    {
        CommandRuntime snapshot;
        {
            QMutexLocker locker(&commandMutex_);
            if (!commandJobs_.contains(commandId))
                return;
            auto &job = commandJobs_[commandId];
            if (job.state != "queued")
                return;
            const QString fromState = job.state;
            job.state = "sent";
            job.updatedAt = QDateTime::currentDateTimeUtc();
            job.deadlineAt = QDateTime::currentDateTimeUtc().addMSecs(job.timeoutMs);
            db_.updateCommandJob(job.id, "sent", job.retryCount, "", "", job.deadlineAt);
            db_.appendCommandEvent(job.id, fromState, "sent", "worker_dispatch", job.retryCount);
            snapshot = job;
        }

        const bool published = mqtt_.publishCommand(snapshot.roomId, snapshot.deviceId, snapshot.action);
        db_.recordOperation(snapshot.roomId, snapshot.deviceId, snapshot.action, 0);
        if (!published) {
            handleCommandFailure(commandId, "E_PUBLISH", "mqtt_publish_failed", true);
        }
    }

    // 周期性检查是否有命令超时未 ACK。
    void onCommandTimeoutTick()
    {
        QSet<QString> timedOutIds;
        const auto now = QDateTime::currentDateTimeUtc();
        {
            QMutexLocker locker(&commandMutex_);
            for (auto it = commandJobs_.cbegin(); it != commandJobs_.cend(); ++it) {
                if (it.value().state == "sent" && it.value().deadlineAt.isValid() && it.value().deadlineAt <= now)
                    timedOutIds.insert(it.key());
            }
        }
        if (!timedOutIds.isEmpty())
            qInfo("command timeout tick: %d", int(timedOutIds.size()));
        for (const auto &id : timedOutIds) {
            handleCommandTimeout(id);
        }
    }

    // 单个命令超时后的处理：记录超时、判断是否重试。
    void handleCommandTimeout(const QString &commandId)
    {
        int backoffMs = 0;
        bool shouldRetry = false;
        {
            QMutexLocker locker(&commandMutex_);
            if (!commandJobs_.contains(commandId))
                return;
            auto &job = commandJobs_[commandId];
            if (job.state != "sent")
                return;
            const QString fromState = job.state;
            job.state = "timeout";
            job.updatedAt = QDateTime::currentDateTimeUtc();
            db_.updateCommandJob(job.id, "timeout", job.retryCount, "E_TIMEOUT", "ack_timeout");
            db_.appendCommandEvent(job.id, fromState, "timeout", "ack_timeout", job.retryCount);

            if (job.retryCount < job.maxRetry) {
                job.retryCount += 1;
                shouldRetry = true;
                backoffMs = qMin(4000, 500 * (1 << job.retryCount));
            } else {
                job.state = "failed";
                job.errorCode = "E_TIMEOUT";
                job.errorMessage = "ack_timeout_final";
                db_.updateCommandJob(job.id, "failed", job.retryCount, job.errorCode, job.errorMessage);
                db_.appendCommandEvent(job.id, "timeout", "failed", "retry_exhausted", job.retryCount);
            }
        }

        if (shouldRetry) {
            std::thread([this, commandId, backoffMs]() {
                QThread::msleep(backoffMs);
                moveToQueued(commandId, "retry_after_timeout");
            }).detach();
        }
    }

    // MQTT 发布失败后的处理：记录失败，并根据重试上限决定是否回退重试。
    void handleCommandFailure(const QString &commandId,
                              const QString &errorCode,
                              const QString &errorMessage,
                              bool retryable)
    {
        int backoffMs = 0;
        bool shouldRetry = false;
        {
            QMutexLocker locker(&commandMutex_);
            if (!commandJobs_.contains(commandId))
                return;
            auto &job = commandJobs_[commandId];
            const QString fromState = job.state;
            if (retryable && job.retryCount < job.maxRetry) {
                job.retryCount += 1;
                job.state = "queued";
                job.errorCode = errorCode;
                job.errorMessage = errorMessage;
                job.updatedAt = QDateTime::currentDateTimeUtc();
                db_.updateCommandJob(job.id, "queued", job.retryCount, errorCode, errorMessage);
                db_.appendCommandEvent(job.id, fromState, "queued", "retry_after_publish_failed", job.retryCount);
                shouldRetry = true;
                backoffMs = qMin(4000, 500 * (1 << job.retryCount));
            } else {
                job.state = "failed";
                job.errorCode = errorCode;
                job.errorMessage = errorMessage;
                job.updatedAt = QDateTime::currentDateTimeUtc();
                db_.updateCommandJob(job.id, "failed", job.retryCount, errorCode, errorMessage);
                db_.appendCommandEvent(job.id, fromState, "failed", "publish_failed", job.retryCount);
            }
        }
        if (shouldRetry) {
            std::thread([this, commandId, backoffMs]() {
                QThread::msleep(backoffMs);
                moveToQueued(commandId, "retry_backoff_publish_failed");
            }).detach();
        }
    }

    // 设备状态回执到达后，尝试把对应命令标记为已确认。
    void tryAckCommands(const QString &roomId, const QString &deviceId, const QString &status)
    {
        QStringList toAck;
        {
            QMutexLocker locker(&commandMutex_);
            for (auto it = commandJobs_.cbegin(); it != commandJobs_.cend(); ++it) {
                const auto &job = it.value();
                if (job.state == "sent" &&
                    job.roomId == roomId &&
                    job.deviceId == deviceId &&
                    job.targetStatus == status) {
                    toAck.append(it.key());
                }
            }
        }

        for (const auto &id : toAck) {
            QMutexLocker locker(&commandMutex_);
            if (!commandJobs_.contains(id))
                continue;
            auto &job = commandJobs_[id];
            if (job.state != "sent")
                continue;
            const QString fromState = job.state;
            job.state = "acked";
            job.errorCode.clear();
            job.errorMessage.clear();
            job.updatedAt = QDateTime::currentDateTimeUtc();
            db_.updateCommandJob(job.id, "acked", job.retryCount);
            db_.appendCommandEvent(job.id, fromState, "acked", "state_matched", job.retryCount);
        }
    }

private slots:
    // 收到设备状态上报后，把状态写入数据库并尝试完成命令 ACK。
    void onState(const QString &roomId, const QString &deviceId, const QString &name, const QString &status)
    {
        if (!db_.isClassroomRegistered(roomId))
            return;
        const QString normalizedStatus = normalizeDeviceStatus(status);
        DeviceState s;
        s.name = name;
        s.status = normalizedStatus;
        s.updated = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        {
            QMutexLocker locker(&dataMutex_);
            devices_[roomId + "::" + deviceId] = s;
        }
        db_.upsertDevice(roomId, deviceId, name, normalizedStatus);
        tryAckCommands(roomId, deviceId, normalizedStatus);
    }

    // 收到告警或报修信息后，直接写入告警表。
    void onAlert(const QString &roomId, const QString &deviceId, const QString &message, const QString &level)
    {
        if (!db_.isClassroomRegistered(roomId))
            return;
        db_.insertAlert(roomId, deviceId, message, level);
    }
};

// 后端进程入口：启动 Qt 事件循环，并在后台线程中运行服务。
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    BackendServer server;
    const auto future = QtConcurrent::run([&]() { server.start(); });
    Q_UNUSED(future);
    return app.exec();
}

#include "backend_server.moc"
