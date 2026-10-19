#include "report_comparison.hpp"
#include "digest.hpp"
#include "observation_policy.hpp"
#include "regular_input.hpp"
#include "report_evidence.hpp"
#include "sv/build_passport.hpp"
#include "sv/json.hpp"
#include "trial_plan.hpp"
#include <cmath>
#include <stdexcept>

namespace sv::research
{
namespace
{
using namespace comparison;

boost::json::value pick(const boost::json::object &report, const char *key, size_t index)
{
    const auto *array = report.if_contains(key);
    if (!array)
    {
        return nullptr;
    }
    if (!array->is_array() || array->as_array().size() > 32)
    {
        throw std::invalid_argument("invalid report summary array");
    }
    boost::json::value result;
    for (const auto &entry : array->as_array())
    {
        const auto &object = entry.as_object();
        if (integer(object.at("variant"),
                    unsigned(report.at("scenario").as_object().at("variants").as_array().size() -
                             1)) != index)
        {
            continue;
        }
        if (!result.is_null())
        {
            throw std::invalid_argument("duplicate variant summary");
        }
        result = object;
    }
    return result;
}

boost::json::value number(const boost::json::object &object, const char *key)
{
    const auto *v = object.if_contains(key);
    if (!v || v->is_null())
    {
        return nullptr;
    }
    if (!v->is_number() || !std::isfinite(v->to_number<double>()) || v->to_number<double>() < 0)
    {
        throw std::invalid_argument("invalid report metric");
    }
    return *v;
}

boost::json::object compact(const boost::json::object &report, size_t index)
{
    boost::json::object row{
        {"render_p50_ms", nullptr}, {"render_p95_ms", nullptr}, {"measured", 0},
        {"mae", nullptr},           {"rmse", nullptr},          {"psnr", nullptr},
        {"exact_match", false},     {"quality_frames", 0},      {"selected_pixels", 0}};
    const auto summary = pick(report, "summaries", index);
    if (!summary.is_null())
    {
        const auto &timings = summary.as_object().at("timings").as_object();
        if (const auto *wall = timings.if_contains("render_wall"); wall && !wall->is_null())
        {
            const auto &object = wall->as_object();
            row["measured"] = integer(object.at("count"), 10000);
            row["render_p50_ms"] = number(object, "p50_ms");
            row["render_p95_ms"] = number(object, "p95_ms");
        }
    }
    const auto quality = pick(report, "quality_summaries", index);
    if (!quality.is_null())
    {
        const auto &object = quality.as_object();
        row["quality_frames"] = integer(object.at("frames"), 256);
        for (const auto *name :
             {"pixel_weighted_mae_rgb8", "pixel_weighted_rmse_rgb8", "pixel_weighted_psnr_db"})
        {
            (void)number(object, name);
        }
        row["mae"] = number(object, "pixel_weighted_mae_rgb8");
        row["rmse"] = number(object, "pixel_weighted_rmse_rgb8");
        row["psnr"] = number(object, "pixel_weighted_psnr_db");
        if (const auto *exact = object.if_contains("exact_match"))
        {
            row["exact_match"] = exact->as_bool();
        }
        if (object.contains("selected_pixels"))
        {
            row["selected_pixels"] = number(object, "selected_pixels");
        }
    }
    if (row.at("exact_match").as_bool() &&
        (!row.at("mae").is_number() || row.at("mae").to_number<double>() != 0 ||
         !row.at("rmse").is_number() || row.at("rmse").to_number<double>() != 0 ||
         !row.at("psnr").is_null() || row.at("quality_frames").to_number<unsigned>() == 0))
    {
        throw std::invalid_argument("inconsistent exact-match summary");
    }
    return row;
}

boost::json::object passport(const boost::json::object &report)
{
    boost::json::object result{{"success", report.at("success")},
                               {"restored", report.at("restored")},
                               {"quality_status", "not_requested"},
                               {"platform_attestation", "not_available"}};
    if (const auto *quality = report.if_contains("quality_acceptance"))
    {
        result["quality_status"] = quality->as_object().at("status");
    }
    if (const auto *catalog = report.if_contains("catalog"))
    {
        for (const auto *key :
             {"backend", "gl_vendor", "gl_renderer", "source_revision", "source_fingerprint"})
        {
            if (const auto *value = catalog->as_object().if_contains(key))
            {
                if (!value->is_string() || value->as_string().size() > 512)
                {
                    throw std::invalid_argument("invalid platform label");
                }
                result[key] = *value;
            }
        }
    }
    result["client_build_passport"] = nullptr;
    if (const auto *claim = report.if_contains("client_build_passport"))
    {
        result["client_build_passport"] = sv::verified_build_passport(*claim);
    }
    result["build_passport"] = nullptr;
    if (const auto *catalog = report.if_contains("catalog"); catalog && catalog->is_object())
    {
        if (const auto *claim = catalog->as_object().if_contains("build_passport"))
        {
            auto verified = sv::verified_build_passport(*claim);
            if (verified.is_object() && result.contains("source_fingerprint") &&
                verified.as_object()
                        .at("descriptor")
                        .as_object()
                        .at("source")
                        .as_object()
                        .at("fingerprint") == result.at("source_fingerprint"))
            {
                result["build_passport"] = std::move(verified);
            }
        }
    }
    result["observation_policy"] = verified_observation_policy(report);
    result["criteria"] = nullptr;
    result["criteria_source"] = "legacy_unverified";
    if (const auto *reference = report.if_contains("quality_reference");
        reference && reference->is_object())
    {
        if (const auto *criteria = reference->as_object().if_contains("criteria"))
        {
            result["criteria"] =
                quality_criteria_json(parse_quality_criteria(criteria->as_object()));
        }
        if (const auto *origin = reference->as_object().if_contains("criteria_source"))
        {
            if (!origin->is_string() || origin->as_string().size() > 128)
            {
                throw std::invalid_argument("invalid criteria source");
            }
            result["criteria_source"] = *origin;
        }
        for (const auto *key : {"kind", "dataset_id"})
        {
            if (const auto *value = reference->as_object().if_contains(key))
            {
                if (!value->is_string() || value->as_string().size() > 512)
                {
                    throw std::invalid_argument("invalid reference label");
                }
                result[std::string("reference_") + key] = *value;
            }
        }
    }
    return result;
}

boost::json::value truth(const boost::json::object &report)
{
    try
    {
        const auto &source = report.at("quality_reference").as_object();
        if (integer(source.at("schema_version"), 2) != 2 ||
            source.at("algorithm") != "masked_rgb8_v1" ||
            (source.at("kind") != "regression" && source.at("kind") != "independent_ground_truth"))
        {
            return nullptr;
        }
        boost::json::object value;
        for (const auto *key : {"schema_version", "algorithm", "kind", "width", "height", "view"})
        {
            value[key] = source.at(key);
        }
        boost::json::array frames;
        const auto &original = source.at("frames").as_array();
        if (original.empty() || original.size() > 256)
        {
            return nullptr;
        }
        for (const auto &entry : original)
        {
            boost::json::object frame;
            for (const auto *key :
                 {"inputs", "selected_pixels", "rgb_pixels_sha256", "mask_pixels_sha256"})
            {
                frame[key] = entry.as_object().at(key);
            }
            if (frame.at("rgb_pixels_sha256").as_string().size() != 64 ||
                frame.at("mask_pixels_sha256").as_string().size() != 64)
            {
                return nullptr;
            }
            const auto pixels = integer(frame.at("selected_pixels"), 16384U * 16384U);
            if (!pixels ||
                pixels > integer(source.at("width"), 16384) * integer(source.at("height"), 16384))
            {
                return nullptr;
            }
            for (const auto *key : {"rgb_pixels_sha256", "mask_pixels_sha256"})
            {
                for (char c : frame.at(key).as_string())
                {
                    if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f'))
                    {
                        return nullptr;
                    }
                }
            }
            frames.push_back(std::move(frame));
        }
        value["frames"] = std::move(frames);
        return value;
    }
    catch (const std::exception &)
    {
        return nullptr;
    }
}

bool truth_inputs(const boost::json::value &value, const Evidence &evidence)
{
    if (!value.is_object() ||
        value.as_object().at("frames").as_array().size() != evidence.frames.size())
    {
        return false;
    }
    for (size_t i = 0; i < evidence.frames.size(); ++i)
    {
        const auto &frame = evidence.frames[i].as_object();
        for (const auto *key : {"view", "width", "height"})
        {
            if (!equal(frame.at(key), value.as_object().at(key)))
            {
                return false;
            }
        }
        if (!equal(frame.at("inputs"),
                   value.as_object().at("frames").as_array()[i].as_object().at("inputs")))
        {
            return false;
        }
    }
    return true;
}

bool timing_policy(const boost::json::object &a, const boost::json::object &b, const Evidence &left,
                   const Evidence &right)
{
    for (const auto *key :
         {"frames", "warmup", "repeats", "seed", "reset_replay", "capture_frames"})
    {
        if (!a.at("scenario").as_object().contains(key) ||
            !b.at("scenario").as_object().contains(key) ||
            !equal(a.at("scenario").as_object().at(key), b.at("scenario").as_object().at(key)))
        {
            return false;
        }
    }
    if (!a.contains("percentile_definition") || !b.contains("percentile_definition") ||
        a.at("percentile_definition") !=
            "nearest rank; warmup excluded; per-frame timings, not FPS" ||
        a.at("percentile_definition") != b.at("percentile_definition"))
    {
        return false;
    }
    if (!a.contains("trial_order_algorithm") || !b.contains("trial_order_algorithm") ||
        a.at("trial_order_algorithm") != trial_order_algorithm ||
        a.at("trial_order_algorithm") != b.at("trial_order_algorithm"))
    {
        return false;
    }
    const auto &x = a.at("samples").as_array(), &y = b.at("samples").as_array();
    if (x.size() != y.size())
    {
        return false;
    }
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto &m = x[i].as_object(), &n = y[i].as_object();
        for (const auto *key : {"frame_index", "block", "warmup"})
        {
            if (!equal(m.at(key), n.at(key)))
            {
                return false;
            }
        }
        if (!equal(left.profiles[m.at("variant").to_number<unsigned>()],
                   right.profiles[n.at("variant").to_number<unsigned>()]))
        {
            return false;
        }
    }
    const auto pa = passport(a), pb = passport(b);
    for (const auto *key : {"backend", "gl_vendor", "gl_renderer", "source_fingerprint"})
    {
        if (!pa.contains(key) || !pb.contains(key) || pa.at(key).as_string().empty() ||
            pb.at(key).as_string().empty())
        {
            return false;
        }
    }
    const auto &clientA = pa.at("client_build_passport"), &clientB = pb.at("client_build_passport");
    return clientA.is_object() && clientB.is_object() &&
           clientA.as_object().at("sha256") == clientB.as_object().at("sha256") &&
           pa.at("build_passport").is_object() && pb.at("build_passport").is_object() &&
           pa.at("build_passport").as_object().at("sha256") ==
               pb.at("build_passport").as_object().at("sha256") &&
           pa.at("source_fingerprint").as_string().size() == 64 &&
           pb.at("source_fingerprint").as_string().size() == 64 &&
           pa.at("source_fingerprint") == pb.at("source_fingerprint");
}
} // namespace

boost::json::object compare_reports(const boost::json::object &a, const boost::json::object &b,
                                    const std::atomic_bool *cancel)
{
    cancelled(cancel);
    const auto left = evidence(a, cancel), right = evidence(b, cancel);
    const bool inputs = left.identity && right.identity && equal(left.frames, right.frames);
    const bool complete = left.complete && right.complete;
    const auto ta = truth(a), tb = truth(b);
    const bool quality =
        inputs && complete && truth_inputs(ta, left) && truth_inputs(tb, right) && equal(ta, tb);
    uint64_t quality_pixels = 0;
    if (quality)
    {
        for (const auto &frame : ta.as_object().at("frames").as_array())
        {
            quality_pixels += frame.as_object().at("selected_pixels").to_number<uint64_t>();
        }
    }
    const auto policyA = passport(a), policyB = passport(b);
    const auto oa = verified_observation_policy(a), ob = verified_observation_policy(b);
    const bool observations = oa.is_object() && ob.is_object() && equal(oa, ob) &&
                              (!oa.as_object().at("quality_evaluation").as_bool() ||
                               (quality && equal(policyA.at("criteria"), policyB.at("criteria"))));
    const bool timings = inputs && complete && observations && timing_policy(a, b, left, right);
    boost::json::array reasons;
    if (!equal(policyA.at("criteria"), policyB.at("criteria")) ||
        policyA.at("criteria_source") != policyB.at("criteria_source"))
    {
        reasons.push_back("acceptance_policy_differs");
    }
    if (!oa.is_object() || !ob.is_object())
    {
        reasons.push_back("observation_policy_missing_or_unverified");
    }
    else if (!equal(oa, ob))
    {
        reasons.push_back("observation_workload_differs");
    }
    else if (!observations)
    {
        reasons.push_back("quality_evaluation_reference_or_criteria_differ");
    }
    const auto &buildA = policyA.at("build_passport"), &buildB = policyB.at("build_passport");
    const bool builds = buildA.is_object() && buildB.is_object() &&
                        buildA.as_object().at("sha256") == buildB.as_object().at("sha256");
    if (!buildA.is_object() || !buildB.is_object())
    {
        reasons.push_back("build_passport_missing_or_unverified");
    }
    else if (!builds)
    {
        reasons.push_back("configured_build_differs");
    }
    const auto &clientA = policyA.at("client_build_passport"),
               &clientB = policyB.at("client_build_passport");
    const bool clientBuilds = clientA.is_object() && clientB.is_object() &&
                              clientA.as_object().at("sha256") == clientB.as_object().at("sha256");
    if (!clientA.is_object() || !clientB.is_object())
    {
        reasons.push_back("client_build_passport_missing_or_unverified");
    }
    else if (!clientBuilds)
    {
        reasons.push_back("client_configured_build_differs");
    }
    if (!complete)
    {
        reasons.push_back("one_or_both_runs_incomplete");
    }
    if (!left.identity)
    {
        reasons.push_back("left_content_or_settings_unverified");
    }
    if (!right.identity)
    {
        reasons.push_back("right_content_or_settings_unverified");
    }
    if (left.identity && right.identity && !inputs)
    {
        reasons.push_back("input_content_models_or_view_differ");
    }
    if (inputs && complete && !timings)
    {
        reasons.push_back("trial_policy_order_or_build_differs_or_missing");
    }
    if (!quality)
    {
        reasons.push_back("reference_masks_inputs_or_completeness_unverified");
    }
    boost::json::array rows;
    std::vector<bool> consumed(right.profiles.size());
    auto append = [&](size_t i, size_t j, const char *pairing)
    {
        const bool have_a = i < left.profiles.size(), have_b = j < right.profiles.size();
        boost::json::object row{
            {"pairing", pairing},
            {"left_index", have_a ? boost::json::value(i) : boost::json::value()},
            {"right_index", have_b ? boost::json::value(j) : boost::json::value()},
            {"settings", have_a ? left.profiles[i] : right.profiles[j]},
            {"left", have_a ? boost::json::value(compact(a, i)) : boost::json::value()},
            {"right", have_b ? boost::json::value(compact(b, j)) : boost::json::value()},
            {"timing_delta_ms", nullptr},
            {"mae_delta", nullptr},
            {"rmse_delta", nullptr}};
        if (have_a && have_b && std::string_view(pairing) == "matched")
        {
            const auto &x = row.at("left").as_object(), &y = row.at("right").as_object();
            const auto measured = a.at("scenario").as_object().at("frames").to_number<unsigned>() *
                                  a.at("scenario").as_object().at("repeats").to_number<unsigned>();
            if (timings && x.at("measured") == measured && y.at("measured") == measured &&
                x.at("render_p50_ms").is_number() && y.at("render_p50_ms").is_number())
            {
                row["timing_delta_ms"] = y.at("render_p50_ms").to_number<double>() -
                                         x.at("render_p50_ms").to_number<double>();
            }
            if (quality && x.at("quality_frames") == a.at("scenario").as_object().at("frames") &&
                x.at("quality_frames") == y.at("quality_frames") &&
                x.at("selected_pixels").is_number() &&
                x.at("selected_pixels").to_number<double>() == quality_pixels &&
                equal(x.at("selected_pixels"), y.at("selected_pixels")))
            {
                for (const auto *metric : {"mae", "rmse"})
                {
                    if (x.at(metric).is_number() && y.at(metric).is_number())
                    {
                        row[std::string(metric) + "_delta"] =
                            y.at(metric).to_number<double>() - x.at(metric).to_number<double>();
                    }
                }
            }
        }
        rows.push_back(std::move(row));
    };
    for (size_t i = 0; i < left.profiles.size(); ++i)
    {
        cancelled(cancel);
        size_t candidate = right.profiles.size(), matches = 0, duplicates = 0;
        for (const auto &p : left.profiles)
        {
            duplicates += equal(p, left.profiles[i]);
        }
        for (size_t j = 0; j < right.profiles.size(); ++j)
        {
            if (equal(left.profiles[i], right.profiles[j]))
            {
                ++matches;
                candidate = j;
            }
        }
        if (matches == 1 && duplicates == 1)
        {
            append(i, candidate, "matched");
            consumed[candidate] = true;
        }
        else
        {
            append(i, right.profiles.size(), matches ? "ambiguous" : "left_only");
        }
    }
    for (size_t j = 0; j < right.profiles.size(); ++j)
    {
        if (!consumed[j])
        {
            append(left.profiles.size(), j, "right_only");
        }
    }
    return {
        {"schema_version", 1},
        {"policy", "report_claims_comparison_v4"},
        {"build_gate",
         builds ? "matching_configured_build_claims" : "different_or_unverified_configured_build"},
        {"client_build_gate", clientBuilds ? "matching_configured_build_claims"
                                           : "different_or_unverified_configured_build"},
        {"observation_gate",
         observations ? "matching_observation_claims" : "different_or_unverified_observation"},
        {"inputs", inputs ? "matching_claims" : "different_or_unverified"},
        {"timing_gate", timings ? "matching_workload_claims" : "incomplete_or_unverified_workload"},
        {"quality_gate",
         quality ? "matching_reference_claims" : "incomplete_or_unverified_reference"},
        {"left", passport(a)},
        {"right", passport(b)},
        {"rows", std::move(rows)},
        {"reasons", std::move(reasons)},
        {"limitation", "Reported identities are claims, not attestation; deltas are B-A, "
                       "paused-frame observations, not a winner or sustained FPS."}};
}

boost::json::object compare_report_files(const std::filesystem::path &a,
                                         const std::filesystem::path &b,
                                         const std::atomic_bool *cancel)
{
    const auto left = read_regular_file(a, 16 * 1024 * 1024, cancel);
    const auto right = read_regular_file(b, 16 * 1024 * 1024, cancel);
    cancelled(cancel);
    auto result = compare_reports(sv::parse_json(left).as_object(),
                                  sv::parse_json(right).as_object(), cancel);
    result["left_sha256"] = sha256(left.data(), left.size());
    result["right_sha256"] = sha256(right.data(), right.size());
    return result;
}
} // namespace sv::research
