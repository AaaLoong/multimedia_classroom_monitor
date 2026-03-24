#include "rest_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QUrl>
#include <QtGlobal>

/*
 * 这个文件专门负责 Qt/QML 和 HTTP 后端之间的通信。
 * 可以把它理解成“请求翻译层”：QML 只管发出业务动作，RestClient 负责拼请求、解析响应，
 * 再把结果变成属性和信号返回给界面。
 */

RestClient::RestClient(QObject *parent)
    : QObject(parent)
{
}

// 把用户传入的基础地址规范化，保证后面的请求路径拼接稳定。
static QString normalizeBase(const QString &raw)
{
    QString u = raw.trimmed();
    if (u.isEmpty())
        return QStringLiteral("http://127.0.0.1:8080");
    if (u.endsWith('/'))
        u.chop(1);
    if (u.startsWith("http:/") && !u.startsWith("http://"))
        u.replace(0, 6, "http://");
    if (u.startsWith("https:/") && !u.startsWith("https://"))
        u.replace(0, 7, "https://");
    if (u.startsWith("http://") || u.startsWith("https://"))
        return u;

    QString host = u;
    const int slashPos = host.indexOf('/');
    if (slashPos >= 0)
        host = host.left(slashPos);
    const int colonPos = host.indexOf(':');
    if (colonPos >= 0)
        host = host.left(colonPos);

    QHostAddress address;
    const bool isLocal = host == "localhost" || (address.setAddress(host) && address.isLoopback());
    if (isLocal)
        return "http://" + u;

    return {};
}

static QString errorMessageFromDocument(const QJsonDocument &doc, const QString &fallback)
{
    if (doc.isObject() && doc.object().contains("error")) {
        const QString error = doc.object().value("error").toString().trimmed();
        if (error == "invalid_username")
            return QStringLiteral("管理员用户名需以字母开头，长度 3 到 32 位");
        if (error == "invalid_password")
            return QStringLiteral("密码长度需为 6 到 64 位");
        if (error == "admin_exists")
            return QStringLiteral("管理员账号已存在");
        if (error == "admin_not_found")
            return QStringLiteral("未找到该管理员");
        if (error == "cannot_delete_super_admin")
            return QStringLiteral("超级管理员不可删除");
        if (error == "password_change_forbidden")
            return QStringLiteral("当前账号不支持修改密码");
        if (error == "invalid_current_password")
            return QStringLiteral("当前密码错误");
        if (error == "forbidden")
            return QStringLiteral("当前账号没有权限执行此操作");
        if (error == "create_admin_failed")
            return QStringLiteral("新增管理员失败");
        if (error == "delete_admin_failed")
            return QStringLiteral("删除管理员失败");
        if (error == "update_password_failed")
            return QStringLiteral("修改密码失败");
        if (error == "unauthorized")
            return QStringLiteral("登录状态已失效，请重新登录");
        if (!error.isEmpty())
            return error;
    }
    return fallback;
}

void RestClient::updateAdminSessionFromJson(const QJsonDocument &doc)
{
    const QJsonObject obj = doc.object();
    const QString newUsername = obj.value("username").toString().trimmed();
    const bool newSuperAdmin = obj.value("is_super").toBool(false);
    const bool newCanManageAdmins = obj.value("can_manage_admins").toBool(false);
    const bool newCanChangePassword = obj.value("can_change_password").toBool(false);

    if (adminUsername_ != newUsername) {
        adminUsername_ = newUsername;
        emit adminUsernameChanged();
    }
    if (superAdmin_ != newSuperAdmin) {
        superAdmin_ = newSuperAdmin;
        emit superAdminChanged();
    }
    if (canManageAdmins_ != newCanManageAdmins) {
        canManageAdmins_ = newCanManageAdmins;
        emit canManageAdminsChanged();
    }
    if (canChangePassword_ != newCanChangePassword) {
        canChangePassword_ = newCanChangePassword;
        emit canChangePasswordChanged();
    }
}

void RestClient::clearAdminSession()
{
    if (!adminUsername_.isEmpty()) {
        adminUsername_.clear();
        emit adminUsernameChanged();
    }
    if (superAdmin_) {
        superAdmin_ = false;
        emit superAdminChanged();
    }
    if (canManageAdmins_) {
        canManageAdmins_ = false;
        emit canManageAdminsChanged();
    }
    if (canChangePassword_) {
        canChangePassword_ = false;
        emit canChangePasswordChanged();
    }
}

// 更新后端地址时，只在真正变化时才通知 QML。
void RestClient::setBaseUrl(const QString &url)
{
    const QString normalized = normalizeBase(url);
    if (normalized.isEmpty()) {
        qWarning("忽略无效后端地址: %s", qPrintable(url));
        return;
    }
    if (baseUrl_ == normalized)
        return;
    baseUrl_ = normalized;
    emit baseUrlChanged();
}

// 所有请求都从这里拼出来，统一处理 Content-Type 和 Authorization 头。
QNetworkRequest RestClient::makeRequest(const QString &path)
{
    QNetworkRequest req(QUrl(baseUrl_ + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!token_.isEmpty())
        req.setRawHeader("Authorization", QString("Bearer %1").arg(token_).toUtf8());
    return req;
}

// 忙碌状态用来让界面知道当前是否正在请求网络。
void RestClient::setBusy(bool busy)
{
    if (busy_ == busy)
        return;
    busy_ = busy;
    emit busyChanged();
}

// 响应统一在这里收尾：解析 JSON、判断 HTTP 状态、触发成功或失败回调。
void RestClient::handleReply(QNetworkReply *reply, const std::function<void(const QJsonDocument &)> &onOk)
{
    QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
        const QByteArray data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300) {
            onOk(doc);
        } else {
            QString msg = errorMessageFromDocument(doc, reply->errorString());
            if (httpStatus >= 400 && httpStatus < 600)
                msg = QStringLiteral("服务错误(%1): %2").arg(httpStatus).arg(msg);
            if (httpStatus == 401 && !token_.isEmpty()) {
                token_.clear();
                clearAdminSession();
                emit unauthorized(QStringLiteral("登录状态已失效，请重新登录"));
            } else if (httpStatus == 401 && reply->url().path() == "/login") {
                emit loginFailed(QStringLiteral("用户名或密码错误"));
            } else {
                emit error(msg);
            }
        }
        reply->deleteLater();
        setBusy(false);
    });
}

// 登录接口：成功后保存 token，并拉取锁状态、设备列表和告警列表。
void RestClient::login(const QString &username, const QString &password)
{
    setBusy(true);
    QJsonObject obj;
    obj["username"] = username;
    obj["password"] = password;
    auto reply = nam_.post(makeRequest("/login"), QJsonDocument(obj).toJson(QJsonDocument::Compact));
    handleReply(reply, [=](const QJsonDocument &doc) {
        const QString t = doc.object().value("token").toString();
        if (t.isEmpty()) {
            emit loginFailed(QStringLiteral("登录失败"));
            return;
        }
        token_ = t;
        updateAdminSessionFromJson(doc);
        emit loginSucceeded();
        fetchLock();
        fetchDevices();
        fetchAlerts();
    });
}

// 主动注销当前管理员会话。
void RestClient::logout()
{
    if (token_.isEmpty()) {
        emit loginFailed(QStringLiteral("当前未登录"));
        return;
    }
    setBusy(true);
    auto reply = nam_.post(makeRequest("/logout"), QByteArray());
    handleReply(reply, [=](const QJsonDocument &) {
        token_.clear();
        clearAdminSession();
        emit loginFailed(QStringLiteral("已退出登录"));
    });
}

// 拉取全量设备，用于管理端总览和设备控制页。
void RestClient::fetchDevices()
{
    auto reply = nam_.get(makeRequest("/devices"));
    handleReply(reply, [=](const QJsonDocument &doc) {
        QVariantList list;
        if (doc.isObject()) {
            const auto obj = doc.object();
            updateLockFromJson(doc);
            if (obj.contains("devices") && obj.value("devices").isArray()) {
                for (const auto &item : obj.value("devices").toArray())
                    list.push_back(item.toObject().toVariantMap());
            }
        }
        emit devicesReceived(list);
    });
}

// 按教室拉取设备，用于教室端只看当前教室自己的设备。
void RestClient::fetchDevicesByRoom(const QString &roomId)
{
    if (roomId.isEmpty()) {
        emit devicesReceived(QVariantList());
        return;
    }
    const QString encodedRoom = QString::fromUtf8(QUrl::toPercentEncoding(roomId));
    auto reply = nam_.get(makeRequest(QString("/devices?room_id=%1").arg(encodedRoom)));
    handleReply(reply, [=](const QJsonDocument &doc) {
        QVariantList list;
        if (doc.isObject()) {
            const auto obj = doc.object();
            updateLockFromJson(doc);
            if (obj.contains("devices") && obj.value("devices").isArray()) {
                for (const auto &item : obj.value("devices").toArray())
                    list.push_back(item.toObject().toVariantMap());
            }
        }
        emit devicesReceived(list);
    });
}

// 拉取告警中心数据。
void RestClient::fetchAlerts()
{
    auto reply = nam_.get(makeRequest("/alerts"));
    handleReply(reply, [=](const QJsonDocument &doc) {
        QVariantList list;
        if (doc.isArray()) {
            for (const auto &item : doc.array())
                list.push_back(item.toObject().toVariantMap());
        }
        emit alertsReceived(list);
    });
}

// 拉取当前锁定状态。
void RestClient::fetchLock()
{
    auto reply = nam_.get(makeRequest("/lock"));
    handleReply(reply, [=](const QJsonDocument &doc) {
        updateLockFromJson(doc);
    });
}

// 向后端下发设备控制命令。
void RestClient::sendCommand(const QString &roomId, const QString &deviceId, const QString &action)
{
    if (roomId.isEmpty() || deviceId.isEmpty()) {
        emit error(QStringLiteral("未选中设备，无法下发指令"));
        return;
    }
    setBusy(true);
    QJsonObject obj;
    obj["action"] = action;
    const QString path = QString("/devices/%1/%2/command").arg(roomId, deviceId);
    auto reply = nam_.post(makeRequest(path), QJsonDocument(obj).toJson(QJsonDocument::Compact));
    handleReply(reply, [=](const QJsonDocument &doc) {
        const auto o = doc.object();
        const QString status = o.value("status").toString("accepted");
        const QString friendly = (action == "power_on")
            ? QStringLiteral("开机指令已发送")
            : ((action == "power_off")
                ? QStringLiteral("关机指令已发送")
                : QStringLiteral("控制指令已发送"));
        Q_UNUSED(status);
        emit operationFinished("command", friendly);
    });
}

// 提交报修内容到后端告警接口。
void RestClient::submitReport(const QString &roomId, const QString &deviceId, const QString &message)
{
    if (roomId.isEmpty() || deviceId.isEmpty()) {
        emit error(QStringLiteral("请选择一个报修的设备"));
        return;
    }
    setBusy(true);
    QJsonObject obj;
    obj["message"] = message;
    const QString path = QString("/devices/%1/%2/report").arg(roomId, deviceId);
    auto reply = nam_.post(makeRequest(path), QJsonDocument(obj).toJson(QJsonDocument::Compact));
    handleReply(reply, [=](const QJsonDocument &doc) {
        Q_UNUSED(doc);
        emit operationFinished("report", QStringLiteral("报修已提交"));
        fetchAlerts();
    });
}

// 锁定或解锁管理端控制权。
void RestClient::setLock(bool locked)
{
    setBusy(true);
    const QString path = locked ? "/lock" : "/unlock";
    auto reply = nam_.post(makeRequest(path), QByteArray());
    handleReply(reply, [=](const QJsonDocument &doc) {
        updateLockFromJson(doc);
    });
}

// 教室心跳只负责上报在线状态，不需要额外响应内容。
void RestClient::sendHeartbeat(const QString &roomId)
{
    if (roomId.isEmpty())
        return;
    auto reply = nam_.post(makeRequest(QString("/heartbeat/%1").arg(roomId)), QByteArray());
    handleReply(reply, [=](const QJsonDocument &) {});
}

// 查询当前教室是否已经注册。
void RestClient::fetchRegistration(const QString &roomId)
{
    if (roomId.isEmpty())
        return;
    auto reply = nam_.get(makeRequest(QString("/classrooms/%1/registration").arg(roomId)));
    handleReply(reply, [=](const QJsonDocument &doc) {
        updateRegistrationFromJson(doc);
    });
}

// 注册一个教室。
void RestClient::registerClassroom(const QString &roomId)
{
    if (roomId.isEmpty()) {
        emit error(QStringLiteral("教室ID为空，无法注册"));
        return;
    }
    setBusy(true);
    auto reply = nam_.post(makeRequest(QString("/classrooms/%1/register").arg(roomId)), QByteArray());
    handleReply(reply, [=](const QJsonDocument &doc) {
        updateRegistrationFromJson(doc);
        emit operationFinished("register", QStringLiteral("教室已接入，注册完成"));
    });
}

// 取消教室注册。
void RestClient::unregisterClassroom(const QString &roomId)
{
    if (roomId.isEmpty()) {
        emit error(QStringLiteral("教室ID为空，无法取消注册"));
        return;
    }
    setBusy(true);
    auto reply = nam_.post(makeRequest(QString("/classrooms/%1/unregister").arg(roomId)), QByteArray());
    handleReply(reply, [=](const QJsonDocument &doc) {
        updateRegistrationFromJson(doc);
        emit operationFinished("unregister", QStringLiteral("教室已取消接入"));
    });
}

void RestClient::fetchAdminUsers()
{
    auto reply = nam_.get(makeRequest("/admin-users"));
    handleReply(reply, [=](const QJsonDocument &doc) {
        QVariantList list;
        if (doc.isObject()) {
            const QJsonArray admins = doc.object().value("admins").toArray();
            for (const auto &item : admins)
                list.push_back(item.toObject().toVariantMap());
        }
        emit adminUsersReceived(list);
    });
}

void RestClient::createAdminUser(const QString &username, const QString &password)
{
    setBusy(true);
    QJsonObject obj;
    obj["username"] = username.trimmed();
    obj["password"] = password;
    auto reply = nam_.post(makeRequest("/admin-users"), QJsonDocument(obj).toJson(QJsonDocument::Compact));
    handleReply(reply, [=](const QJsonDocument &) {
        emit operationFinished("admin_create", QStringLiteral("管理员已新增"));
        fetchAdminUsers();
    });
}

void RestClient::deleteAdminUser(const QString &username)
{
    const QString target = username.trimmed();
    if (target.isEmpty()) {
        emit error(QStringLiteral("管理员账号为空，无法删除"));
        return;
    }

    setBusy(true);
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(target));
    auto reply = nam_.deleteResource(makeRequest(QString("/admin-users/%1").arg(encoded)));
    handleReply(reply, [=](const QJsonDocument &) {
        emit operationFinished("admin_delete", QStringLiteral("管理员已删除"));
        fetchAdminUsers();
    });
}

void RestClient::changeMyPassword(const QString &oldPassword, const QString &newPassword)
{
    setBusy(true);
    QJsonObject obj;
    obj["old_password"] = oldPassword;
    obj["new_password"] = newPassword;
    auto reply = nam_.post(makeRequest("/me/password"), QJsonDocument(obj).toJson(QJsonDocument::Compact));
    handleReply(reply, [=](const QJsonDocument &) {
        emit operationFinished("password_change", QStringLiteral("密码已修改"));
    });
}

// 从统一格式的 JSON 中同步锁状态到属性上。
void RestClient::updateLockFromJson(const QJsonDocument &doc)
{
    bool newLocked = locked_;
    if (doc.isObject()) {
        const auto obj = doc.object();
        if (obj.contains("locked"))
            newLocked = obj.value("locked").toBool();
        if (obj.contains("status"))
            newLocked = obj.value("status").toString() == "locked";
    }
    if (newLocked != locked_) {
        locked_ = newLocked;
        emit lockChanged(locked_);
    }
}

// 从统一格式的 JSON 中同步教室注册状态到属性上。
void RestClient::updateRegistrationFromJson(const QJsonDocument &doc)
{
    bool newRegistered = false;
    if (doc.isObject()) {
        const auto obj = doc.object();
        if (obj.contains("registered"))
            newRegistered = obj.value("registered").toBool(false);
    }

    if (newRegistered != registered_) {
        registered_ = newRegistered;
        emit registrationChanged(registered_);
    }

    if (!registrationChecked_) {
        registrationChecked_ = true;
        emit registrationCheckedChanged(true);
    }
}
