#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <unordered_map>

/*
 * ModelDevice 是 DeviceModel 里保存的一行设备数据。
 * 它只保存界面展示需要的扁平字段，方便 QML 直接绑定。
 */
struct ModelDevice {
    QString roomId;
    QString deviceId;
    QString name;
    QString status;
    QString updated;
};

/*
 * DeviceModel 是给 QML 使用的设备列表模型。
 * 它负责把后端/仿真返回的设备数组转换成列表数据，
 * 并在设备状态变化时做增量刷新。
 */
class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
public:
    // 这些角色名会直接暴露给 QML，界面可以按字段名读取。
    enum Roles { RoomRole = Qt::UserRole + 1, DeviceIdRole, NameRole, StatusRole, UpdatedRole };

    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    // 清空模型，常用于教室切换或取消注册后的重置。
    void refresh();
    // 整体导入一批设备数据。
    Q_INVOKABLE void setDevices(const QVariantList &items);
    // 提供给 QML 按行读取单个设备。
    Q_INVOKABLE QVariantMap get(int row) const;
    // 处理状态上报，更新对应设备所在行。
    void onState(const QString &roomId, const QString &deviceId, const QString &name, const QString &status);

private:
    QList<ModelDevice> devices_;
    // 键的格式为 roomId::deviceId，用于快速定位设备所在行。
    std::unordered_map<QString, int> indexMap_;
    void upsertDevice(const ModelDevice &d);
};
