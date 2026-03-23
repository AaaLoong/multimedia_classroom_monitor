#pragma once

#include <QObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariant>
#include <functional>

/*
 * RestClient 是 QML 侧访问后端 HTTP 接口的统一入口。
 * 它把登录、设备同步、告警同步、锁定控制、教室注册这些业务包装成 QObject 接口，
 * 让界面层只需要调用方法和监听信号，不需要自己拼接请求和解析响应。
 */
class RestClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY registrationChanged)
    Q_PROPERTY(bool registrationChecked READ registrationChecked NOTIFY registrationCheckedChanged)
public:
    explicit RestClient(QObject *parent = nullptr);

    // 这些属性直接暴露给 QML 使用。
    QString baseUrl() const { return baseUrl_; }
    bool locked() const { return locked_; }
    bool busy() const { return busy_; }
    bool registered() const { return registered_; }
    bool registrationChecked() const { return registrationChecked_; }

    // 设置后端基础地址。
    void setBaseUrl(const QString &url);

    // 下面这些接口会被 QML 直接调用，分别对应主要业务动作。
    Q_INVOKABLE void login(const QString &username, const QString &password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void fetchDevices();
    Q_INVOKABLE void fetchDevicesByRoom(const QString &roomId);
    Q_INVOKABLE void fetchAlerts();
    Q_INVOKABLE void fetchLock();
    Q_INVOKABLE void sendCommand(const QString &roomId, const QString &deviceId, const QString &action);
    Q_INVOKABLE void submitReport(const QString &roomId, const QString &deviceId, const QString &message);
    Q_INVOKABLE void setLock(bool locked);
    Q_INVOKABLE void sendHeartbeat(const QString &roomId);
    Q_INVOKABLE void fetchRegistration(const QString &roomId);
    Q_INVOKABLE void registerClassroom(const QString &roomId);
    Q_INVOKABLE void unregisterClassroom(const QString &roomId);

signals:
    // 这些信号用于把后端数据返回到 QML 页面。
    void devicesReceived(const QVariantList &devices);
    void alertsReceived(const QVariantList &alerts);
    void loginSucceeded();
    void loginFailed(const QString &message);
    void unauthorized(const QString &message);
    void lockChanged(bool locked);
    void busyChanged();
    void baseUrlChanged();
    void registrationChanged(bool registered);
    void registrationCheckedChanged(bool checked);
    void operationFinished(const QString &action, const QString &message);
    void error(const QString &message);

private:
    QString baseUrl_ = QStringLiteral("http://127.0.0.1:8080");
    QString token_;
    bool locked_ = false;
    bool busy_ = false;
    bool registered_ = false;
    bool registrationChecked_ = false;
    QNetworkAccessManager nam_;

    // 内部工具函数：拼请求、处理响应、同步状态。
    QNetworkRequest makeRequest(const QString &path);
    void setBusy(bool busy);
    void handleReply(QNetworkReply *reply, const std::function<void(const QJsonDocument &)> &onOk);
    void updateLockFromJson(const QJsonDocument &doc);
    void updateRegistrationFromJson(const QJsonDocument &doc);
};
