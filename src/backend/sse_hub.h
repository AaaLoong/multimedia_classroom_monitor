#pragma once

#include <httplib.h>

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

class SseHub
{
public:
    SseHub() = default;

    void attachAdminStream(httplib::Response &res);
    void attachClassroomStream(const QString &roomId, httplib::Response &res);

    void broadcastDevicesChanged(const QString &roomId);
    void broadcastAlertsChanged(const QString &roomId, const QString &deviceId);
    void broadcastLockChanged(bool locked);
    void broadcastRegistrationChanged(const QString &roomId, bool registered);

private:
    struct SseEventClient {
        QString id;
        QString roomId;
        bool admin = false;
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<QByteArray> queue;
        bool closed = false;
    };

    QByteArray encodeSseEvent(const QString &eventName, const QJsonObject &payload) const;
    void removeEventClient(const QString &clientId, bool adminClient);
    void enqueueEventForClient(const std::shared_ptr<SseEventClient> &client, const QByteArray &payload);
    bool streamEventClient(const std::shared_ptr<SseEventClient> &client, httplib::DataSink &sink);
    void broadcastAdminEvent(const QString &eventName, const QJsonObject &payload);
    void broadcastClassroomEvent(const QString &roomId, const QString &eventName, const QJsonObject &payload);

    mutable QMutex eventMutex_;
    QHash<QString, std::shared_ptr<SseEventClient>> adminEventClients_;
    QHash<QString, std::shared_ptr<SseEventClient>> classroomEventClients_;
};
