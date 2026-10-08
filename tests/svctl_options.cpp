#include "options.hpp"
#include <gtest/gtest.h>

TEST(SvctlOptions, DefaultsAndHelp)
{
    const auto options = sv::ctl::parse({});
    EXPECT_EQ(options.request.operation, "state");
    EXPECT_EQ(options.endpoint.transport, sv::client::Endpoint::Transport::Unix);
    EXPECT_TRUE(sv::ctl::parse({"--help"}).help);
}

TEST(SvctlOptions, TcpAndOrbitUnits)
{
    const auto options = sv::ctl::parse(
        {"--tcp", "127.0.0.1", "53101", "53102", "--timeout-ms", "400", "orbit", "-.2", ".3"});
    EXPECT_EQ(options.endpoint.control_port, 53101);
    EXPECT_EQ(options.endpoint.data_port, 53102);
    EXPECT_EQ(options.timeout_ms, 400);
    EXPECT_DOUBLE_EQ(options.request.parameters.at("azimuth_delta_rad").as_double(), -.2);
}

TEST(SvctlOptions, CalibrationCommands)
{
    const auto status_opts = sv::ctl::parse({"calibration-status", "calib-job-42"});
    EXPECT_EQ(status_opts.request.operation, "calibration_status");
    EXPECT_EQ(status_opts.request.parameters.at("job_id").as_string(), "calib-job-42");

    const auto apply_opts = sv::ctl::parse({"apply-calibration", "calib-job-42"});
    EXPECT_EQ(apply_opts.request.operation, "apply_calibration");
    EXPECT_EQ(apply_opts.request.parameters.at("job_id").as_string(), "calib-job-42");

    const auto cancel_opts = sv::ctl::parse({"cancel-calibration", "calib-job-42"});
    EXPECT_EQ(cancel_opts.request.operation, "cancel_calibration");
    EXPECT_EQ(cancel_opts.request.parameters.at("job_id").as_string(), "calib-job-42");
}

TEST(SvctlOptions, GenericCommandPreservesParameters)
{
    const auto options =
        sv::ctl::parse({"command", "extension", "--params", "{\"enabled\":false}"});
    EXPECT_EQ(options.request.operation, "extension");
    EXPECT_FALSE(options.request.parameters.at("enabled").as_bool());
}

TEST(SvctlOptions, InvalidArgumentsNeverReachNetwork)
{
    const std::vector<std::vector<std::string>> cases = {
        {"--tcp", "host", "0", "3"},
        {"--tcp", "host", "65536", "3"},
        {"--unix", ""},
        {"--unix", "/tmp/a", "--tcp", "host", "1", "2"},
        {"--timeout-ms", "9"},
        {"--timeout-ms", "60001"},
        {"orbit", "nan", "0"},
        {"orbit", "1garbage", "0"},
        {"zoom", "inf"},
        {"preset"},
        {"state", "extra"},
        {"calibration-status"},
        {"cancel-calibration"},
        {"apply-calibration"},
        {"typo"},
        {"--unknown"},
        {"command", "extension", "--params", "[]"},
        {"command", "extension", "--params", "{\"command_id\":\"9\"}"},
        {"command", "extension", "--params", "{\"session_id\":\"other\"}"},
        {"command", "extension", "--params", "{\"type\":\"state\"}"}};
    for (const auto &arguments : cases)
    {
        EXPECT_ANY_THROW(sv::ctl::parse(arguments));
    }
}
