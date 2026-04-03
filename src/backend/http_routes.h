#pragma once

#include <httplib.h>

#include <QHash>
#include <QMutex>
#include <QString>

#include <functional>

class DbStore;
class MqttAdapter;
class BackendAuth;
class SseHub;
class CommandService;

struct HttpRouteContext {
    DbStore &db;
    MqttAdapter &mqtt;
    BackendAuth &auth;
    SseHub &events;
    CommandService &commands;
    std::function<bool()> lockedState;
    std::function<void(bool)> setLockedState;
    std::function<bool(const QString &)> isRoomRegistered;
    std::function<void(const QString &, bool)> markRoomRegistered;
    std::function<void(const QString &)> seedRuntimeDevicesForRoom;
    std::function<void(const QString &)> purgeRuntimeForRoom;
    std::function<void(const QString &)> broadcastDevicesChanged;
    std::function<void(const QString &, const QString &)> broadcastAlertsChanged;
    std::function<void(const QString &, bool)> broadcastRegistrationChanged;
    std::function<void()> broadcastLockChanged;
    std::function<void(const httplib::Request &, httplib::Response &)> pushDevicesResponse;
};

class HttpRoutes
{
public:
    explicit HttpRoutes(HttpRouteContext context);

    void registerRoutes(httplib::Server &server);

private:
    quint64 recordFailure(const QString &endpoint, const QString &reason);
    void logRouteFailure(const QString &endpoint,
                         const QString &reason,
                         int statusCode,
                         const QString &roomId = QString(),
                         const QString &deviceId = QString(),
                         const QString &detail = QString());

    HttpRouteContext ctx_;
    mutable QMutex failureMutex_;
    QHash<QString, quint64> failureCounts_;
};
