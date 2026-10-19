#pragma once
#include "bundle_io.hpp"
#include "digest.hpp"
#include "regular_input.hpp"
#include "scenario_bundle.hpp"
#include "sv/frame.hpp"
#include "sv/json.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <sys/stat.h>

namespace bundle_fixture
{
inline void json(const std::filesystem::path &path, const boost::json::object &value)
{
    std::ofstream(path) << boost::json::serialize(value) << '\n';
}

inline boost::json::object document(const std::filesystem::path &path)
{
    return sv::parse_json(sv::research::read_regular_file(path, 1024 * 1024)).as_object();
}

struct BundleFixture : testing::Test
{
    sv::research::bundle_detail::TemporaryDirectory directory;
    std::filesystem::path source = directory.path / "original # %",
                          bundle = directory.path / "bundle";
    boost::json::object scenario, reference;

    void SetUp() override
    {
        std::filesystem::create_directory(source);
        const boost::json::object view{{"azimuth_rad", 0.},
                                       {"elevation_rad", 1.},
                                       {"distance_m", 8.},
                                       {"fov_y_rad", 1.},
                                       {"clip_m", boost::json::array{.1, 50.}},
                                       {"target_m", boost::json::array{0., 0., 0.}}};
        boost::json::array guards;
        for (unsigned i = 0; i < 4; ++i)
        {
            guards.push_back(boost::json::object{
                {"camera_id", i}, {"calibration_id", "fixture"}, {"source_timestamp_ns", "0"}});
        }
        reference = {
            {"schema_version", 1},
            {"kind", "regression"},
            {"dataset_id", "bundle-fixture"},
            {"width", 2},
            {"height", 2},
            {"view", view},
            {"criteria", boost::json::object{{"max_mae_rgb8", 1.}}},
            {"frames", boost::json::array{boost::json::object{
                           {"rgb", "rgb # %.ppm"}, {"mask", "mask.ppm"}, {"inputs", guards}}}}};
        scenario = {{"schema_version", 1},
                    {"frames", 1},
                    {"warmup", 0},
                    {"repeats", 1},
                    {"reset_replay", true},
                    {"variants", boost::json::array{boost::json::object{{"mode", "edge_feather"}}}},
                    {"quality_reference", "reference.json"},
                    {"quality_criteria", boost::json::object{{"max_mae_rgb8", .5}}}};
        sv::write_ppm(source / "rgb # %.ppm",
                      sv::Image{2, 2, 3, std::vector<unsigned char>(12, 10)});
        sv::write_ppm(source / "mask.ppm", sv::Image{2, 2, 3, std::vector<unsigned char>(12, 255)});
        json(source / "reference.json", reference);
        json(source / "scenario.json", scenario);
    }

    boost::json::object export_bundle()
    {
        return sv::research::export_scenario_bundle(source / "scenario.json", bundle);
    }

    // Updating a descriptor lets negative tests reach semantic/closure checks rather than only
    // hashes.
    void update_descriptor(boost::json::object &manifest, const std::string &name)
    {
        const auto bytes = sv::research::read_regular_file(bundle / name, 1024 * 1024);
        for (auto &entry : manifest.at("files").as_array())
        {
            if (std::string(entry.as_object().at("path").as_string()) == name)
            {
                entry.as_object()["bytes"] = bytes.size();
                entry.as_object()["sha256"] = sv::research::sha256(bytes.data(), bytes.size());
            }
        }
    }
};
} // namespace bundle_fixture
