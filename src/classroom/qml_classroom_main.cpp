#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "class_init.h"
#include "devicemodel.h"
#include "rest_client.h"

/*
 * 教室端入口程序。
 * 主要职责和管理端类似，但会额外把 initialRoomId 传给 QML，
 * 让教室端可以直接按照指定教室号启动。
 */
int main(int argc, char **argv)
{
    ClassInit::setupConsoleUtf8();
    QQuickStyle::setStyle("Material");
    QQuickStyle::setFallbackStyle("Basic");
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    const QString roomId = ClassInit::argOrDefault(args, 1, "A101");
    const QString baseUrl = ClassInit::argOrEnv(args, 2, "SIM_BACKEND_URL", "http://127.0.0.1:8080");

    QQmlApplicationEngine engine;
    DeviceModel deviceModel;
    RestClient api;
    api.setBaseUrl(baseUrl);

    QObject::connect(&api, &RestClient::devicesReceived, &deviceModel, &DeviceModel::setDevices);

    engine.rootContext()->setContextProperty("api", &api);
    engine.rootContext()->setContextProperty("deviceModel", &deviceModel);
    engine.rootContext()->setContextProperty("initialRoomId", roomId);

    engine.load(QUrl(QStringLiteral("qrc:/classroom/ClassroomApp.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
