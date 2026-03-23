#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <random>

// 设备状态枚举，统一描述设备在仿真端和后端里的运行状态。
enum class DeviceStatus { Offline, Off, Starting, On, Fault };

/*
 * Device 是项目中最基础的设备数据结构。
 * 它只保存设备的标识、名称、所属教室和状态，不参与界面渲染。
 * toJson() 会把内部状态转换成后端和 MQTT 使用的 JSON 格式。
 */
struct Device
{
    QString id;
    QString name;
    QString roomId;
    DeviceStatus status = DeviceStatus::Off;

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["room_id"] = roomId;
        QString s;
        switch (status) {
        case DeviceStatus::Offline:
            s = "离线";
            break;
        case DeviceStatus::Off:
            s = "关机";
            break;
        case DeviceStatus::Starting:
            s = "启动中";
            break;
        case DeviceStatus::On:
            s = "开机";
            break;
        case DeviceStatus::Fault:
            s = "设备故障";
            break;
        }
        obj["status"] = s;
        return obj;
    }
};

/*
 * Randomizer 用来模拟仿真端的随机行为。
 * 例如设备响应延迟和小概率故障，都是通过它来生成的。
 */
class Randomizer
{
public:
    Randomizer()
        : rng_(std::random_device{}())
    {
    }

    // 在闭区间 [minMs, maxMs] 内随机取一个整数。
    int between(int minMs, int maxMs)
    {
        std::uniform_int_distribution<int> dist(minMs, maxMs);
        return dist(rng_);
    }

    // 按百分比判断是否命中，用来模拟低概率事件。
    bool hitPercent(double percent)
    {
        std::uniform_real_distribution<double> dist(0.0, 100.0);
        return dist(rng_) < percent;
    }

private:
    std::mt19937 rng_;
};

// 返回一个教室的默认设备清单，注册后和仿真初始化都会用到。
inline QList<Device> defaultDevicesForRoom(const QString &roomId)
{
    return {
        {"proj", "投影仪", roomId, DeviceStatus::Off},
        {"amp", "功放", roomId, DeviceStatus::Off},
        {"pc", "讲台电脑", roomId, DeviceStatus::Off},
        {"mic", "无线麦克风", roomId, DeviceStatus::Off},
        {"screen", "电动幕布", roomId, DeviceStatus::Off},
    };
}
