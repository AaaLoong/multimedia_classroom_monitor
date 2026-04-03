#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "class_init.h"
#include "devicemodel.h"
#include "rest_client.h"

/*
 * 管理端入口程序。
 * 负责完成 Qt 应用初始化、QML 上下文对象挂载，以及把 RestClient 和 DeviceModel 暴露给界面。
 */
int main(int argc, char **argv)
{
    // 先处理控制台编码，再统一设置 Material 风格，最后进入 Qt 应用主循环。
    ClassInit::setupConsoleUtf8();
    QQuickStyle::setStyle("Material");
    QQuickStyle::setFallbackStyle("Basic");
    QGuiApplication app(argc, argv);
    ClassInit::loadRuntimeEnvDefaults();
    QQmlApplicationEngine engine;

    DeviceModel deviceModel;
    RestClient api;
    const auto args = app.arguments();
    const QString baseUrl = ClassInit::argOrEnv(args, 1, "SIM_BACKEND_URL", "http://127.0.0.1:8080");
    api.setBaseUrl(baseUrl);

    // 当后端返回设备数据时，直接更新到 QML 可绑定的列表模型。
    QObject::connect(&api, &RestClient::devicesReceived, &deviceModel, &DeviceModel::setDevices);

    // 把接口对象放进 QML 上下文，供 AdminApp.qml 直接调用。
    engine.rootContext()->setContextProperty("api", &api);
    engine.rootContext()->setContextProperty("deviceModel", &deviceModel);

    engine.load(QUrl(QStringLiteral("qrc:/admin/AdminApp.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
