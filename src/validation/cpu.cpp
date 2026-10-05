#include "sv/frame.hpp"
#include "sv/protocol.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <cmath>
#include <functional>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <random>

namespace sv
{
boost::json::array qualify_cpu(const Config &config, const std::filesystem::path &work)
{
    boost::json::array checks;
    auto run = [&](const char *id, const char *detail, const std::function<void()> &action)
    {
        const auto start = now_ns();
        try
        {
            action();
            checks.push_back(boost::json::object{{"id", id},
                                                 {"status", "pass"},
                                                 {"detail", detail},
                                                 {"elapsed_ms", (now_ns() - start) / 1e6}});
        }
        catch (const std::exception &e)
        {
            checks.push_back(
                boost::json::object{{"id", id}, {"status", "fail"}, {"detail", e.what()}});
        }
    };
    const auto require = [](bool valid)
    {
        if (!valid)
        {
            throw std::runtime_error("criterion failed");
        }
    };
    const auto rejects = [&](const std::function<void()> &action)
    {
        bool rejected = false;
        try
        {
            action();
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected);
    };
    run("HASH_SHA256", "Known SHA-256 vector",
        [&]
        {
            require(sha256("abc") ==
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        });
    run("MATH_OPENCV",
        "8192 deterministic points; zero and nonzero fisheye coefficients; tolerance 1e-9 px",
        [&]
        {
            std::mt19937 engine(20261005);
            std::vector<Vec3> points;
            for (int j = 0; j < 1024; ++j)
            {
                points.push_back({-6 + 12.0 * (engine() % 100000) / 100000,
                                  -4.5 + 9.0 * (engine() % 100000) / 100000,
                                  1.5 * (engine() % 100000) / 100000});
            }
            for (auto camera : config.cameras)
            {
                for (int distortion = 0; distortion < 2; ++distortion)
                {
                    camera.k = distortion ? std::array<double, 4>{.03, -.005, .001, .0001}
                                          : std::array<double, 4>{};
                    auto actual = project_opencv(camera, points);
                    for (size_t i = 0; i < points.size(); ++i)
                    {
                        auto expected = project(camera, points[i]);
                        require(expected.valid == actual[i].valid);
                        if (expected.valid)
                        {
                            require(std::hypot(expected.u - actual[i].u, expected.v - actual[i].v) <
                                    1e-9);
                        }
                    }
                }
            }
        });
    run("MATH_CENTRAL_INVALID", "Central ray; behind-camera and nonfinite rejection",
        [&]
        {
            Camera c;
            c.width = c.height = 100;
            c.fx = c.fy = 20;
            c.cx = c.cy = 49.5;
            auto p = project_opencv(c, {{0, 0, 1}, {0, 0, -1}, {NAN, 0, 1}});
            require(p[0].valid && p[0].u == 49.5 && p[0].v == 49.5 && !p[1].valid && !p[2].valid);
        });
    run("TRANSFORM_INVERSE", "Rigid transform and inverse",
        [&]
        {
            auto point = Vec3{2, 3, 4};
            auto restored = transform(inverse_rigid(config.cameras[0].T),
                                      transform(config.cameras[0].T, point));
            require(std::sqrt(dot(restored - point, restored - point)) < 1e-10);
        });
    run("CONFIG_STRICT", "Unknown field, nonrigid transform and nonmonotonic fisheye rejected",
        [&]
        {
            auto bad = config.effective;
            bad.as_object()["unexpected"] = true;
            rejects([&] { parse_config(bad); });
            bad = config.effective;
            bad.as_object()["cameras"]
                .as_array()[0]
                .as_object()["T_camera_from_vehicle"]
                .as_array()[0]
                .as_array()[0] = 2.;
            rejects([&] { parse_config(bad); });
            bad = config.effective;
            bad.as_object()["cameras"].as_array()[0].as_object()["projection"].as_object()["k"] =
                boost::json::array{-1., 0., 0., 0.};
            rejects([&] { parse_config(bad); });
        });
    run("MESH_WINDING", "All triangles have positive XY winding; flat center and corner height",
        [&]
        {
            auto mesh = make_mesh(config.surface);
            for (size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                auto a = mesh.vertices[mesh.indices[i]], b = mesh.vertices[mesh.indices[i + 1]],
                     c = mesh.vertices[mesh.indices[i + 2]];
                require(cross(b - a, c - a).z > 0);
            }
            require(config.surface.point(0, 0).z == 0 &&
                    std::abs(config.surface.point(config.surface.A, config.surface.B).z -
                             config.surface.H) < 1e-10);
        });
    run("ENCLOSURE_MESH", "Dome, cylinder cap and cube shell have nondegenerate outward triangles",
        [&]
        {
            for (const auto &mesh :
                 {make_dome_mesh(12, 8, 24), make_cylinder_shell(12, 12, 8, 24, 8),
                  make_box_shell(12, 12, 8)})
            {
                for (size_t index = 0; index < mesh.indices.size(); index += 3)
                {
                    const auto a = mesh.vertices.at(mesh.indices[index]);
                    const auto b = mesh.vertices.at(mesh.indices[index + 1]);
                    const auto c = mesh.vertices.at(mesh.indices[index + 2]);
                    require(dot(cross(b - a, c - a), a + b + c) > 0);
                }
            }
        });
    run("SRGB_LINEAR", "sRGB round trip and linear half-intensity",
        [&]
        {
            require(std::abs(encode_srgb(.5) - .7353569830524495) < 1e-10);
            for (int i = 0; i < 256; ++i)
            {
                require(std::abs(encode_srgb(decode_srgb(i / 255.0)) - i / 255.0) < 1e-10);
            }
        });
    run("IMAGE_RGB_ORIGIN", "PNG BGR-to-RGB; asymmetric top/bottom marker",
        [&]
        {
            cv::Mat marker(8, 8, CV_8UC3, cv::Scalar(255, 0, 0));
            marker.row(0).setTo(cv::Scalar(0, 0, 255));
            auto file = work / "rgb-marker.png";
            require(cv::imwrite(file.string(), marker));
            auto image = read_image(file);
            require(image.pixels[0] == 255 && image.pixels[2] == 0 &&
                    image.pixels[7 * 8 * 3] == 0 && image.pixels[7 * 8 * 3 + 2] == 255);
        });
    const auto image = std::make_shared<Image>();
    run("SYNC_COMPLETE_STALE", "Fresh four-camera set; age expiry returns NO_INPUT",
        [&]
        {
            Synchronizer sync(config);
            for (int i = 0; i < 4; ++i)
            {
                sync.push({i, 0, 1000000000, 0, image});
            }
            require(sync.select(1000000000).health == "READY" &&
                    sync.select(1000000000 + config.age_ns + 1).health == "NO_INPUT");
        });
    run("SYNC_SKEW", "Total selected skew cannot exceed configured window",
        [&]
        {
            Synchronizer sync(config);
            for (int i = 0; i < 4; ++i)
            {
                sync.push({i, 0, 1000000000 + i * config.skew_ns, 0, image});
            }
            auto set = sync.select(1000000000 + 4 * config.skew_ns);
            require(set.health == "DEGRADED" && set.skew_ns <= config.skew_ns);
        });
    run("SYNC_ORDER_BOUND", "Duplicate/out-of-order rejection and bounded queue",
        [&]
        {
            Synchronizer sync(config);
            sync.push({0, 1, 100, 0, image});
            require(!sync.push({0, 1, 100, 0, image}) && !sync.push({0, 2, 99, 0, image}));
            for (unsigned i = 2; i < 40; ++i)
            {
                sync.push({0, i, 100 + i, 0, image});
            }
            require(sync.size(0) == size_t(config.queue_size) && sync.duplicate == 1 &&
                    sync.out_of_order == 1 && sync.dropped > 0);
        });
    run("PROTOCOL_FRAGMENTED", "One-byte and coalesced message reads",
        [&]
        {
            auto packet = encode({20, {{"command_id", "18446744073709551615"}}, {1, 2, 255}});
            Decoder decoder;
            size_t count = 0;
            for (auto byte : packet)
            {
                count += decoder.feed(&byte, 1).size();
            }
            require(count == 1);
            auto doubled = packet;
            doubled.insert(doubled.end(), packet.begin(), packet.end());
            require(decoder.feed(doubled.data(), doubled.size()).size() == 2);
        });
    run("PROTOCOL_LIMITS", "Invalid length and decimal uint64 rejected",
        [&]
        {
            auto packet = encode({1, {}, {}});
            packet[8] = 255;
            rejects(
                [&]
                {
                    Decoder decoder;
                    decoder.feed(packet.data(), packet.size());
                });
            for (const char *invalid : {"", "-1", "+1", "1x", "18446744073709551616"})
            {
                rejects([&] { parse_decimal_u64(invalid); });
            }
        });
    run("OPENCV_BOARD", "Generated 7x5 inner-corner board detected; blank image rejected",
        [&]
        {
            cv::Mat board(296, 368, CV_8UC1, cv::Scalar(255));
            for (int y = 0; y < 6; ++y)
            {
                for (int x = 0; x < 8; ++x)
                {
                    if ((x + y) % 2 == 0)
                    {
                        cv::rectangle(board, cv::Rect(40 + x * 36, 40 + y * 36, 36, 36),
                                      cv::Scalar(0), cv::FILLED);
                    }
                }
            }
            require(detect_board(board, {7, 5}).size() == 35);
            rejects([&] { detect_board(cv::Mat(200, 200, CV_8UC1, cv::Scalar(255)), {7, 5}); });
        });
    run("OPENCV_INTRINSICS",
        "Eight synthetic board poses recover fisheye intrinsics; held-out pose reprojection < 0.01 "
        "px",
        [&]
        {
            const cv::Matx33d K{310, 0, 319.5, 0, 305, 239.5, 0, 0, 1};
            const cv::Vec4d distortion{.03, -.005, .001, -.0001};
            std::vector<cv::Point3d> board;
            for (int y = 0; y < 6; ++y)
            {
                for (int x = 0; x < 9; ++x)
                {
                    board.emplace_back((x - 4) * .04, (y - 2.5) * .04, 0);
                }
            }
            std::vector<std::vector<cv::Point3d>> objects;
            std::vector<std::vector<cv::Point2d>> pixels;
            for (int i = 0; i < 8; ++i)
            {
                std::vector<cv::Point2d> image;
                cv::fisheye::projectPoints(
                    board, image, cv::Vec3d(-.3 + i * .08, .2 - i * .05, -.12 + i * .04),
                    cv::Vec3d(-.12 + (i % 3) * .11, -.06 + (i % 2) * .12, .4 + (i % 4) * .07), K,
                    distortion);
                objects.push_back(board);
                pixels.push_back(image);
            }
            auto calibrated = calibrate_intrinsics(objects, pixels, {640, 480});
            require(std::abs(calibrated.K(0, 0) - K(0, 0)) < .01 &&
                    std::abs(calibrated.K(1, 1) - K(1, 1)) < .01);
            std::vector<cv::Point2d> held_out;
            cv::fisheye::projectPoints(board, held_out, cv::Vec3d(.25, -.22, .09),
                                       cv::Vec3d(.08, -.04, .51), K, distortion);
            for (auto error : intrinsic_validation_errors(calibrated, board, held_out))
            {
                require(error < .01);
            }
        });
    return checks;
}
} // namespace sv
