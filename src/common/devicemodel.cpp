#include "devicemodel.h"

/*
 * DeviceModel 的实现分成三层：
 * 1. QAbstractListModel 的基本接口；
 * 2. 整体刷新/重建；
 * 3. 单条设备的增量更新。
 */

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// 返回模型当前有多少行，QML 的 ListView 会依赖这个值。
int DeviceModel::rowCount(const QModelIndex &) const
{
    return devices_.size();
}

// 根据角色返回对应字段，让 QML 可以按名字直接读取。
QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= devices_.size())
        return {};
    const auto &d = devices_.at(index.row());
    switch (role) {
    case RoomRole:
        return d.roomId;
    case DeviceIdRole:
        return d.deviceId;
    case NameRole:
        return d.name;
    case StatusRole:
        return d.status;
    case UpdatedRole:
        return d.updated;
    default:
        return {};
    }
}

// 角色名映射表，QML 中显示的 roomId、deviceId 就是从这里来的。
QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {
        {RoomRole, "roomId"},
        {DeviceIdRole, "deviceId"},
        {NameRole, "name"},
        {StatusRole, "status"},
        {UpdatedRole, "updated"},
    };
}

// 清空当前缓存，常用于教室取消注册或界面切换。
void DeviceModel::refresh()
{
    beginResetModel();
    devices_.clear();
    indexMap_.clear();
    endResetModel();
}

// 用一批新的设备数据整体重建模型。
void DeviceModel::setDevices(const QVariantList &items)
{
    beginResetModel();
    devices_.clear();
    indexMap_.clear();
    for (const auto &variant : items) {
        const auto map = variant.toMap();
        ModelDevice d;
        d.roomId = map.value("room_id").toString();
        d.deviceId = map.value("device_id").toString();
        d.name = map.value("name").toString();
        if (d.name.isEmpty())
            d.name = map.value("deviceId").toString();
        d.status = map.value("status").toString();
        d.updated = map.value("updated").toString();
        devices_.append(d);
        indexMap_[d.roomId + "::" + d.deviceId] = devices_.size() - 1;
    }
    endResetModel();
}

// 按行号取出设备的扁平数据，方便 QML 进行局部读取。
QVariantMap DeviceModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= devices_.size())
        return map;
    const auto &d = devices_.at(row);
    map["room_id"] = d.roomId;
    map["roomId"] = d.roomId;
    map["device_id"] = d.deviceId;
    map["deviceId"] = d.deviceId;
    map["name"] = d.name;
    map["status"] = d.status;
    map["updated"] = d.updated;
    return map;
}

// 如果列表里没有这台设备，就插入新行；如果有，就直接更新那一行。
void DeviceModel::upsertDevice(const ModelDevice &d)
{
    const QString key = d.roomId + "::" + d.deviceId;
    auto it = indexMap_.find(key);
    if (it == indexMap_.end()) {
        const int row = devices_.size();
        beginInsertRows(QModelIndex(), row, row);
        devices_.append(d);
        indexMap_[key] = row;
        endInsertRows();
    } else {
        const int row = it->second;
        devices_[row] = d;
        emit dataChanged(index(row), index(row));
    }
}

// 设备状态上报进入这里，转成列表模型能理解的结构后再更新。
void DeviceModel::onState(const QString &roomId, const QString &deviceId, const QString &name, const QString &status)
{
    ModelDevice d;
    d.roomId = roomId;
    d.deviceId = deviceId;
    d.name = name;
    d.status = status;
    d.updated = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
    upsertDevice(d);
}
