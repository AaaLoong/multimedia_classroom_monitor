#include "class_init.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>
#include <QTimeZone>

#ifdef _WIN32
#include <windows.h>
#endif

/*
 * 这个文件只放“启动时公共辅助函数”。
 * 这些函数不会参与业务流程本身，但它们能让不同入口程序共享同一套参数读取和编码处理逻辑。
 */

namespace {

QString stripOptionalQuotes(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
            value = value.mid(1, value.size() - 2);
    }
    return value;
}

QString detectProjectRoot()
{
    auto projectRootFromCMakeCache = [](const QString &seedPath) -> QString {
        QDir dir(seedPath);
        for (int depth = 0; depth < 8; ++depth) {
            const QString cachePath = dir.filePath("CMakeCache.txt");
            QFile cacheFile(cachePath);
            if (cacheFile.exists() && cacheFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream cacheStream(&cacheFile);
                while (!cacheStream.atEnd()) {
                    const QString line = cacheStream.readLine().trimmed();
                    if (!line.startsWith("CMAKE_HOME_DIRECTORY:"))
                        continue;

                    const int eq = line.indexOf('=');
                    if (eq <= 0)
                        break;

                    const QString value = line.mid(eq + 1).trimmed();
                    if (!value.isEmpty() && QFileInfo::exists(QDir(value).filePath("CMakeLists.txt")))
                        return value;
                    break;
                }
            }

            if (!dir.cdUp())
                break;
        }
        return {};
    };

    QStringList seeds;
    const QString configuredRoot = qEnvironmentVariable("CMM_PROJECT_ROOT").trimmed();
    if (!configuredRoot.isEmpty())
        seeds.append(configuredRoot);

    if (QCoreApplication::instance())
        seeds.append(QCoreApplication::applicationDirPath());
    seeds.append(QDir::currentPath());

    QSet<QString> visited;
    for (const auto &seed : seeds) {
        if (seed.trimmed().isEmpty())
            continue;

        const QString cacheRoot = projectRootFromCMakeCache(seed);
        if (!cacheRoot.isEmpty())
            return cacheRoot;

        QDir dir(seed);
        for (int depth = 0; depth < 8; ++depth) {
            const QString candidate = dir.absolutePath();
            if (visited.contains(candidate))
                break;
            visited.insert(candidate);

            if (QFileInfo::exists(dir.filePath("CMakeLists.txt"))
                    && QFileInfo::exists(dir.filePath("config"))) {
                return candidate;
            }

            if (!dir.cdUp())
                break;
        }
    }

    return {};
}

void loadRuntimeEnvFile(const QString &envPath)
{
    QFile file(envPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine();
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;

        const QString key = line.left(eq).trimmed();
        if (key.isEmpty())
            continue;

        const QByteArray keyUtf8 = key.toUtf8();
        if (qEnvironmentVariableIsSet(keyUtf8.constData()))
            continue;

        const QString value = stripOptionalQuotes(line.mid(eq + 1));
        qputenv(keyUtf8.constData(), value.toUtf8());
    }
}

QTimeZone shanghaiTimeZone()
{
    static const QTimeZone tz("Asia/Shanghai");
    return tz.isValid() ? tz : QTimeZone::fromSecondsAheadOfUtc(8 * 60 * 60);
}

} // namespace

void ClassInit::setupConsoleUtf8()
{
#ifdef _WIN32
    // Windows 控制台默认不是 UTF-8，这里手动切换，避免中文输出乱码。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void ClassInit::loadRuntimeEnvDefaults()
{
    const QString projectRoot = detectProjectRoot();
    if (projectRoot.isEmpty())
        return;

    const QDir configDir(QDir(projectRoot).filePath("config"));
    loadRuntimeEnvFile(configDir.filePath("runtime.env"));
    loadRuntimeEnvFile(configDir.filePath("runtime.env.local"));
}

QString ClassInit::argOrDefault(const QStringList &args, int index, const QString &fallback)
{
    // 如果命令行里已经传了参数，就直接用；否则回退到默认值。
    if (index >= 0 && index < args.size())
        return args.at(index);
    return fallback;
}

QString ClassInit::argOrEnv(const QStringList &args,
                            int index,
                            const char *envName,
                            const QString &fallback)
{
    // 先看命令行，再看环境变量，最后才使用默认值。
    const QString fromArgs = argOrDefault(args, index, QString());
    if (!fromArgs.isEmpty())
        return fromArgs;

    const QString fromEnv = qEnvironmentVariable(envName, QByteArray());
    if (!fromEnv.isEmpty())
        return fromEnv;
    return fallback;
}

QDateTime ClassInit::currentShanghaiDateTime()
{
    return QDateTime::currentDateTimeUtc().toTimeZone(shanghaiTimeZone());
}

QDateTime ClassInit::toShanghaiDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid())
        return {};
    return dateTime.toTimeZone(shanghaiTimeZone());
}

QString ClassInit::currentShanghaiIsoString()
{
    return currentShanghaiDateTime().toString(Qt::ISODate);
}

QString ClassInit::toShanghaiIsoString(const QDateTime &dateTime)
{
    const QDateTime shanghai = toShanghaiDateTime(dateTime);
    return shanghai.isValid() ? shanghai.toString(Qt::ISODate) : QString();
}

QString ClassInit::toDatabaseDateTimeString(const QDateTime &dateTime)
{
    const QDateTime shanghai = toShanghaiDateTime(dateTime);
    return shanghai.isValid() ? shanghai.toString("yyyy-MM-dd HH:mm:ss") : QString();
}

QString ClassInit::databaseSessionTimeZone()
{
    return QStringLiteral("+08:00");
}
