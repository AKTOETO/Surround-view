#pragma once
#include <QStringList>
#include <QtGlobal>
#include <mutex>

// engine.warnings does not include every qt.qml.context / property-cache warning.
class QmlWarningCapture
{
    inline static QmlWarningCapture *active_ = nullptr;
    inline static std::mutex mutex_;
    QtMessageHandler previous_;
    QStringList messages_;

    static void capture(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg))
        {
            active_->messages_ << QString::fromUtf8(context.category) + ": " + message;
        }
    }

  public:

    QmlWarningCapture()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = this;
        previous_ = qInstallMessageHandler(capture);
    }

    ~QmlWarningCapture()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        qInstallMessageHandler(previous_);
        active_ = nullptr;
    }

    QmlWarningCapture(const QmlWarningCapture &) = delete;
    QmlWarningCapture &operator=(const QmlWarningCapture &) = delete;

    QStringList messages() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }
};
