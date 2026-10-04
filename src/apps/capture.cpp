#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <iostream>
#include <map>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

int main(int argc, char **argv)
{
    try
    {
        std::map<std::string, std::string> options;
        for (int i = 1; i < argc; i += 2)
        {
            const std::string key = argv[i];
            if (i + 1 == argc ||
                (key != "--config" && key != "--output" && key != "--frames" &&
                 key != "--source0" && key != "--source1" && key != "--source2" &&
                 key != "--source3") ||
                !options.emplace(key, argv[i + 1]).second)
            {
                throw std::runtime_error("invalid capture argument");
            }
        }
        if (!options.count("--config") || !options.count("--output"))
        {
            throw std::runtime_error(
                "usage: sv-capture --config FILE --output NEW_DIR --source0 SOURCE --source1 "
                "SOURCE --source2 SOURCE --source3 SOURCE [--frames 30]");
        }
        const auto config = sv::load_config(options.at("--config"));
        const std::filesystem::path output = options.at("--output");
        const int count = options.count("--frames") ? std::stoi(options.at("--frames")) : 30;
        if (count < 1 || count > 10000 || std::filesystem::exists(output))
        {
            throw std::runtime_error("require 1..10000 frames and a new output directory");
        }
        std::array<cv::VideoCapture, 4> cameras;
        boost::json::array sources, ids;
        for (int k = 0; k < 4; ++k)
        {
            auto key = "--source" + std::to_string(k);
            if (!options.count(key))
            {
                throw std::runtime_error(key + " required");
            }
            const auto &source = options.at(key);
            const bool index =
                !source.empty() && source.find_first_not_of("0123456789") == std::string::npos;
            const bool opened = index ? cameras[k].open(std::stoi(source), cv::CAP_ANY)
                                      : cameras[k].open(source, cv::CAP_ANY);
            if (!opened)
            {
                throw std::runtime_error("cannot open source " + std::to_string(k));
            }
            cameras[k].set(cv::CAP_PROP_FRAME_WIDTH, config.cameras[k].width);
            cameras[k].set(cv::CAP_PROP_FRAME_HEIGHT, config.cameras[k].height);
            sources.push_back(boost::json::object{
                {"camera_id", k}, {"source", source}, {"backend", cameras[k].getBackendName()}});
            ids.emplace_back(config.cameras[k].calibration_id);
        }
        std::filesystem::create_directories(output);
        boost::json::array rows, timing;
        boost::json::object hashes;
        uint64_t epoch = 0;
        for (int frame = 0; frame < count; ++frame)
        {
            const auto cycle_start = sv::now_ns();
            for (int k = 0; k < 4; ++k)
            {
                if (!cameras[k].grab())
                {
                    throw std::runtime_error("grab/EOF at source " + std::to_string(k));
                }
            }
            std::array<cv::Mat, 4> images;
            std::array<uint64_t, 4> delivered;
            for (int k = 0; k < 4; ++k)
            {
                if (!cameras[k].retrieve(images[k]))
                {
                    throw std::runtime_error("retrieve failed");
                }
                delivered[k] = sv::now_ns();
                if (images[k].type() != CV_8UC3 || images[k].cols != config.cameras[k].width ||
                    images[k].rows != config.cameras[k].height)
                {
                    throw std::runtime_error(
                        "capture resolution/format differs from calibration; resize forbidden");
                }
            }
            const uint64_t anchor = delivered[3];
            if (!epoch)
            {
                epoch = anchor;
            }
            if (anchor - delivered[0] > 1000000000)
            {
                throw std::runtime_error("capture delivery skew exceeds manifest limit");
            }
            boost::json::array paths, offsets;
            for (int k = 0; k < 4; ++k)
            {
                auto name = "camera-" + std::to_string(k) + "-" + std::to_string(frame) + ".png";
                if (!cv::imwrite((output / name).string(), images[k]))
                {
                    throw std::runtime_error("capture image write failed");
                }
                paths.emplace_back(name);
                offsets.emplace_back(-int64_t(anchor - delivered[k]));
                hashes[name] = sv::file_sha256(output / name);
            }
            rows.push_back(
                boost::json::object{{"scenario_timestamp_ns", std::to_string(anchor - epoch)},
                                    {"paths", paths},
                                    {"offset_ns", offsets}});
            timing.push_back(boost::json::object{{"cycle_start_ns", std::to_string(cycle_start)},
                                                 {"last_delivery_ns", std::to_string(anchor)},
                                                 {"delivery_skew_ns", anchor - delivered[0]}});
        }
        sv::write_json(output / "manifest.json",
                       boost::json::object{{"schema_version", 1},
                                           {"calibration_ids", ids},
                                           {"origin", "opencv_video_capture"},
                                           {"sha256", hashes},
                                           {"frames", rows}});
        sv::write_json(
            output / "capture.json",
            boost::json::object{
                {"opencv_version", sv::opencv_version()},
                {"sources", sources},
                {"delivery", timing},
                {"timestamp_scope",
                 "Host steady-clock after retrieve; not sensor exposure timestamps"},
                {"replay_limit", "Current server advances rows at 30 Hz; original capture "
                                 "intervals retained but not replayed as a clock"}});
        std::cout << "captured " << count << " four-camera rows at " << output << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
