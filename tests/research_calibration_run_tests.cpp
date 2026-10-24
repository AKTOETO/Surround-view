#include "calibration_run.hpp"
#include "sv/json.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
class CalibrationRun : public testing::Test
{
  protected:

    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("sv-calibration-run-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path input = directory / "input.json", output = directory / "report.json";

    void SetUp() override
    {
        std::filesystem::create_directory(directory);
    }

    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    boost::json::object item()
    {
        return {{"case_id", "test"},
                {"parameters",
                 {{"camera_id", 0},
                  {"method", "sqpnp"},
                  {"points", boost::json::array{}},
                  {"pixels", boost::json::array{}},
                  {"validation_points", boost::json::array{}},
                  {"validation_pixels", boost::json::array{}},
                  {"provenance", boost::json::object{}}}}};
    }

    void save(const boost::json::array &cases)
    {
        std::ofstream(input) << boost::json::serialize(
            boost::json::object{{"schema_version", 1}, {"cases", cases}});
    }
};

TEST_F(CalibrationRun, InvalidEnvelopeRejectedBeforeReportReservationOrNetwork)
{
    save({});
    EXPECT_THROW(sv::research::run_calibration_cases(input, output, {}), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(output));
    save({item(), item()});
    EXPECT_THROW(sv::research::run_calibration_cases(input, output, {}), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(output));
    auto bad = item();
    bad["parameters"].as_object()["extra"] = true;
    save({bad});
    EXPECT_THROW(sv::research::run_calibration_cases(input, output, {}), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(output));
    bad = item();
    bad["parameters"].as_object()["points"] = std::string(sv::max_header, 'x');
    save({bad});
    EXPECT_THROW(sv::research::run_calibration_cases(input, output, {}), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST_F(CalibrationRun, ExistingReportPreservedBeforeConnection)
{
    save({item()});
    std::ofstream(output) << "preserve existing report";
    EXPECT_THROW(sv::research::run_calibration_cases(input, output, {}), std::runtime_error);
    std::ifstream file(output);
    const std::string contents{std::istreambuf_iterator<char>(file), {}};
    EXPECT_EQ(contents, "preserve existing report");
}
} // namespace
