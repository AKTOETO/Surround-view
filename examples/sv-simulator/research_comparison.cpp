#include "research_comparison.hpp"
#include "report_comparison.hpp"
#include "report_output.hpp"
#include "report_series.hpp"
#include "research_batch_draft.hpp"
#include <atomic>
#include <thread>

struct ResearchComparison::Impl
{
    std::atomic_bool cancelled{false};
    std::thread worker;
    bool busy = false, series = false;
    QString status = "Выберите два individual research report файла";
    boost::json::object report;
    QVariantMap view;
};

namespace
{
QVariant convert(const boost::json::value &value)
{
    if (value.is_null())
    {
        return {};
    }
    if (value.is_bool())
    {
        return value.as_bool();
    }
    if (value.is_string())
    {
        return QString::fromUtf8(value.as_string().data(), int(value.as_string().size()));
    }
    if (value.is_number())
    {
        return value.to_number<double>();
    }
    if (value.is_array())
    {
        QVariantList array;
        for (const auto &item : value.as_array())
        {
            array.push_back(convert(item));
        }
        return array;
    }
    QVariantMap map;
    for (const auto &entry : value.as_object())
    {
        map[QString::fromUtf8(entry.key().data(), int(entry.key().size()))] =
            convert(entry.value());
    }
    return map;
}
} // namespace

ResearchComparison::ResearchComparison(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>())
{
}

ResearchComparison::~ResearchComparison()
{
    impl_->cancelled = true;
    if (impl_->worker.joinable())
    {
        impl_->worker.join();
    }
}

bool ResearchComparison::busy() const
{
    return impl_->busy;
}

bool ResearchComparison::series() const
{
    return impl_->series;
}

QString ResearchComparison::status() const
{
    return impl_->status;
}

QVariantMap ResearchComparison::result() const
{
    return impl_->view;
}

bool ResearchComparison::compare(const QUrl &left, const QUrl &right)
{
    return launch({left, right}, false);
}

bool ResearchComparison::compareSeries(const QVariantList &files)
{
    return launch(files, true);
}

bool ResearchComparison::launch(const QVariantList &files, bool series)
{
    if (impl_->busy)
    {
        return false;
    }
    impl_->report.clear();
    impl_->view.clear();
    impl_->series = series;
    try
    {
        if (files.size() < 2 || files.size() > (series ? 8 : 2))
        {
            throw std::invalid_argument("Выберите от 2 до 8 отчётов для серии");
        }
        std::vector<std::filesystem::path> paths;
        for (const auto &file : files)
        {
            paths.emplace_back(researchLocalPath(file.toUrl()).toStdString());
        }
        impl_->cancelled = false;
        impl_->busy = true;
        impl_->status = "Чтение и проверка отчётов…";
        impl_->worker = std::thread(
            [this, paths = std::move(paths), series]
            {
                boost::json::object report;
                QVariantMap view;
                QString status;
                try
                {
                    if (series)
                    {
                        report =
                            sv::research::compare_report_series_files(paths, &impl_->cancelled);
                    }
                    else
                    {
                        report = sv::research::compare_report_files(paths[0], paths[1],
                                                                    &impl_->cancelled);
                        report["left_path"] = paths[0].string();
                        report["right_path"] = paths[1].string();
                    }
                    if (impl_->cancelled)
                    {
                        throw std::runtime_error("cancelled");
                    }
                    view = convert(report).toMap();
                    status = "Сравнение подготовлено. Разницы B−A не означают научную приёмку или "
                             "общий рейтинг.";
                }
                catch (const std::exception &error)
                {
                    report.clear();
                    view.clear();
                    status = "Сравнение не выполнено: " + QString::fromUtf8(error.what());
                }
                QMetaObject::invokeMethod(
                    this,
                    [this, report = std::move(report), view = std::move(view), status]() mutable
                    {
                        impl_->worker.join();
                        impl_->busy = false;
                        if (impl_->cancelled)
                        {
                            impl_->report.clear();
                            impl_->view.clear();
                            impl_->status = "Сравнение отменено";
                        }
                        else
                        {
                            impl_->report = std::move(report);
                            impl_->view = std::move(view);
                            impl_->status = status;
                        }
                        emit changed();
                        emit finished();
                    },
                    Qt::QueuedConnection);
            });
        emit changed();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->busy = false;
        impl_->status = "Сравнение не выполнено: " + QString::fromUtf8(error.what());
        emit changed();
        return false;
    }
}

void ResearchComparison::cancel()
{
    if (impl_->busy)
    {
        impl_->cancelled = true;
        impl_->status = "Ожидание остановки чтения/проверки…";
        emit changed();
    }
}

bool ResearchComparison::save(const QUrl &file)
{
    if (impl_->busy || impl_->report.empty())
    {
        return false;
    }
    try
    {
        const auto path = researchLocalPath(file);
        sv::research::ReportOutput output(path.toStdString());
        output.write(impl_->report);
        impl_->status = "Сравнение сохранено: " + path;
        emit changed();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->status = "Сравнение не сохранено: " + QString::fromUtf8(error.what());
        emit changed();
        return false;
    }
}
