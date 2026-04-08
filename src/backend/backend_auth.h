#pragma once

#include <httplib.h>

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>

class DbStore;

struct AdminSession {
    QString token;
    QString username;
    bool isSuper = false;
    bool canChangePassword = true;
    QDateTime issuedAt;
    QDateTime expiresAt;
};

struct AuthOperationResult {
    int statusCode = 200;
    QString error;
    QJsonDocument body;
};

bool isValidAdminUsername(const QString &username);
bool isValidAdminPassword(const QString &password);

class BackendAuth
{
public:
    explicit BackendAuth(DbStore &db);

    bool bootstrapAdminUser();
    QJsonObject authenticateAdmin(const QString &username, const QString &password);
    QString issueSession(const QJsonObject &user);
    bool revokeSessionFromRequest(const httplib::Request &req);
    bool currentSession(const httplib::Request &req, AdminSession *out);
    bool isAuthorized(const httplib::Request &req);

    AuthOperationResult login(const QString &username, const QString &password);
    AuthOperationResult listAdminUsers(const AdminSession &session);
    AuthOperationResult createAdminUser(const AdminSession &session,
                                        const QString &username);
    AuthOperationResult deleteAdminUser(const AdminSession &session, const QString &username);
    AuthOperationResult resetAdminUserPassword(const AdminSession &session, const QString &username);
    AuthOperationResult updateOwnPassword(const AdminSession &session,
                                          const QString &oldPassword,
                                          const QString &newPassword);

private:
    QString bearerTokenFromRequest(const httplib::Request &req) const;
    void pruneExpiredSessions();
    bool revokeSession(const QString &token);

    DbStore &db_;
    mutable QMutex sessionMutex_;
    QHash<QString, AdminSession> sessions_;
};
