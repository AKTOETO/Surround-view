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
} // namespace gui_bundle
