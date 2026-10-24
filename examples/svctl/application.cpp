#include "batch.hpp"
#include "batch_bundle.hpp"
#include "calibration_run.hpp"
#include "file_experiment.hpp"
#include "options.hpp"
#include "report_output.hpp"
#include "report_series.hpp"
#include "scenario_bundle.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace sv::ctl
{
int execute(const Options &options)
{
    if (options.request.operation == "calibration-run")
    {
        const auto &p = options.request.parameters;
        client::Options connection;
        connection.endpoint = options.endpoint;
        connection.timeout_ms = options.timeout_ms;
        const auto report = research::run_calibration_cases(
            std::string(p.at("scenario_path").as_string()),
            std::string(p.at("report_path").as_string()), connection);
        std::cout << boost::json::serialize(
                         boost::json::object{{"success", report.at("success")},
                                             {"all_completed", report.at("all_completed")},
                                             {"report_path", p.at("report_path")}})
                  << '\n';
        return report.at("success").as_bool() && report.at("all_completed").as_bool() ? 0 : 4;
    }
    if (options.request.operation == "research-compare-series")
    {
        const auto &p = options.request.parameters;
        std::vector<std::filesystem::path> paths;
        for (const auto &path : p.at("reports").as_array())
        {
            paths.emplace_back(std::string(path.as_string()));
        }
        const auto audit = research::compare_report_series_files(paths);
        research::ReportOutput output(std::string(p.at("report_path").as_string()));
        output.write(audit);
        std::cout << boost::json::serialize(audit) << '\n';
        return 0;
    }
    if (options.request.operation == "research-batch-bundle-export")
    {
        const auto &p = options.request.parameters;
        std::cout << boost::json::serialize(research::export_batch_bundle(
                         std::string(p.at("scenario_path").as_string()),
                         std::string(p.at("report_path").as_string())))
                  << '\n';
        return 0;
    }
    if (options.request.operation == "research-batch-bundle-verify" ||
        options.request.operation == "research-batch-bundle-run")
    {
        const auto &p = options.request.parameters;
        research::BatchBundle bundle(std::string(p.at("scenario_path").as_string()));
        if (options.request.operation == "research-batch-bundle-verify")
        {
            std::cout << boost::json::serialize(bundle.summary()) << '\n';
            return 0;
        }
        client::Options connection;
        connection.endpoint = options.endpoint;
        connection.timeout_ms = options.timeout_ms;
        const auto report =
            bundle.execute(std::string(p.at("report_path").as_string()), connection);
        std::cout << boost::json::serialize(report) << '\n';
        return research::research_exit_code(report);
    }
    if (options.request.operation == "research-bundle-export")
    {
        const auto &p = options.request.parameters;
        const auto receipt =
            research::export_scenario_bundle(std::string(p.at("scenario_path").as_string()),
                                             std::string(p.at("report_path").as_string()));
        std::cout << boost::json::serialize(receipt) << '\n';
        return 0;
    }
    if (options.request.operation == "research-bundle-verify" ||
        options.request.operation == "research-bundle-run")
    {
        const auto &p = options.request.parameters;
        research::ScenarioBundle bundle(std::string(p.at("scenario_path").as_string()));
        if (options.request.operation == "research-bundle-verify")
        {
            std::cout << boost::json::serialize(bundle.summary()) << '\n';
            return 0;
        }
        client::Options connection;
        connection.endpoint = options.endpoint;
        connection.timeout_ms = options.timeout_ms;
        const auto report =
            bundle.execute(std::string(p.at("report_path").as_string()), connection);
        std::cout << boost::json::serialize(
                         boost::json::object{{"success", report.at("success")},
                                             {"restored", report.at("restored")},
                                             {"quality_status", research::quality_status(report)},
                                             {"input_bundle", report.at("input_bundle")}})
                  << '\n';
        return research::research_exit_code(report);
    }
    if (options.request.operation == "research" || options.request.operation == "research-batch")
    {
        const auto &parameters = options.request.parameters;
        client::Options connection;
        connection.endpoint = options.endpoint;
        connection.timeout_ms = options.timeout_ms;
        boost::json::object report;
        const std::string input_path(parameters.at("scenario_path").as_string());
        const std::filesystem::path output_path(
            std::string(parameters.at("report_path").as_string()));
        auto report_path = output_path;
        if (options.request.operation == "research-batch")
        {
            report = research::run_batch(input_path, output_path, connection);
            report_path /= "batch.json";
        }
        else
        {
            research::FileExperiment experiment(input_path);
            experiment.reserve(output_path);
            report = experiment.execute(connection);
        }
        std::cout << boost::json::serialize(
                         boost::json::object{{"success", report.at("success")},
                                             {"report_path", report_path.string()},
                                             {"restored", report.at("restored")},
                                             {"quality_status", research::quality_status(report)}})
                  << '\n';
        return research::research_exit_code(report);
    }
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<client::Event> events;
    bool overflow = false;
    client::Options connection;
    connection.endpoint = options.endpoint;
    connection.timeout_ms = options.timeout_ms;
    // A one-shot mutation must never be replayed after losing its session.
    connection.max_retries = 0;
    client::Client client(connection,
                          [&](client::Event event)
                          {
                              std::lock_guard<std::mutex> lock(mutex);
                              if (events.size() == 64)
                              {
                                  overflow = true;
                              }
                              else
                              {
                                  events.push_back(std::move(event));
                              }
                              changed.notify_one();
                          });
    uint64_t command_id = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeout_ms);
    while (true)
    {
        client::Event event;
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!changed.wait_until(lock, deadline, [&] { return overflow || !events.empty(); }))
            {
                throw std::runtime_error("command timeout; outcome may be unknown if already sent");
            }
            if (overflow)
            {
                throw std::runtime_error("consumer event queue overflow");
            }
            event = std::move(events.front());
            events.pop_front();
        }
        if (event.kind == client::Event::Kind::Error)
        {
            throw std::runtime_error(event.detail);
        }
        if (event.kind == client::Event::Kind::State)
        {
            if (event.detail == "ready" && !command_id)
            {
                command_id = client.command(options.request.operation, options.request.parameters);
            }
            else if (event.detail == "retry_exhausted")
            {
                throw std::runtime_error(event.detail);
            }
            continue;
        }
        if (!event.message)
        {
            continue;
        }
        const auto &message = *event.message;
        if (message.type == 11)
        {
            // Legacy protocol always opens data. Drain/release until control-only is implemented.
            client.release(message.header);
        }
        else if (message.type == 21 && command_id &&
                 message.header.at("command_id").as_string() == std::to_string(command_id))
        {
            const bool accepted = message.header.at("accepted").as_bool();
            std::cout << boost::json::serialize(message.header) << '\n';
            client.stop();
            return accepted ? 0 : 4;
        }
    }
}
} // namespace sv::ctl
