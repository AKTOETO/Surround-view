#include "observation_policy.hpp"
#include "report_evidence.hpp"
#include <set>

namespace sv::research
{
boost::json::object observation_policy(bool capture, bool evaluate, bool progress)
{
    return {{"policy", "paused_observation_v1"},
            {"frame_digest", "sha256_rgba8_every_trial_v1"},
            {"capture_frames", capture},
            {"capture_consumer", capture ? "callback_unverified" : "none"},
            {"quality_evaluation", evaluate},
            {"progress_callback", progress},
            {"quality_algorithm", evaluate ? "masked_rgb8_v1" : "none"},
            {"telemetry_before", nullptr},
            {"telemetry_after", nullptr},
            {"telemetry_stable", false}};
}

boost::json::value verified_observation_policy(const boost::json::object &report)
{
    try
    {
        const auto &policy = report.at("observation_policy").as_object();
        const std::set<std::string> keys{"policy",
                                         "frame_digest",
                                         "capture_frames",
                                         "capture_consumer",
                                         "quality_evaluation",
                                         "progress_callback",
                                         "quality_algorithm",
                                         "telemetry_before",
                                         "telemetry_after",
                                         "telemetry_stable"};
        if (policy.size() != keys.size())
        {
            return nullptr;
        }
        for (const auto &field : policy)
        {
            if (!keys.count(std::string(field.key())))
            {
                return nullptr;
            }
        }
        if (policy.at("policy") != "paused_observation_v1" ||
            policy.at("frame_digest") != "sha256_rgba8_every_trial_v1")
        {
            return nullptr;
        }
        if (!policy.at("progress_callback").is_bool())
        {
            return nullptr;
        }
        const bool capture = policy.at("capture_frames").as_bool();
        if (capture != report.at("scenario").as_object().at("capture_frames").as_bool() ||
            policy.at("capture_consumer") != (capture ? "rgba8_files_v1" : "none"))
        {
            return nullptr;
        }
        const bool evaluate = policy.at("quality_evaluation").as_bool();
        if (evaluate != report.contains("quality_reference") ||
            policy.at("quality_algorithm") != (evaluate ? "masked_rgb8_v1" : "none"))
        {
            return nullptr;
        }
        if (!policy.at("telemetry_stable").as_bool() ||
            !comparison::equal(policy.at("telemetry_before"), policy.at("telemetry_after")))
        {
            return nullptr;
        }
        for (const auto *key : {"telemetry_before", "telemetry_after"})
        {
            const auto &telemetry = policy.at(key).as_object();
            if (telemetry.size() != 3 || !telemetry.at("enabled").is_bool())
            {
                return nullptr;
            }
            const auto period = comparison::integer(telemetry.at("period_ms"), 5000);
            const auto capacity = comparison::integer(telemetry.at("history_capacity"), 256);
            if (period < 50 || capacity < 8)
            {
                return nullptr;
            }
        }
        return policy;
    }
    catch (const std::exception &)
    {
        return nullptr;
    }
}
} // namespace sv::research
