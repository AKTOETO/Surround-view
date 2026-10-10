#include "simulator_bridge.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <iostream>
#include <stdexcept>

namespace
{
uint16_t parse_port(const char *value)
{
    const auto parsed = std::stoul(value);
    if (parsed < 1 || parsed > 65535)
    {
        throw std::invalid_argument("port must be in 1..65535");
    }
    return static_cast<uint16_t>(parsed);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    sv::client::Endpoint endpoint;
    bool explicit_endpoint = false;
    bool smoke = false;
    bool world_smoke = false;

    try
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--unix" && i + 1 < argc)
            {
                endpoint.transport = sv::client::Endpoint::Transport::Unix;
                endpoint.directory = argv[++i];
                explicit_endpoint = true;
            }
            else if (arg == "--tcp" && i + 3 < argc)
            {
                endpoint.transport = sv::client::Endpoint::Transport::Tcp;
                endpoint.host = argv[++i];
                endpoint.control_port = parse_port(argv[++i]);
                endpoint.data_port = parse_port(argv[++i]);
                if (endpoint.control_port == endpoint.data_port)
                {
                    throw std::invalid_argument("control and data ports must differ");
                }
                explicit_endpoint = true;
            }
            else if (arg == "--help")
            {
                std::cout
                    << "Usage: sv-simulator [--unix DIR | --tcp HOST CONTROL_PORT DATA_PORT]\n"
                       "Without an endpoint, local IPC discovery starts automatically.\n"
                       "--world-smoke loads the optional Qt Quick 3D driving scene.\n";
                return 0;
            }
            else if (arg == "--smoke")
            {
                smoke = true;
            }
            else if (arg == "--world-smoke")
            {
                world_smoke = true;
            }
            else
            {
                throw std::invalid_argument("unknown or incomplete argument: " + arg);
            }
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "sv-simulator: " << error.what() << '\n';
        return 2;
    }
    if (!explicit_endpoint)
    {
        endpoint.transport = sv::client::Endpoint::Transport::Unix;
        endpoint.directory.clear();
    }
    if (world_smoke && SV_SIMULATOR_QUICK3D_AVAILABLE == 0)
    {
        std::cerr << "sv-simulator: this build has no Qt Quick 3D support\n";
        return 2;
    }

    auto *provider = new SimulatorFrameProvider();
    SimulatorBridge bridge(endpoint, provider);
    int smoke_result = 0;
    if (smoke)
    {
        smoke_result = 1;
        QObject::connect(&bridge, &SimulatorBridge::frameReceived, &app,
                         [&app, &smoke_result]
                         {
                             smoke_result = 0;
                             QTimer::singleShot(100, &app, &QCoreApplication::quit);
                         });
        QTimer::singleShot(10000, &app, &QCoreApplication::quit);
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("frames"), provider);
    engine.rootContext()->setContextProperty(QStringLiteral("bridge"), &bridge);
    engine.rootContext()->setContextProperty(QStringLiteral("runtimeSettings"),
                                             bridge.runtimeSettings());
    engine.rootContext()->setContextProperty(QStringLiteral("driveWorldAvailable"),
                                             SV_SIMULATOR_QUICK3D_AVAILABLE != 0);
    engine.rootContext()->setContextProperty(QStringLiteral("startDrivingWorld"), world_smoke);
    if (world_smoke)
    {
        QTimer::singleShot(3000, &app, &QCoreApplication::quit);
    }

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
    const int app_result = app.exec();
    return smoke ? smoke_result : app_result;
}
