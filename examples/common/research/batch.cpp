#include "batch.hpp"
#include "batch_plan.hpp"
#include "digest.hpp"
#include "file_experiment.hpp"
#include "report_output.hpp"
#include <stdexcept>

namespace sv::research
{
namespace
{
boost::json::object execute_batch(std::string_view text, const std::filesystem::path &base,
                                  const std::filesystem::path &output_directory,
                                  const client::Options &options, const std::atomic_bool *cancel,
                                  std::function<void(unsigned, unsigned, unsigned)> progress,
                                  bool snapshot)
{
    const auto plan = parse_batch_plan(text);
    const auto &runs = plan.at("runs").as_array();
    std::vector<std::unique_ptr<FileExperiment>> experiments;
    boost::json::array members;
    size_t bytes = 0, trials = 0;
    for (const auto &value : runs)
    {
        const auto &row = value.as_object();
        const std::string id(row.at("id").as_string());
        const std::string path(row.at("scenario").as_string());
        auto experiment = std::make_unique<FileExperiment>(base / path);
        bytes += experiment->reference_bytes();
        trials += experiment->trial_count();
        if (!experiment->resets_replay() || bytes > 128 * 1024 * 1024 || trials > 10000)
        {
            throw std::invalid_argument(
                "batch requires reset_replay and at most 128 MiB references/10000 trials");
        }
        members.push_back(boost::json::object{{"id", id},
                                              {"scenario", path},
                                              {"scenario_input_sha256", experiment->input_sha256()},
                                              {"report", id + ".json"},
                                              {"status", "not_started"}});
        experiments.push_back(std::move(experiment));
    }
    // A new directory prevents overwriting any previous member, plan, or report.
    if (!std::filesystem::create_directory(output_directory))
    {
        throw std::runtime_error("batch output directory must be new");
    }
    ReportOutput summary_output(output_directory / "batch.json");
    for (size_t index = 0; index < experiments.size(); ++index)
    {
        experiments[index]->reserve(
            output_directory / std::string(members[index].as_object().at("report").as_string()));
    }
    boost::json::object summary{
        {"schema_version", 1},
        {"scope", "same_server_recording_v1"},
        {"plan_sha256", sha256(text.data(), text.size())},
        {"plan", plan},
        {"success", true},
        {"restored", true},
        {"quality_acceptance", boost::json::object{{"status", "not_requested"}}},
        {"completed", 0},
        {"reported", 0},
        {"status", "complete"}};
    if (snapshot)
    {
        const auto inputs = output_directory / "inputs";
        if (!std::filesystem::create_directory(inputs))
        {
            throw std::runtime_error("batch inputs directory must be new");
        }
        ReportOutput plan_output(inputs / "plan.json");
        plan_output.write(plan);
        summary["plan_snapshot"] = "inputs/plan.json";
    }
    unsigned criteria_requested = 0, quality_failed = 0;
    for (size_t index = 0; index < experiments.size(); ++index)
    {
        auto &member = members[index].as_object();
        if (cancel && cancel->load())
        {
            summary["status"] = "cancelled";
            summary["success"] = false;
            break;
        }
        try
        {
            if (progress)
            {
                progress(static_cast<unsigned>(index), 0,
                         static_cast<unsigned>(experiments[index]->trial_count()));
            }
            auto report = experiments[index]->execute(options, cancel,
                                                      [&, index](unsigned done, unsigned total)
                                                      {
                                                          if (progress)
                                                          {
                                                              progress(static_cast<unsigned>(index),
                                                                       done, total);
                                                          }
                                                      });
            const auto status = quality_status(report);
            member["quality_status"] = status;
            member["restored"] = report.at("restored");
            member["success"] = report.at("success");
            member["status"] = "reported";
            summary["reported"] = index + 1;
            criteria_requested += status != "not_requested";
            quality_failed += status == "failed";
            if (research_exit_code(report) == 4)
            {
                summary["success"] = false;
                summary["restored"] = report.at("restored");
                summary["status"] = cancel && cancel->load() ? "cancelled" : "failed";
                break;
            }
            summary["completed"] = index + 1;
        }
        catch (const std::exception &error)
        {
            member["status"] = "error";
            member["error"] = error.what();
            summary["success"] = false;
            // Without a returned report, restoration cannot be established.
            summary["restored"] = false;
            summary["status"] = "failed";
            break;
        }
    }
    auto &acceptance = summary.at("quality_acceptance").as_object();
    acceptance["policy"] = "all_defined_member_criteria_v1";
    acceptance["reported_criteria_members"] = criteria_requested;
    acceptance["failed_criteria_members"] = quality_failed;
    acceptance["status"] = !summary.at("success").as_bool() ? "incomplete"
                           : quality_failed                 ? "failed"
                           : criteria_requested             ? "passed"
                                                            : "not_requested";
    summary["runs"] = std::move(members);
    summary_output.write(summary);
    return summary;
}
} // namespace

boost::json::object run_batch(const std::filesystem::path &plan_path,
                              const std::filesystem::path &output_directory,
                              const client::Options &options, const std::atomic_bool *cancel,
                              std::function<void(unsigned, unsigned, unsigned)> progress)
{
    const auto text = read_batch_plan(plan_path);
    return execute_batch(text, plan_path.parent_path(), output_directory, options, cancel,
                         std::move(progress), false);
}

boost::json::object run_batch(const boost::json::object &plan, const std::filesystem::path &base,
                              const std::filesystem::path &output_directory,
                              const client::Options &options, const std::atomic_bool *cancel,
                              std::function<void(unsigned, unsigned, unsigned)> progress)
{
    return execute_batch(boost::json::serialize(plan) + '\n', base, output_directory, options,
                         cancel, std::move(progress), true);
}
} // namespace sv::research
