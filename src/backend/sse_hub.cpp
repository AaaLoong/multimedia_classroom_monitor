#include "sse_hub.h"

#include <QJsonDocument>
#include <QUuid>
#include <QVector>

#include <chrono>

QByteArray SseHub::encodeSseEvent(const QString &eventName, const QJsonObject &payload) const
{
    QByteArray bytes;
    bytes.append("event: ");
    bytes.append(eventName.toUtf8());
    bytes.append("\n");
    bytes.append("data: ");
    bytes.append(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    bytes.append("\n\n");
    return bytes;
}

void SseHub::removeEventClient(const QString &clientId, bool adminClient)
{
    std::shared_ptr<SseEventClient> client;
    {
        QMutexLocker locker(&eventMutex_);
        if (adminClient)
            client = adminEventClients_.take(clientId);
        else
            client = classroomEventClients_.take(clientId);
    }

    if (!client)
        return;

    {
        std::lock_guard<std::mutex> lock(client->mutex);
        client->closed = true;
    }
    client->cv.notify_all();
}

void SseHub::enqueueEventForClient(const std::shared_ptr<SseEventClient> &client, const QByteArray &payload)
{
    if (!client)
        return;

    {
        std::lock_guard<std::mutex> lock(client->mutex);
        if (client->closed)
            return;
        if (client->queue.size() >= 64)
            client->queue.pop_front();
        client->queue.push_back(payload);
    }
    client->cv.notify_all();
}

bool SseHub::streamEventClient(const std::shared_ptr<SseEventClient> &client, httplib::DataSink &sink)
{
    QByteArray payload;
    {
        std::unique_lock<std::mutex> lock(client->mutex);
        if (client->queue.empty() && !client->closed) {
            client->cv.wait_for(lock, std::chrono::seconds(15), [&]() {
                return client->closed || !client->queue.empty();
            });
        }

        if (client->closed) {
            sink.done();
            return true;
        }

        if (!client->queue.empty()) {
            payload = client->queue.front();
            client->queue.pop_front();
        }
    }

    if (payload.isEmpty())
        payload = QByteArrayLiteral(": keepalive\n\n");

    if (sink.is_writable && !sink.is_writable())
        return false;
    return sink.write(payload.constData(), size_t(payload.size()));
}

void SseHub::attachAdminStream(httplib::Response &res)
{
    auto client = std::make_shared<SseEventClient>();
    client->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    client->admin = true;
    enqueueEventForClient(client, QByteArrayLiteral(": connected\n\n"));
    {
        QMutexLocker locker(&eventMutex_);
        adminEventClients_.insert(client->id, client);
    }

    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, client](size_t, httplib::DataSink &sink) {
            return streamEventClient(client, sink);
        },
        [this, client](bool) {
            removeEventClient(client->id, true);
        });
}

void SseHub::attachClassroomStream(const QString &roomId, httplib::Response &res)
{
    auto client = std::make_shared<SseEventClient>();
    client->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    client->roomId = roomId;
    enqueueEventForClient(client, QByteArrayLiteral(": connected\n\n"));
    {
        QMutexLocker locker(&eventMutex_);
        classroomEventClients_.insert(client->id, client);
    }

    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, client](size_t, httplib::DataSink &sink) {
            return streamEventClient(client, sink);
        },
        [this, client](bool) {
            removeEventClient(client->id, false);
        });
}

void SseHub::broadcastAdminEvent(const QString &eventName, const QJsonObject &payload)
{
    QVector<std::shared_ptr<SseEventClient>> clients;
    {
        QMutexLocker locker(&eventMutex_);
        clients.reserve(adminEventClients_.size());
        for (auto it = adminEventClients_.cbegin(); it != adminEventClients_.cend(); ++it)
            clients.push_back(it.value());
    }

    const QByteArray data = encodeSseEvent(eventName, payload);
    for (const auto &client : clients)
        enqueueEventForClient(client, data);
}

void SseHub::broadcastClassroomEvent(const QString &roomId, const QString &eventName, const QJsonObject &payload)
{
    QVector<std::shared_ptr<SseEventClient>> clients;
    {
        QMutexLocker locker(&eventMutex_);
        clients.reserve(classroomEventClients_.size());
        for (auto it = classroomEventClients_.cbegin(); it != classroomEventClients_.cend(); ++it) {
            if (it.value() && it.value()->roomId == roomId)
                clients.push_back(it.value());
        }
    }

    const QByteArray data = encodeSseEvent(eventName, payload);
    for (const auto &client : clients)
        enqueueEventForClient(client, data);
}

void SseHub::broadcastDevicesChanged(const QString &roomId)
{
    const QJsonObject payload{{"room_id", roomId}};
    broadcastAdminEvent("devices_changed", payload);
    broadcastClassroomEvent(roomId, "devices_changed", payload);
}

void SseHub::broadcastAlertsChanged(const QString &roomId, const QString &deviceId)
{
    const QJsonObject payload{
        {"room_id", roomId},
        {"device_id", deviceId}
    };
    broadcastAdminEvent("alerts_changed", payload);
}

void SseHub::broadcastLockChanged(bool locked)
{
    const QJsonObject payload{{"locked", locked}};
    broadcastAdminEvent("lock_changed", payload);
    QVector<std::shared_ptr<SseEventClient>> clients;
    {
        QMutexLocker locker(&eventMutex_);
        clients.reserve(classroomEventClients_.size());
        for (auto it = classroomEventClients_.cbegin(); it != classroomEventClients_.cend(); ++it)
            clients.push_back(it.value());
    }
    const QByteArray data = encodeSseEvent("lock_changed", payload);
    for (const auto &client : clients)
        enqueueEventForClient(client, data);
}

void SseHub::broadcastRegistrationChanged(const QString &roomId, bool registered)
{
    const QJsonObject payload{
        {"room_id", roomId},
        {"registered", registered}
    };
    broadcastAdminEvent("registration_changed", payload);
    broadcastClassroomEvent(roomId, "registration_changed", payload);
}
