#pragma once
#include "research_controller.hpp"
#include "research_draft.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <fstream>

namespace gui_bundle
{
inline bool wait(ResearchController &controller)
{
    QElapsedTimer timer;
    timer.start();
    while (controller.busy() && timer.elapsed() < 12000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return !controller.busy();
}

inline QVariantMap values()
{
    return {{"modes", QStringList{"edge_feather"}},
            {"frames", 1},
            {"warmup", 0},
            {"repeats", 1},
            {"seed", 17},
            {"boundary", "both"},
            {"pyramid_levels", 3},
            {"seam_solver", "binary_pairs"}};
}

inline QString scenario(const QString &root)
{
    const auto file = root + "/scenario # %.json";
    const auto document = researchDraft({{"modes", QStringList{"edge_feather"}},
                                         {"frames", 1},
                                         {"warmup", 0},
                                         {"repeats", 1},
                                         {"seed", 17},
                                         {"boundary", "both"},
                                         {"pyramid_levels", 3},
                                         {"seam_solver", "binary_pairs"}});
    std::ofstream(file.toStdString()) << boost::json::serialize(document);
    return file;
}

inline QString plan(const QString &root, const QString &source)
{
    const auto file = root + "/plan # %.json";
    boost::json::array runs;
    for (const auto *id : {"first", "second"})
    {
        runs.push_back(boost::json::object{{"id", id}, {"scenario", source.toStdString()}});
    }
    std::ofstream(file.toStdString())
        << boost::json::serialize(boost::json::object{{"schema_version", 1}, {"runs", runs}});
    return file;
}
} // namespace gui_bundle
