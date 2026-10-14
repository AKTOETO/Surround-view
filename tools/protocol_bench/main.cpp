#include "codec.pb.h"
#include <algorithm>
#include <boost/json.hpp>
#include <boost/version.hpp>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace json = boost::json;
volatile uint64_t checksum = 0;
constexpr unsigned iterations = 50000, repeats = 7;

template <class Work> double batch(Work work)
{
    auto begin = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i)
    {
        checksum += work();
    }
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin)
               .count() /
           iterations;
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

template <class Proto>
json::object measure(const char *name, const json::object &object, const Proto &message)
{
    const auto text = json::serialize(object), wire = message.SerializeAsString();
    Proto roundtrip;
    if (!roundtrip.ParseFromString(wire) || roundtrip.SerializeAsString() != wire ||
        json::parse(text) != json::value(object))
    {
        throw std::runtime_error("roundtrip fixture failure");
    }
    auto js = [&]() { return json::serialize(object).size(); };
    auto ps = [&]() { return message.SerializeAsString().size(); };
    const char *id_key = std::is_same_v<Proto, sv::bench::FrameSubset> ? "frame_id" : "command_id";
    auto jp = [&]()
    {
        auto parsed = json::parse(text);
        const auto &id = parsed.as_object().at(id_key).as_string();
        uint64_t result = 0;
        auto converted = std::from_chars(id.data(), id.data() + id.size(), result);
        if (converted.ec != std::errc{} || converted.ptr != id.data() + id.size())
        {
            throw std::runtime_error("ID parse failure");
        }
        return result;
    };
    auto pp = [&]()
    {
        Proto parsed;
        if (!parsed.ParseFromString(wire))
        {
            throw std::runtime_error("parse failure");
        }
        if constexpr (std::is_same_v<Proto, sv::bench::FrameSubset>)
        {
            return parsed.frame_id();
        }
        else
        {
            return parsed.command_id();
        }
    };
    if (jp() != pp())
    {
        throw std::runtime_error("numeric ID mismatch");
    }
    // Both codecs allocate fresh results; no transport, semantic validation or image data.
    for (unsigned i = 0; i < 2000; ++i)
    {
        checksum += js() + ps() + jp() + pp();
    }
    std::vector<double> jse, pse, jpa, ppa;
    for (unsigned r = 0; r < repeats; ++r)
    {
        if (r % 2)
        {
            pse.push_back(batch(ps));
            jse.push_back(batch(js));
            ppa.push_back(batch(pp));
            jpa.push_back(batch(jp));
        }
        else
        {
            jse.push_back(batch(js));
            pse.push_back(batch(ps));
            jpa.push_back(batch(jp));
            ppa.push_back(batch(pp));
        }
    }
    return {{"fixture", name},
            {"json_bytes", text.size()},
            {"protobuf_bytes", wire.size()},
            {"json_serialize_us", median(jse)},
            {"protobuf_serialize_us", median(pse)},
            {"json_parse_id_us", median(jpa)},
            {"protobuf_parse_id_us", median(ppa)},
            {"json_serialize_batches_us", json::value_from(jse)},
            {"protobuf_serialize_batches_us", json::value_from(pse)},
            {"json_parse_id_batches_us", json::value_from(jpa)},
            {"protobuf_parse_id_batches_us", json::value_from(ppa)}};
}

int main()
{
    try
    {
        json::array results;
        sv::bench::Orbit orbit;
        orbit.set_command_id(1234);
        orbit.set_type("orbit");
        orbit.set_azimuth_delta_rad(.05);
        orbit.set_elevation_delta_rad(-.02);
        results.push_back(measure("orbit",
                                  {{"command_id", "1234"},
                                   {"type", "orbit"},
                                   {"azimuth_delta_rad", .05},
                                   {"elevation_delta_rad", -.02}},
                                  orbit));
        sv::bench::Configure configure;
        configure.set_command_id(1235);
        configure.set_type("configure_fusion");
        configure.set_base_config_revision(17);
        auto *f = configure.mutable_fusion();
        f->set_mode("multi_band");
        f->set_diagnostic("color");
        f->set_edge_width_px(24);
        f->set_angle_power(2);
        f->set_pyramid_levels(4);
        f->set_pyramid_boundary("normalized");
        results.push_back(measure("fusion_subset",
                                  {{"command_id", "1235"},
                                   {"type", "configure_fusion"},
                                   {"base_config_revision", "17"},
                                   {"fusion", json::object{{"mode", "multi_band"},
                                                           {"diagnostic", "color"},
                                                           {"edge_width_px", 24},
                                                           {"angle_power", 2},
                                                           {"pyramid_levels", 4},
                                                           {"pyramid_boundary", "normalized"}}}},
                                  configure));
        sv::bench::FrameSubset frame;
        frame.set_frame_id(1234567);
        frame.set_frame_set_id(890123);
        frame.set_config_revision(17);
        frame.set_width(640);
        frame.set_height(360);
        frame.set_stride_bytes(2560);
        frame.set_pixel_format("RGBA8");
        frame.set_row_origin("top_left");
        frame.set_output_product("final_frame");
        json::array inputs;
        for (unsigned camera = 0; camera < 4; ++camera)
        {
            auto *input = frame.add_inputs();
            input->set_camera_id(camera);
            input->set_used(true);
            input->set_sequence_id(890123);
            input->set_release_timestamp_ns(1234567890123456ULL);
            input->set_calibration_id("camera-calibration-fixture-2026");
            inputs.emplace_back(
                json::object{{"camera_id", camera},
                             {"used", true},
                             {"sequence_id", "890123"},
                             {"release_timestamp_ns", "1234567890123456"},
                             {"calibration_id", "camera-calibration-fixture-2026"}});
        }
        results.push_back(measure("frame_metadata_subset",
                                  {{"frame_id", "1234567"},
                                   {"frame_set_id", "890123"},
                                   {"config_revision", "17"},
                                   {"width", 640},
                                   {"height", 360},
                                   {"stride_bytes", 2560},
                                   {"pixel_format", "RGBA8"},
                                   {"row_origin", "top_left"},
                                   {"output_product", "final_frame"},
                                   {"inputs", inputs}},
                                  frame));
        std::cout
            << json::serialize(json::object{
                   {"schema_version", 1},
                   {"scope", "engineering_metadata_microbenchmark"},
                   {"protobuf_version", GOOGLE_PROTOBUF_VERSION},
                   {"boost_version", BOOST_VERSION},
                   {"iterations_per_batch", iterations},
                   {"repeats", repeats},
                   {"compiler", __VERSION__},
                   {"checks_passed", true},
                   {"results", results},
                   {"limitations",
                    json::array{
                        "subsets, not complete production messages",
                        "not end-to-end pipeline or RSS measurement",
                        "decode includes extracting one numeric ID; not full semantic validation",
                        "no arena or payload copies measured"}}})
            << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
