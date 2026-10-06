#include "bridge.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    sv::client::Endpoint endpoint;
    bool smoke = false;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--smoke")
            {
                smoke = true;
                continue;
            }
            if (arg == "--tcp")
            {
                if (i + 3 >= argc)
                {
                    throw std::runtime_error("--tcp HOST CONTROL_PORT DATA_PORT required");
                }
                endpoint.transport = sv::client::Endpoint::Transport::Tcp;
                endpoint.host = argv[++i];
                auto port = [](const char *value)
                {
                    auto n = sv::parse_decimal_u64(value);
                    if (!n || n > 65535)
                    {
                        throw std::runtime_error("port outside 1..65535");
                    }
                    return static_cast<uint16_t>(n);
                };
                endpoint.control_port = port(argv[++i]);
                endpoint.data_port = port(argv[++i]);
            }
            else if (arg.rfind("--", 0) == 0)
            {
                throw std::runtime_error("unknown client option");
            }
            else
            {
                endpoint.directory = arg;
            }
        }
    }
    catch (const std::exception &e)
    {
        qCritical("%s", e.what());
        return 1;
    }
    QQmlApplicationEngine engine;
    auto *provider = new FrameProvider;
    engine.addImageProvider("frames", provider);
    Bridge bridge(endpoint, provider);
    engine.rootContext()->setContextProperty("backend", &bridge);
    engine.load(QUrl(QStringLiteral("qrc:/surround-view/Main.qml")));
    if (engine.rootObjects().isEmpty())
    {
        return 1;
    }
    if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
    {
        QObject::connect(w, &QQuickWindow::frameSwapped, &bridge, &Bridge::presented,
                         Qt::QueuedConnection);
    }
    if (smoke)
    {
        QTimer::singleShot(2000, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
