#include "class_init.h"

#include <QByteArray>

#ifdef _WIN32
#include <windows.h>
#endif

/*
 * 这个文件只放“启动时公共辅助函数”。
 * 这些函数不会参与业务流程本身，但它们能让不同入口程序共享同一套参数读取和编码处理逻辑。
 */

void ClassInit::setupConsoleUtf8()
{
#ifdef _WIN32
    // Windows 控制台默认不是 UTF-8，这里手动切换，避免中文输出乱码。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
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
