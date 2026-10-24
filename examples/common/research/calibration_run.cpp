#include "calibration_run.hpp"
#include "connection.hpp"
#include "digest.hpp"
#include "report_output.hpp"
#include "sv/build_passport.hpp"
#include "sv/json.hpp"
#include <cmath>
#include <fstream>
#include <set>
#include <thread>

namespace sv::research
{
boost::json::object run_calibration_cases(const std::filesystem::path &input,
                                          const std::filesystem::path &path,
                                          client::Options options)
{
    std::ifstream file(input, std::ios::binary);
    std::string text(8 * 1024 * 1024 + 1, '\0');
    file.read(text.data(), text.size());
    text.resize(static_cast<size_t>(file.gcount()));
    if (file.bad() || text.empty() || text.size() > 8 * 1024 * 1024)
    {
        throw std::invalid_argument("calibration cases require 1..8388608 bytes");
    }
    const auto document = parse_json(text).as_object();
    if (document.size() != 2 || document.at("schema_version") != 1)
    {
        throw std::invalid_argument("calibration cases schema 1 required");
    }
    const auto &cases = document.at("cases").as_array();
    if (cases.empty() || cases.size() > 256)
    {
        throw std::invalid_argument("calibration cases require 1..256 entries");
    }
    std::set<std::string> ids;
    for (const auto &value : cases)
    {
        const auto &item = value.as_object();
        const std::string id(item.at("case_id").as_string());
        if (item.size() != 2 || id.empty() || id.size() > 128 ||
            id.find('\0') != std::string::npos || !ids.insert(id).second)
        {
            throw std::invalid_argument("unique bounded calibration case_id required");
        }
        auto parameters = item.at("parameters").as_object();
        if (parameters.size() != 7)
        {
            throw std::invalid_argument("calibration case requires seven parameter fields");
        }
        for (const auto *key : {"camera_id", "method", "points", "pixels", "validation_points",
                                "validation_pixels", "provenance"})
        {
            parameters.at(key);
        }
        // Bound every request before opening sockets. Semantic validation belongs to server.
        parameters["type"] = "calibrate";
        parameters["command_id"] = "18446744073709551615";
        (void)encode({20, parameters, {}});
    }
    ReportOutput output(path);
    boost::json::object report{{"schema_version", 1},
                               {"scope", "server_calibration_cases_no_apply_v1"},
                               {"input_sha256", sha256(text.data(), text.size())},
                               {"planned", cases.size()},
                               {"success", false},
                               {"all_completed", false},
                               {"results", boost::json::array{}}};
    try
    {
        Connection connection(options);
        if (!connection.supports("calibration_result_v1"))
        {
            throw std::runtime_error("server lacks calibration_result_v1");
        }
        report["client_build_passport"] = configured_build_passport();
        const auto catalog = connection.request("fusion_catalog");
        if (!catalog.at("accepted").as_bool())
        {
            throw std::runtime_error("calibration catalog rejected");
        }
        report["catalog"] = catalog.at("fusion_catalog");
        const auto initial = connection.request("state");
        if (!initial.at("accepted").as_bool())
        {
            throw std::runtime_error("calibration initial state rejected");
        }
        report["initial_state"] = initial;
        bool all_completed = true;
        for (const auto &value : cases)
        {
            const auto &item = value.as_object();
            auto &results = report.at("results").as_array();
            results.emplace_back(boost::json::object{{"case_id", item.at("case_id")},
                                                     {"parameters_sha256", nullptr}});
            auto &result = results.back().as_object();
            const auto parameters = item.at("parameters").as_object();
            const auto encoded = boost::json::serialize(parameters);
            result["parameters_sha256"] = sha256(encoded.data(), encoded.size());
            const auto admission = connection.request("calibrate", parameters);
            result["admission"] = admission;
            if (!admission.at("accepted").as_bool())
            {
                result["outcome"] = "rejected";
                all_completed = false;
                continue;
            }
            const auto job = admission.at("job_id");
            result["job_id"] = job;
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeout_ms);
            while (true)
            {
                const auto status = connection.request("calibration_status", {{"job_id", job}});
                result["status"] = status;
                if (!status.at("accepted").as_bool())
                {
                    throw std::runtime_error("calibration status rejected");
                }
                const auto state = status.at("job_state").as_string();
                if (state == "completed" || state == "failed" || state == "cancelled")
                {
                    if (state == "completed")
                    {
                        const auto &matrix = status.at("T_camera_from_vehicle").as_array();
                        if (matrix.size() != 4)
                        {
                            throw std::runtime_error("invalid calibration result transform");
                        }
                        for (const auto &row : matrix)
                        {
                            if (row.as_array().size() != 4)
                            {
                                throw std::runtime_error("invalid calibration result transform");
                            }
                            for (const auto &coordinate : row.as_array())
                            {
                                if (!coordinate.is_number() ||
                                    !std::isfinite(coordinate.to_number<double>()))
                                {
                                    throw std::runtime_error(
                                        "nonfinite calibration result transform");
                                }
                            }
                        }
                    }
                    result["outcome"] = state;
                    all_completed = all_completed && state == "completed";
                    break;
                }
                if (state != "pending" && state != "running")
                {
                    throw std::runtime_error("unknown calibration job state");
                }
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    result["outcome"] = "timeout";
                    (void)connection.request("cancel_calibration", {{"job_id", job}});
                    throw std::runtime_error("calibration case deadline exceeded");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        const auto final = connection.request("state");
        report["final_state"] = final;
        if (!final.at("accepted").as_bool() ||
            initial.at("config_revision") != final.at("config_revision"))
        {
            throw std::runtime_error("configuration changed during calibration cases");
        }
        report["success"] = true;
        report["all_completed"] = all_completed;
    }
    catch (const std::exception &error)
    {
        report["error"] = error.what();
    }
    output.write(report);
    return report;
}
} // namespace sv::research
