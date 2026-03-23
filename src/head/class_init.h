#pragma once

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
    // 优先返回指定位置的命令行参数，否则返回默认值。
    static QString argOrDefault(const QStringList &args, int index, const QString &fallback);
    // 优先读命令行参数，读不到再读环境变量，最后回退默认值。
    static QString argOrEnv(const QStringList &args,
                            int index,
                            const char *envName,
                            const QString &fallback);
};
