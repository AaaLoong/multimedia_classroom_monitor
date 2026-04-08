#include "http_routes.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "backend_auth.h"
#include "command_service.h"
#include "db_store.h"
#include "mqtt_adapter.h"
#include "sse_hub.h"

namespace {

void respondJson(httplib::Response &res, const QJsonDocument &doc, int status = 200)
{
    res.status = status;
    res.set_content(doc.toJson(QJsonDocument::Compact).toStdString(), "application/json");
}

void respondError(httplib::Response &res, const QString &error, int status)
{
    respondJson(res, QJsonDocument(QJsonObject{{"error", error}}), status);
}

void respondAuthResult(httplib::Response &res, const AuthOperationResult &result)
{
    if (!result.error.isEmpty()) {
        respondError(res, result.error, result.statusCode);
        return;
    }
    respondJson(res, result.body, result.statusCode);
}

QString decodedPathSegment(const httplib::Match &match, size_t index)
{
    return QUrl::fromPercentEncoding(QByteArray::fromStdString(match[index].str())).trimmed();
}

} // namespace

HttpRoutes::HttpRoutes(HttpRouteContext context)
    : ctx_(std::move(context))
{
}

quint64 HttpRoutes::recordFailure(const QString &endpoint, const QString &reason)
{
    const QString key = endpoint + "|" + reason;
    QMutexLocker locker(&failureMutex_);
    auto it = failureCounts_.find(key);
    if (it == failureCounts_.end())
        it = failureCounts_.insert(key, 0);
    *it += 1;
    return *it;
}

void HttpRoutes::logRouteFailure(const QString &endpoint,
                                 const QString &reason,
                                 int statusCode,
                                 const QString &roomId,
                                 const QString &deviceId,
                                 const QString &detail)
{
    const quint64 count = recordFailure(endpoint, reason);
    qWarning().noquote()
        << "route_failure"
        << "endpoint=" + endpoint
        << "reason=" + reason
        << "status=" + QString::number(statusCode)
        << "count=" + QString::number(count)
        << "room=" + roomId
        << "device=" + deviceId
        << "detail=" + detail;
}

void HttpRoutes::registerRoutes(httplib::Server &server)
{
    server.Get("/health", [&](const httplib::Request &, httplib::Response &res) {
        respondJson(res, QJsonDocument(QJsonObject{
                         {"status", "ok"},
                         {"transport", "http"}
                     }));
    });

    server.Get("/lock", [&](const httplib::Request &, httplib::Response &res) {
        respondJson(res, QJsonDocument(QJsonObject{{"locked", ctx_.lockedState()}}));
    });

    server.Post("/login", [&](const httplib::Request &req, httplib::Response &res) {
        const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        respondAuthResult(res,
                          ctx_.auth.login(body.value("username").toString().trimmed(),
                                          body.value("password").toString()));
    });

    server.Post("/logout", [&](const httplib::Request &req, httplib::Response &res) {
        if (!ctx_.auth.revokeSessionFromRequest(req)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        respondJson(res, QJsonDocument(QJsonObject{{"status", "logged_out"}}));
    });

    server.Get("/events/admin", [&](const httplib::Request &req, httplib::Response &res) {
        AdminSession session;
        if (!ctx_.auth.currentSession(req, &session)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        ctx_.events.attachAdminStream(res);
    });

    server.Get(R"(/events/classrooms/([^/]+))", [&](const httplib::Request &req, httplib::Response &res) {
        const QString roomId = decodedPathSegment(req.matches, 1);
        if (roomId.isEmpty() || !ctx_.isRoomRegistered(roomId)) {
            respondError(res, "forbidden", 403);
            return;
        }
        ctx_.events.attachClassroomStream(roomId, res);
    });

    server.Get("/admin-users", [&](const httplib::Request &req, httplib::Response &res) {
        AdminSession session;
        if (!ctx_.auth.currentSession(req, &session)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        respondAuthResult(res, ctx_.auth.listAdminUsers(session));
    });

    server.Post("/admin-users", [&](const httplib::Request &req, httplib::Response &res) {
        AdminSession session;
        if (!ctx_.auth.currentSession(req, &session)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        respondAuthResult(res,
                          ctx_.auth.createAdminUser(session,
                                                    body.value("username").toString().trimmed()));
    });

    server.Delete(R"(/admin-users/([\w\-]+))", [&](const httplib::Request &req, httplib::Response &res) {
        AdminSession session;
        if (!ctx_.auth.currentSession(req, &session)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        respondAuthResult(res,
                          ctx_.auth.deleteAdminUser(session,
                                                    QString::fromStdString(req.matches[1]).trimmed()));
    });

    server.Post(R"(/admin-users/([^/]+)/reset-password)", [&](const httplib::Request &req, httplib::Response &res) {
        AdminSession session;
        if (!ctx_.auth.currentSession(req, &session)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        respondAuthResult(res,
                          ctx_.auth.resetAdminUserPassword(session,
                                                           decodedPathSegment(req.matches, 1)));
    });

    server.Post("/me/password", [&](const httplib::Request &req, httplib::Response &res) {
        AdminSession session;
        if (!ctx_.auth.currentSession(req, &session)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        respondAuthResult(res,
                          ctx_.auth.updateOwnPassword(session,
                                                      body.value("old_password").toString(),
                                                      body.value("new_password").toString()));
    });

    server.Post("/lock", [&](const httplib::Request &req, httplib::Response &res) {
        if (!ctx_.auth.isAuthorized(req)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        ctx_.setLockedState(true);
        ctx_.db.setLock(true);
        ctx_.broadcastLockChanged();
        respondJson(res, QJsonDocument(QJsonObject{{"status", "locked"}, {"locked", true}}));
    });

    server.Post("/unlock", [&](const httplib::Request &req, httplib::Response &res) {
        if (!ctx_.auth.isAuthorized(req)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        ctx_.setLockedState(false);
        ctx_.db.setLock(false);
        ctx_.broadcastLockChanged();
        respondJson(res, QJsonDocument(QJsonObject{{"status", "unlocked"}, {"locked", false}}));
    });

    server.Get("/devices", [&](const httplib::Request &req, httplib::Response &res) {
        ctx_.pushDevicesResponse(req, res);
    });

    server.Get("/alerts", [&](const httplib::Request &req, httplib::Response &res) {
        if (!ctx_.auth.isAuthorized(req)) {
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
        respondJson(res, QJsonDocument(ctx_.db.loadAlerts(limit)));
    });

    server.Post("/alerts/resolve-all", [&](const httplib::Request &req, httplib::Response &res) {
        if (!ctx_.auth.isAuthorized(req)) {
            respondError(res, "unauthorized", 401);
            return;
        }
        const int count = ctx_.db.resolveAllAlerts();
        ctx_.broadcastAlertsChanged(QString(), QString());
        respondJson(res, QJsonDocument(QJsonObject{
                         {"status", "resolved"},
                         {"resolved_count", count}
                     }));
    });

    server.Post(R"(/alerts/(\d+)/resolve)", [&](const httplib::Request &req, httplib::Response &res) {
        if (!ctx_.auth.isAuthorized(req)) {
            respondError(res, "unauthorized", 401);
            return;
        }

        bool ok = false;
        const int alertId = QString::fromStdString(req.matches[1]).toInt(&ok);
        if (!ok || alertId <= 0) {
            respondError(res, "invalid_alert", 400);
            return;
        }

        if (!ctx_.db.resolveAlert(alertId)) {
            respondError(res, "alert_not_found", 404);
            return;
        }
        ctx_.broadcastAlertsChanged(QString(), QString());
        respondJson(res, QJsonDocument(QJsonObject{
                         {"status", "resolved"},
                         {"alert_id", alertId}
                     }));
    });

    server.Get("/classrooms", [&](const httplib::Request &, httplib::Response &res) {
        respondJson(res, QJsonDocument(QJsonObject{{"classrooms", ctx_.db.loadRegisteredClassrooms()}}));
    });

    server.Get("/classroom-binding", [&](const httplib::Request &req, httplib::Response &res) {
        if (!req.has_param("fingerprint")) {
            respondJson(res, QJsonDocument(QJsonObject{{"error", "invalid_fingerprint"}}), 400);
            return;
        }

        const QString fingerprint = QString::fromStdString(req.get_param_value("fingerprint")).trimmed();
        if (fingerprint.isEmpty()) {
            respondJson(res, QJsonDocument(QJsonObject{{"error", "invalid_fingerprint"}}), 400);
            return;
        }

        const ClassroomBindingInfo info = ctx_.db.loadClassroomBindingByFingerprint(fingerprint);
        QJsonObject body{
            {"registered", info.registered}
        };
        if (info.registered)
            body.insert("room_id", info.roomId);
        respondJson(res, QJsonDocument(body));
    });

    server.Get(R"(/classrooms/([^/]+)/registration)", [&](const httplib::Request &req, httplib::Response &res) {
        const QString roomId = decodedPathSegment(req.matches, 1);
        respondJson(res, QJsonDocument(QJsonObject{
                         {"room_id", roomId},
                         {"registered", ctx_.isRoomRegistered(roomId)}
                     }));
    });

    server.Post(R"(/classrooms/([^/]+)/register)", [&](const httplib::Request &req, httplib::Response &res) {
        const QString roomId = decodedPathSegment(req.matches, 1);
        if (roomId.isEmpty()) {
            respondJson(res, QJsonDocument(QJsonObject{{"error", "invalid_room"}}), 400);
            return;
        }

        const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        const QString fingerprint = body.value("fingerprint").toString().trimmed();
        const ClassroomRegisterResult result = ctx_.db.registerClassroom(roomId, fingerprint);
        if (!result.success) {
            const QString error = result.error.isEmpty() ? QStringLiteral("register_failed") : result.error;
            const int status = (error == "room_already_bound" || error == "fingerprint_already_bound")
                ? 409
                : ((error == "invalid_room" || error == "invalid_fingerprint") ? 400 : 500);
            respondJson(res, QJsonDocument(QJsonObject{{"error", error}}), status);
            return;
        }
        ctx_.markRoomRegistered(result.roomId, true);
        ctx_.seedRuntimeDevicesForRoom(result.roomId);
        ctx_.broadcastRegistrationChanged(result.roomId, true);
        ctx_.broadcastDevicesChanged(result.roomId);
        respondJson(res, QJsonDocument(QJsonObject{
                         {"room_id", result.roomId},
                         {"registered", true},
                         {"status", "registered"}
                     }));
    });

    server.Post(R"(/classrooms/([^/]+)/unregister)", [&](const httplib::Request &req, httplib::Response &res) {
        const QString roomId = decodedPathSegment(req.matches, 1);
        if (roomId.isEmpty()) {
            respondJson(res, QJsonDocument(QJsonObject{{"error", "invalid_room"}}), 400);
            return;
        }
        if (!ctx_.db.unregisterClassroom(roomId)) {
            respondJson(res, QJsonDocument(QJsonObject{{"error", "unregister_failed"}}), 500);
            return;
        }
        ctx_.markRoomRegistered(roomId, false);
        ctx_.purgeRuntimeForRoom(roomId);
        ctx_.broadcastRegistrationChanged(roomId, false);
        ctx_.broadcastDevicesChanged(roomId);
        respondJson(res, QJsonDocument(QJsonObject{
                         {"room_id", roomId},
                         {"registered", false},
                         {"status", "unregistered"}
                     }));
    });

    server.Post(R"(/devices/([^/]+)/([\w\-]+)/command)", [&](const httplib::Request &req, httplib::Response &res) {
        const bool adminRequest = ctx_.auth.isAuthorized(req);
        const QString roomId = decodedPathSegment(req.matches, 1);
        const QString deviceId = QString::fromStdString(req.matches[2]);
        if (ctx_.lockedState() && !adminRequest) {
            logRouteFailure("command", "locked", 423, roomId, deviceId);
            respondJson(res, QJsonDocument(QJsonObject{{"error", "locked"}}), 423);
            return;
        }

        const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        if (!ctx_.isRoomRegistered(roomId)) {
            logRouteFailure("command", "room_not_registered", 403, roomId, deviceId);
            respondJson(res, QJsonDocument(QJsonObject{{"error", "room_not_registered"}}), 403);
            return;
        }

        const auto result = ctx_.commands.acceptCommand(roomId,
                                                        deviceId,
                                                        body.value("action").toString());
        if (!result.error.isEmpty()) {
            logRouteFailure("command",
                            result.error,
                            result.statusCode,
                            roomId,
                            deviceId,
                            "action=" + body.value("action").toString());
            respondJson(res, QJsonDocument(QJsonObject{{"error", result.error}}), result.statusCode);
            return;
        }

        respondJson(res, QJsonDocument(QJsonObject{
                         {"status", "accepted"},
                         {"delay_ms", result.delayMs},
                         {"command_id", result.commandId}
                     }),
                    result.statusCode);
    });

    server.Post(R"(/devices/([^/]+)/([\w\-]+)/report)", [&](const httplib::Request &req, httplib::Response &res) {
        const bool adminRequest = ctx_.auth.isAuthorized(req);
        const QString roomId = decodedPathSegment(req.matches, 1);
        const QString deviceId = QString::fromStdString(req.matches[2]);
        if (ctx_.lockedState() && !adminRequest) {
            logRouteFailure("report", "locked", 423, roomId, deviceId);
            respondJson(res, QJsonDocument(QJsonObject{{"error", "locked"}}), 423);
            return;
        }
        if (!ctx_.isRoomRegistered(roomId)) {
            logRouteFailure("report", "room_not_registered", 403, roomId, deviceId);
            respondJson(res, QJsonDocument(QJsonObject{{"error", "room_not_registered"}}), 403);
            return;
        }
        const auto body = QJsonDocument::fromJson(QByteArray::fromStdString(req.body)).object();
        const QString msg = body.value("message").toString("报修");
        if (!ctx_.mqtt.publishReport(roomId, deviceId, msg)) {
            logRouteFailure("report", "report_publish_failed", 503, roomId, deviceId, msg);
            respondJson(res, QJsonDocument(QJsonObject{{"error", "report_publish_failed"}}), 503);
            return;
        }
        respondJson(res, QJsonDocument(QJsonObject{{"status", "submitted"}}));
    });
}
