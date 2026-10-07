#include "simulator_bridge.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <iostream>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    sv::client::Endpoint endpoint;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--unix" && i + 1 < argc)
        {
            endpoint.transport = sv::client::Endpoint::Transport::Unix;
            endpoint.directory = argv[++i];
        }
        else if (arg == "--tcp" && i + 3 < argc)
        {
            endpoint.transport = sv::client::Endpoint::Transport::Tcp;
            endpoint.host = argv[++i];
            endpoint.control_port = static_cast<uint16_t>(std::stoul(argv[++i]));
            endpoint.data_port = static_cast<uint16_t>(std::stoul(argv[++i]));
        }
    }

    auto *provider = new SimulatorFrameProvider();
    SimulatorBridge bridge(endpoint, provider);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("frames"), provider);
    engine.rootContext()->setContextProperty(QStringLiteral("bridge"), &bridge);

    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl)
        {
            if (!obj && url == objUrl)
            {
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);
    return app.exec();
}
