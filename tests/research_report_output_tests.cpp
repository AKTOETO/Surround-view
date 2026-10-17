#include "report_output.hpp"
#include "sv/json.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
struct ReportDirectory
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("sv-report-output-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ReportDirectory()
    {
        std::filesystem::create_directory(path);
    }

    ~ReportDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::string contents(const std::filesystem::path &path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace

TEST(ResearchReport, ExclusiveReservationPreservesExistingReportsAndDanglingLinks)
{
    ReportDirectory directory;
    const auto path = directory.path / "report.json";
    std::ofstream(path) << "previous report";
    EXPECT_THROW(sv::research::ReportOutput{path}, std::runtime_error);
    EXPECT_EQ(contents(path), "previous report");
    const auto link = directory.path / "dangling.json";
    std::filesystem::create_symlink("absent.json", link);
    EXPECT_THROW(sv::research::ReportOutput{link}, std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_symlink(link));
    EXPECT_FALSE(std::filesystem::exists(directory.path / "absent.json"));
}

TEST(ResearchReport, ReservedReportCannotBeTruncatedBySecondWriterAndWritesOnlyOnce)
{
    ReportDirectory directory;
    const auto path = directory.path / "new.json";
    sv::research::ReportOutput output(path);
    EXPECT_THROW(sv::research::ReportOutput{path}, std::runtime_error);
    const boost::json::object report{{"success", true}, {"value", 1.1959916878056157e-7}};
    output.write(report);
    const auto text = contents(path);
    EXPECT_EQ(sv::parse_json(text), report);
    EXPECT_THROW(output.write(report), std::logic_error);
    EXPECT_EQ(contents(path), text);
}
