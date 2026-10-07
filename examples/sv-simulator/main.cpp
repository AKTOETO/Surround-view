#include "simulator_bridge.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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
                std::cout << "Usage: sv-simulator [--unix DIR | --tcp HOST CONTROL_PORT DATA_PORT]\n"
                             "Without an endpoint, local IPC discovery starts automatically.\n";
                return 0;
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
