#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

/*
 * ClassInit 是整个项目的启动辅助工具类。
 * 它不承载业务逻辑，只负责处理和“程序启动”相关的公共工作：
 * 1. 统一控制台编码；
 * 2. 统一读取命令行参数；
 * 3. 统一读取环境变量兜底值。
 */
class ClassInit
{
public:
    // 在需要时把控制台输出切换成 UTF-8，避免中文日志乱码。
    static void setupConsoleUtf8();
    // 从项目下的 config/runtime.env 读取未显式设置的环境变量，方便 IDE 直接运行。
    static void loadRuntimeEnvDefaults();
    // 优先返回指定位置的命令行参数，否则返回默认值。
    static QString argOrDefault(const QStringList &args, int index, const QString &fallback);
    // 优先读命令行参数，读不到再读环境变量，最后回退默认值。
    static QString argOrEnv(const QStringList &args,
                            int index,
                            const char *envName,
                            const QString &fallback);
    // 统一返回上海时区当前时间。
    static QDateTime currentShanghaiDateTime();
    // 把任意时间转换成上海时区。
    static QDateTime toShanghaiDateTime(const QDateTime &dateTime);
    // 统一输出带 +08:00 的 ISO 时间字符串。
    static QString currentShanghaiIsoString();
    static QString toShanghaiIsoString(const QDateTime &dateTime);
    // 输出数据库 DATETIME 使用的上海时间字符串。
    static QString toDatabaseDateTimeString(const QDateTime &dateTime);
    // 返回数据库会话时区字符串。
    static QString databaseSessionTimeZone();
};
