#include "calibration_run.hpp"
#include "support/tcp_peer.hpp"
#include "sv/json.hpp"
#include <atomic>
#include <fstream>
#include <future>
#include <gtest/gtest.h>

namespace
{
using sv::test::Tcp;
namespace asio = boost::asio;
enum class Fault
{
    AdmissionLost,
    InvalidAdmission,
    StatusLost,
    StatusRejected,
    CancelLost,
    CancelRejected,
    InvalidTransform,
    WrongJob,
    WrongCamera,
    FinalStateLost
};

class CalibrationFailure : public testing::TestWithParam<Fault>
{
};

TEST_P(CalibrationFailure, PartialReportRetainsKnownAndUnknownOutcomesWithoutRetry)
{
    const auto fault = GetParam();
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("sv-calibration-fault-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);

    struct Cleanup
    {
        std::filesystem::path path;

        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};

    const auto input = directory / "cases.json", output = directory / "report.json";
    const boost::json::object parameters{{"camera_id", 0},
                                         {"method", "sqpnp"},
                                         {"points", boost::json::array{}},
                                         {"pixels", boost::json::array{}},
                                         {"validation_points", boost::json::array{}},
                                         {"validation_pixels", boost::json::array{}},
                                         {"provenance", boost::json::object{}}};
    std::ofstream(input) << boost::json::serialize(boost::json::object{
        {"schema_version", 1},
        {"cases",
         {boost::json::object{{"case_id", "first"}, {"parameters", parameters}},
          boost::json::object{{"case_id", "second"}, {"parameters", parameters}}}}});
    asio::io_context io;
    Tcp::acceptor control(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    Tcp::acceptor data(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    sv::client::Options options;
    options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
    options.endpoint.control_port = control.local_endpoint().port();
    options.endpoint.data_port = data.local_endpoint().port();
    options.timeout_ms = 250;
    std::atomic<unsigned> submissions{0}, cancellations{0};
    auto peer = std::async(
        std::launch::async,
        [&]
        {
            Tcp::socket ctl(io), video(io);
            sv::test::accept(control, ctl);
            sv::test::Reader commands(ctl);
            commands.receive();
            sv::test::send(ctl, {2,
                                 {{"session_id", "session"},
                                  {"data_token", "token"},
                                  {"capabilities", {"calibration_result_v1"}}},
                                 {}});
            sv::test::accept(data, video);
            sv::test::Reader frames(video);
            frames.receive();
            sv::test::send(video, {2, {{"session_id", "session"}}, {}});
            unsigned states = 0;
            try
            {
                while (true)
                {
                    const auto message = commands.receive();
                    const auto type = message.header.at("type").as_string();
                    boost::json::object ack{{"command_id", message.header.at("command_id")},
                                            {"accepted", true},
                                            {"config_revision", "0"}};
                    if (type == "fusion_catalog")
                    {
                        ack["fusion_catalog"] = boost::json::object{};
                    }
                    else if (type == "state")
                    {
                        if (++states == 2 && fault == Fault::FinalStateLost)
                        {
                            return;
                        }
                    }
                    else if (type == "calibrate")
                    {
                        ++submissions;
                        if (fault == Fault::AdmissionLost)
                        {
                            return;
                        }
                        ack["job_id"] = "job-" + std::to_string(submissions.load());
                        if (fault == Fault::InvalidAdmission)
                        {
                            ack["job_id"] = "";
                        }
                    }
                    else if (type == "calibration_status")
                    {
                        if (fault == Fault::StatusLost)
                        {
                            return;
                        }
                        ack["job_id"] = fault == Fault::WrongJob ? boost::json::value("other-job")
                                                                 : message.header.at("job_id");
                        ack["camera_id"] = fault == Fault::WrongCamera ? 1 : 0;
                        if (fault == Fault::StatusRejected)
                        {
                            ack["accepted"] = false;
                            ack["reason"] = "unknown_job";
                        }
                        const bool pending =
                            fault == Fault::CancelLost || fault == Fault::CancelRejected;
                        ack["job_state"] = pending ? "pending" : "completed";
                        ack["T_camera_from_vehicle"] = boost::json::array{
                            boost::json::array{1, 0, 0, 0}, boost::json::array{0, 1, 0, 0},
                            boost::json::array{0, 0, 1, 0}, boost::json::array{0, 0, 0, 1}};
                        if (fault == Fault::InvalidTransform)
                        {
                            ack["T_camera_from_vehicle"].as_array().pop_back();
                        }
                    }
                    else if (type == "cancel_calibration")
                    {
                        ++cancellations;
                        if (fault == Fault::CancelLost)
                        {
                            return;
                        }
                        ack["accepted"] = false;
                        ack["reason"] = "not_cancellable";
                    }
                    else
                    {
                        throw std::runtime_error("unexpected mock command");
                    }
                    sv::test::send(ctl, {21, ack, {}});
                }
            }
            catch (const boost::system::system_error &error)
            {
                if (error.code() != asio::error::eof &&
                    error.code() != asio::error::connection_reset)
                {
                    throw;
                }
            }
        });
    const auto report = sv::research::run_calibration_cases(input, output, options);
    EXPECT_NO_THROW(peer.get());
    EXPECT_FALSE(report.at("success").as_bool());
    EXPECT_TRUE(std::filesystem::file_size(output) > 0);
    std::ifstream saved(output);
    const std::string saved_text{std::istreambuf_iterator<char>(saved),
                                 std::istreambuf_iterator<char>()};
    EXPECT_EQ(sv::parse_json(saved_text), report);
    const auto &results = report.at("results").as_array();
    if (fault == Fault::FinalStateLost)
    {
        ASSERT_EQ(results.size(), 2U);
        EXPECT_TRUE(report.at("all_completed").as_bool());
        EXPECT_EQ(report.at("started_cases"), 2);
        EXPECT_TRUE(report.at("unstarted_case_ids").as_array().empty());
        EXPECT_EQ(submissions, 2U);
        EXPECT_EQ(results[0].as_object().at("outcome"), "completed");
        EXPECT_EQ(results[1].as_object().at("outcome"), "completed");
        return;
    }
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FALSE(report.at("all_completed").as_bool());
    EXPECT_EQ(report.at("started_cases"), 1);
    EXPECT_EQ(report.at("unstarted_case_ids"), boost::json::array{"second"});
    EXPECT_EQ(submissions, 1U);
    const auto &result = results[0].as_object();
    EXPECT_FALSE(result.at("error").as_string().empty());
    if (fault == Fault::AdmissionLost || fault == Fault::InvalidAdmission)
    {
        EXPECT_EQ(result.at("outcome"), "admission_unknown");
        EXPECT_FALSE(result.contains("job_id"));
    }
    else if (fault == Fault::StatusLost)
    {
        EXPECT_EQ(result.at("outcome"), "result_unknown");
        EXPECT_EQ(result.at("job_id"), "job-1");
    }
    else if (fault == Fault::StatusRejected)
    {
        EXPECT_EQ(result.at("outcome"), "status_rejected");
        EXPECT_FALSE(result.at("status").as_object().at("accepted").as_bool());
        EXPECT_EQ(result.at("job_id"), "job-1");
    }
    else if (fault == Fault::CancelLost || fault == Fault::CancelRejected)
    {
        EXPECT_EQ(result.at("outcome"), "timeout");
        EXPECT_EQ(report.at("error"), "calibration case deadline exceeded");
        EXPECT_EQ(cancellations, 1U);
        if (fault == Fault::CancelLost)
        {
            EXPECT_TRUE(result.contains("cancel_error"));
        }
        else
        {
            EXPECT_FALSE(result.at("cancel_ack").as_object().at("accepted").as_bool());
        }
    }
    else
    {
        EXPECT_EQ(result.at("outcome"), "invalid_result");
        EXPECT_TRUE(result.contains("status"));
    }
}

INSTANTIATE_TEST_SUITE_P(TcpFaults, CalibrationFailure,
                         testing::Values(Fault::AdmissionLost, Fault::InvalidAdmission,
                                         Fault::StatusLost, Fault::StatusRejected,
                                         Fault::CancelLost, Fault::CancelRejected,
                                         Fault::InvalidTransform, Fault::WrongJob,
                                         Fault::WrongCamera, Fault::FinalStateLost));
} // namespace
