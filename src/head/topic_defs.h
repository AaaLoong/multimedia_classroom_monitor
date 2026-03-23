#pragma once

#include <QString>

namespace Topics {
// 后端向教室下发控制命令的主题：classroom/<roomId>/cmd/<deviceId>
inline QString commandTopic(const QString &roomId, const QString &deviceId)
{
    return QStringLiteral("classroom/%1/cmd/%2").arg(roomId, deviceId);
}

// 教室向后端上报设备状态的主题：classroom/<roomId>/state/<deviceId>
inline QString stateTopic(const QString &roomId, const QString &deviceId)
{
    return QStringLiteral("classroom/%1/state/%2").arg(roomId, deviceId);
}

// 教室向后端上报警告或报修的主题：classroom/<roomId>/alert/<deviceId>
inline QString alertTopic(const QString &roomId, const QString &deviceId)
{
    return QStringLiteral("classroom/%1/alert/%2").arg(roomId, deviceId);
}

// 教室心跳主题：classroom/<roomId>/heartbeat
inline QString heartbeatTopic(const QString &roomId)
{
    return QStringLiteral("classroom/%1/heartbeat").arg(roomId);
}
} // 主题命名空间结束
