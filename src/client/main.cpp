#include "bridge.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QString directory = argc > 1 ? argv[1] : "/tmp/sv-prototype";
    QQmlApplicationEngine engine;
    auto *provider = new FrameProvider;
    engine.addImageProvider("frames", provider);
    Bridge bridge(directory, provider);
    engine.rootContext()->setContextProperty("backend", &bridge);
    engine.load(QUrl::fromLocalFile(SV_QML_PATH));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
        QObject::connect(w, &QQuickWindow::frameSwapped, &bridge, &Bridge::presented,
                         Qt::QueuedConnection);
    if (argc > 2 && QString(argv[2]) == "--smoke")
        QTimer::singleShot(2000, &app, &QCoreApplication::quit);
    return app.exec();
}
