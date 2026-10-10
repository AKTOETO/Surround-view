#pragma once
#include "sv/math.hpp"
#include <boost/json.hpp>
#include <filesystem>

namespace sv
{
struct Fusion
{
    std::string mode = "edge_feather", diagnostic = "color";
    double edge_width_px = 24, angle_power = 2, smoothness_weight = .1;
    unsigned pyramid_levels = 4;
};

struct Connections
{
    bool explicit_config = false, unix_enabled = true, tcp_enabled = false;
    std::string unix_directory = "/tmp/sv-prototype", address = "127.0.0.1";
    uint16_t control_port = 0, data_port = 0;
};

struct CameraEndpoint
{
    std::string transport = "unix", path, address = "127.0.0.1";
    uint16_t port = 0;
};

struct SourceConfig
{
    bool explicit_config = false, loop = true;
    std::string type = "replay", manifest;
    unsigned message_timeout_ms = 2000;
    std::array<CameraEndpoint, 4> cameras;
    std::array<std::string, 4> camera_devices = {"/dev/video0", "/dev/video1", "/dev/video2",
                                                 "/dev/video3"};
};

struct Config
{
    std::array<Camera, 4> cameras;
    Surface surface;
    View view;
    Fusion fusion;
    Connections connections;
    SourceConfig source;
    int width = 640, height = 360, queue_size = 3;
    double vehicle_length = 4.6, vehicle_width = 1.8, margin = .1;
    uint64_t skew_ns = 10000000, age_ns = 100000000;
    std::string profile_id;
    boost::json::value effective;
};

Config parse_config(const boost::json::value &);
Config load_config(const std::filesystem::path &);
boost::json::value read_json(const std::filesystem::path &);
void write_json(const std::filesystem::path &, const boost::json::value &);
uint64_t now_ns();
} // namespace sv
