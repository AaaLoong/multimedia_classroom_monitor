#include "backend_auth.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>

#include "class_init.h"
#include "db_store.h"

namespace {

constexpr int kPasswordSaltLength = 16;
constexpr int kPasswordHashLength = 32;
constexpr int kPasswordIterations = 210000;
constexpr int kSessionTtlSeconds = 8 * 60 * 60;
const QString kManagedAdminInitialPassword = QStringLiteral("000000");

QByteArray randomBytes(int length)
{
    QByteArray bytes(length, Qt::Uninitialized);
    for (int i = 0; i < length; ++i)
        bytes[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return bytes;
}

QByteArray derivePasswordHash(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                              password.toUtf8(),
                                              salt,
                                              iterations,
                                              32);
}

bool constantTimeEquals(const QByteArray &lhs, const QByteArray &rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    unsigned char diff = 0;
    for (int i = 0; i < lhs.size(); ++i)
        diff |= static_cast<unsigned char>(lhs.at(i) ^ rhs.at(i));
    return diff == 0;
}

} // namespace

bool isValidAdminUsername(const QString &username)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{2,31}$"));
    return re.match(username.trimmed()).hasMatch();
}

bool isValidAdminPassword(const QString &password)
{
    return password.size() >= 6 && password.size() <= 64;
}

BackendAuth::BackendAuth(DbStore &db)
    : db_(db)
{
}

bool BackendAuth::bootstrapAdminUser()
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
                                        kPasswordIterations,
                                        true,
                                        false);
    if (!ok) {
        qCritical("初始化管理员账号失败。");
        return false;
    }

    qInfo("已初始化首个管理员账号: %s", qPrintable(username));
    return true;
}

QJsonObject BackendAuth::authenticateAdmin(const QString &username, const QString &password)
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
    return (userExists && match) ? user : QJsonObject();
}

QString BackendAuth::bearerTokenFromRequest(const httplib::Request &req) const
{
    if (!req.has_header("Authorization"))
        return {};

    const QString header = QString::fromStdString(req.get_header_value("Authorization")).trimmed();
    if (!header.startsWith("Bearer "))
        return {};
    return header.mid(7).trimmed();
}

void BackendAuth::pruneExpiredSessions()
{
    const QDateTime now = ClassInit::currentShanghaiDateTime();
    QMutexLocker locker(&sessionMutex_);
    QStringList expiredTokens;
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
        if (it.value().expiresAt <= now)
            expiredTokens.append(it.key());
    }
    for (const auto &token : expiredTokens)
        sessions_.remove(token);
}

QString BackendAuth::issueSession(const QJsonObject &user)
{
    pruneExpiredSessions();

    AdminSession session;
    session.token = QString::fromUtf8(randomBytes(32).toBase64(QByteArray::Base64UrlEncoding |
                                                               QByteArray::OmitTrailingEquals));
    session.username = user.value("username").toString();
    session.isSuper = user.value("is_super").toBool(false);
    session.canChangePassword = user.value("can_change_password").toBool(true);
    session.issuedAt = ClassInit::currentShanghaiDateTime();
    session.expiresAt = session.issuedAt.addSecs(kSessionTtlSeconds);

    QMutexLocker locker(&sessionMutex_);
    sessions_.insert(session.token, session);
    return session.token;
}

bool BackendAuth::revokeSession(const QString &token)
{
    pruneExpiredSessions();
    QMutexLocker locker(&sessionMutex_);
    return sessions_.remove(token) > 0;
}

bool BackendAuth::revokeSessionFromRequest(const httplib::Request &req)
{
    const QString token = bearerTokenFromRequest(req);
    if (token.isEmpty())
        return false;
    return revokeSession(token);
}

bool BackendAuth::currentSession(const httplib::Request &req, AdminSession *out)
{
    pruneExpiredSessions();
    const QString token = bearerTokenFromRequest(req);
    if (token.isEmpty())
        return false;

    QMutexLocker locker(&sessionMutex_);
    if (!sessions_.contains(token))
        return false;
    if (out)
        *out = sessions_.value(token);
    return true;
}

bool BackendAuth::isAuthorized(const httplib::Request &req)
{
    return currentSession(req, nullptr);
}

AuthOperationResult BackendAuth::login(const QString &username, const QString &password)
{
    const QJsonObject adminUser = authenticateAdmin(username.trimmed(), password);
    if (adminUser.isEmpty())
        return {401, "invalid_credentials", {}};

    const QString token = issueSession(adminUser);
    AdminSession session;
    {
        QMutexLocker locker(&sessionMutex_);
        session = sessions_.value(token);
    }

    return {
        200,
        {},
        QJsonDocument(QJsonObject{
            {"token", token},
            {"username", session.username},
            {"is_super", session.isSuper},
            {"can_manage_admins", session.isSuper},
            {"can_change_password", session.canChangePassword},
            {"expires_at", ClassInit::toShanghaiIsoString(session.expiresAt)}
        })
    };
}

AuthOperationResult BackendAuth::listAdminUsers(const AdminSession &session)
{
    if (!session.isSuper)
        return {403, "forbidden", {}};

    return {
        200,
        {},
        QJsonDocument(QJsonObject{{"admins", db_.loadAdminUsers()}})
    };
}

AuthOperationResult BackendAuth::createAdminUser(const AdminSession &session,
                                                 const QString &username)
{
    if (!session.isSuper)
        return {403, "forbidden", {}};
    if (!isValidAdminUsername(username))
        return {400, "invalid_username", {}};
    if (!db_.loadAdminUser(username).isEmpty())
        return {409, "admin_exists", {}};

    const QByteArray salt = randomBytes(kPasswordSaltLength);
    const QByteArray hash = derivePasswordHash(kManagedAdminInitialPassword, salt, kPasswordIterations);
    const bool ok = db_.createAdminUser(username,
                                        QString::fromUtf8(salt.toBase64()),
                                        QString::fromUtf8(hash.toBase64()),
                                        kPasswordIterations,
                                        false,
                                        true);
    if (!ok)
        return {500, "create_admin_failed", {}};

    return {
        201,
        {},
        QJsonDocument(QJsonObject{
            {"status", "created"},
            {"username", username},
            {"is_super", false},
            {"initial_password", kManagedAdminInitialPassword}
        })
    };
}

AuthOperationResult BackendAuth::deleteAdminUser(const AdminSession &session, const QString &username)
{
    if (!session.isSuper)
        return {403, "forbidden", {}};

    const QJsonObject target = db_.loadAdminUser(username);
    if (target.isEmpty())
        return {404, "admin_not_found", {}};
    if (target.value("is_super").toBool(false))
        return {403, "cannot_delete_super_admin", {}};
    if (!db_.deleteAdminUser(username))
        return {500, "delete_admin_failed", {}};

    return {
        200,
        {},
        QJsonDocument(QJsonObject{
            {"status", "deleted"},
            {"username", username}
        })
    };
}

AuthOperationResult BackendAuth::resetAdminUserPassword(const AdminSession &session, const QString &username)
{
    if (!session.isSuper)
        return {403, "forbidden", {}};

    const QJsonObject target = db_.loadAdminUser(username);
    if (target.isEmpty())
        return {404, "admin_not_found", {}};
    if (target.value("is_super").toBool(false))
        return {403, "cannot_reset_super_admin", {}};

    const QByteArray salt = randomBytes(kPasswordSaltLength);
    const QByteArray hash = derivePasswordHash(kManagedAdminInitialPassword, salt, kPasswordIterations);
    const bool ok = db_.updateAdminPassword(username,
                                            QString::fromUtf8(salt.toBase64()),
                                            QString::fromUtf8(hash.toBase64()),
                                            kPasswordIterations);
    if (!ok)
        return {500, "reset_admin_password_failed", {}};

    return {
        200,
        {},
        QJsonDocument(QJsonObject{
            {"status", "password_reset"},
            {"username", username},
            {"initial_password", kManagedAdminInitialPassword}
        })
    };
}

AuthOperationResult BackendAuth::updateOwnPassword(const AdminSession &session,
                                                   const QString &oldPassword,
                                                   const QString &newPassword)
{
    if (session.isSuper || !session.canChangePassword)
        return {403, "password_change_forbidden", {}};
    if (!isValidAdminPassword(newPassword))
        return {400, "invalid_password", {}};

    const QJsonObject currentUser = authenticateAdmin(session.username, oldPassword);
    if (currentUser.isEmpty())
        return {401, "invalid_current_password", {}};

    const QByteArray salt = randomBytes(kPasswordSaltLength);
    const QByteArray hash = derivePasswordHash(newPassword, salt, kPasswordIterations);
    const bool ok = db_.updateAdminPassword(session.username,
                                            QString::fromUtf8(salt.toBase64()),
                                            QString::fromUtf8(hash.toBase64()),
                                            kPasswordIterations);
    if (!ok)
        return {500, "update_password_failed", {}};

    return {200, {}, QJsonDocument(QJsonObject{{"status", "updated"}})};
}
