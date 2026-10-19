#include "batch_bundle.hpp"
#include "batch_execution.hpp"
#include "batch_plan.hpp"
#include "bundle_io.hpp"
#include "digest.hpp"
#include "sv/json.hpp"
#include <set>
#include <stdexcept>

namespace sv::research
{
namespace
{
using namespace bundle_detail;
constexpr const char *kind = "batch_bundle_v1", *scope = "client_scenarios_and_references_only";
constexpr size_t json_limit = 1024 * 1024;

std::string run_name(size_t index)
{
    return "run-" + std::to_string(index);
}

boost::json::object plan_from_manifest(const boost::json::object &manifest)
{
    const std::set<std::string> keys{"schema_version", "kind", "scope", "source_plan_sha256",
                                     "runs"};
    if (manifest.size() != keys.size())
    {
        throw std::invalid_argument("invalid batch bundle fields");
    }
    for (const auto &field : manifest)
    {
        if (!keys.count(std::string(field.key())))
        {
            throw std::invalid_argument("unknown batch bundle field");
        }
    }
    const auto &version = manifest.at("schema_version");
    if ((!version.is_int64() && !version.is_uint64()) || version.to_number<double>() != 1 ||
        manifest.at("kind") != kind || manifest.at("scope") != scope ||
        !digest_string(std::string(manifest.at("source_plan_sha256").as_string())))
    {
        throw std::invalid_argument("invalid batch bundle schema/kind/scope/digest");
    }
    const auto &runs = manifest.at("runs").as_array();
    if (runs.empty() || runs.size() > 8)
    {
        throw std::invalid_argument("batch bundle requires 1..8 runs");
    }
    boost::json::array rows;
    for (size_t index = 0; index < runs.size(); ++index)
    {
        const auto &row = runs[index].as_object();
        if (row.size() != 3 || std::string(row.at("directory").as_string()) != run_name(index) ||
            !digest_string(std::string(row.at("manifest_sha256").as_string())))
        {
            throw std::invalid_argument("invalid batch bundle run descriptor");
        }
        rows.push_back(boost::json::object{{"id", row.at("id")},
                                           {"scenario", run_name(index) + "/scenario.json"}});
    }
    return parse_batch_plan(
        boost::json::serialize(boost::json::object{{"schema_version", 1}, {"runs", rows}}));
}

// Read only fixed paths. ScenarioBundle performs full descriptor/hash/closure/decoder validation
// on this private snapshot. Top-level digest binds the exact child manifest bytes.
size_t copy_run(const std::filesystem::path &source, size_t index,
                const std::filesystem::path &destination, const std::string &digest, size_t budget,
                const std::atomic_bool *cancel)
{
    const auto text =
        read_run_member(source, static_cast<unsigned>(index), "bundle.json", json_limit, cancel);
    if (sha256(text.data(), text.size()) != digest || text.size() > budget)
    {
        throw std::invalid_argument("batch child manifest hash/budget mismatch");
    }
    size_t bytes = text.size();
    const auto manifest = sv::parse_json(text).as_object();
    const auto &files = manifest.at("files").as_array();
    if (files.empty() || files.size() > 514)
    {
        throw std::invalid_argument("invalid batch child inventory");
    }
    std::filesystem::create_directory(destination);
    std::set<std::string> seen;
    for (const auto &value : files)
    {
        const std::string path(value.as_object().at("path").as_string());
        if (!safe_file(path) || !seen.insert(path).second)
        {
            throw std::invalid_argument("unsafe/duplicate batch child file");
        }
        const auto limit =
            std::min(budget - bytes,
                     path == "scenario.json" || path == "reference.json" ? json_limit : file_limit);
        if (!limit)
        {
            throw std::invalid_argument("batch bundle exceeds 64MiB raw budget");
        }
        const auto data =
            read_run_member(source, static_cast<unsigned>(index), path, limit, cancel);
        bytes += data.size();
        if (path.rfind("assets/", 0) == 0)
        {
            std::filesystem::create_directory(destination / "assets");
        }
        write_new(destination / path, data, cancel);
    }
    write_new(destination / "bundle.json", text, cancel);
    return bytes;
}
} // namespace

struct BatchBundle::Impl
{
    TemporaryDirectory staging;
    std::vector<std::unique_ptr<ScenarioBundle>> bundles;
    boost::json::object plan, receipt;
    bool attempted = false;
};

BatchBundle::BatchBundle(const std::filesystem::path &root, const std::atomic_bool *cancel)
    : impl_(std::make_unique<Impl>())
{
    check_cancel(cancel);
    if (root.empty() || root.string().find('\0') != std::string::npos)
    {
        throw std::invalid_argument("invalid batch bundle directory");
    }
    const auto text = read_member(root, "bundle.json", 65536, cancel);
    const auto manifest = sv::parse_json(text).as_object();
    impl_->plan = plan_from_manifest(manifest);
    size_t raw = 0, decoded = 0, trials = 0;
    boost::json::array receipts;
    const auto &runs = manifest.at("runs").as_array();
    for (size_t index = 0; index < runs.size(); ++index)
    {
        const auto &row = runs[index].as_object();
        const auto staged = impl_->staging.path / run_name(index);
        raw += copy_run(root, index, staged, std::string(row.at("manifest_sha256").as_string()),
                        total_limit - raw, cancel);
        auto bundle = std::make_unique<ScenarioBundle>(staged, cancel);
        auto receipt = bundle->summary();
        decoded += receipt.at("reference_decoded_bytes").to_number<size_t>();
        trials += receipt.at("trials").to_number<size_t>();
        if (!bundle->experiment().resets_replay() || decoded > 128 * 1024 * 1024 || trials > 10000)
        {
            throw std::invalid_argument(
                "batch requires replay reset, <=128MiB decoded references and <=10000 trials");
        }
        receipt["id"] = row.at("id");
        receipts.push_back(std::move(receipt));
        impl_->bundles.push_back(std::move(bundle));
    }
    impl_->receipt = {{"verified", true},
                      {"policy", kind},
                      {"scope", scope},
                      {"manifest_sha256", sha256(text.data(), text.size())},
                      {"source_plan_sha256", manifest.at("source_plan_sha256")},
                      {"server_recording_included", false},
                      {"runs", receipts},
                      {"trials", trials},
                      {"reference_decoded_bytes", decoded},
                      {"raw_bytes", raw}};
}

BatchBundle::~BatchBundle() = default;

boost::json::object BatchBundle::summary() const
{
    return impl_->receipt;
}

boost::json::object BatchBundle::execute(const std::filesystem::path &output,
                                         const client::Options &options,
                                         const std::atomic_bool *cancel,
                                         std::function<void(unsigned, unsigned, unsigned)> progress)
{
    check_cancel(cancel);
    if (impl_->attempted)
    {
        throw std::logic_error("batch bundle execute already attempted");
    }
    impl_->attempted = true;
    std::vector<FileExperiment *> experiments;
    for (const auto &bundle : impl_->bundles)
    {
        experiments.push_back(&bundle->experiment());
    }
    return execute_prepared_batch(boost::json::serialize(impl_->plan) + '\n', experiments, output,
                                  options, cancel, std::move(progress), false, impl_->receipt);
}

boost::json::object export_batch_bundle(const std::filesystem::path &path,
                                        const std::filesystem::path &output,
                                        const std::atomic_bool *cancel)
{
    check_cancel(cancel);
    if (path.empty() || output.empty() || path.string().find('\0') != std::string::npos ||
        output.string().find('\0') != std::string::npos)
    {
        throw std::invalid_argument("invalid batch bundle source/destination");
    }
    const auto text = read_batch_plan(path);
    const auto plan = parse_batch_plan(text);
    TemporaryDirectory staging;
    boost::json::array runs;
    size_t raw = 0;
    for (size_t index = 0; index < plan.at("runs").as_array().size(); ++index)
    {
        const auto &row = plan.at("runs").as_array()[index].as_object();
        const auto child =
            export_scenario_bundle(path.parent_path() / std::string(row.at("scenario").as_string()),
                                   staging.path / run_name(index), cancel);
        const auto child_text =
            read_member(staging.path / run_name(index), "bundle.json", json_limit, cancel);
        if (child_text.size() > total_limit - raw)
        {
            throw std::invalid_argument("batch bundle exceeds 64MiB raw budget");
        }
        raw += child_text.size();
        const auto child_manifest = sv::parse_json(child_text).as_object();
        for (const auto &file : child_manifest.at("files").as_array())
        {
            const auto bytes = file.as_object().at("bytes").to_number<size_t>();
            if (bytes > total_limit - raw)
            {
                throw std::invalid_argument("batch bundle exceeds 64MiB raw budget");
            }
            raw += bytes;
        }
        runs.push_back(boost::json::object{{"id", row.at("id")},
                                           {"directory", run_name(index)},
                                           {"manifest_sha256", child.at("manifest_sha256")}});
    }
    const boost::json::object manifest{{"schema_version", 1},
                                       {"kind", kind},
                                       {"scope", scope},
                                       {"source_plan_sha256", sha256(text.data(), text.size())},
                                       {"runs", runs}};
    const auto manifest_text = boost::json::serialize(manifest) + '\n';
    write_new(staging.path / "bundle.json", manifest_text, cancel);
    BatchBundle verified(staging.path, cancel); // aggregate preflight before destination exists
    check_cancel(cancel);
    if (!std::filesystem::create_directory(output))
    {
        throw std::runtime_error("batch bundle output directory must be new");
    }
    size_t bytes = 0;
    for (size_t index = 0; index < runs.size(); ++index)
    {
        bytes += copy_run(staging.path, index, output / run_name(index),
                          std::string(runs[index].as_object().at("manifest_sha256").as_string()),
                          total_limit - bytes, cancel);
    }
    write_new(output / "bundle.json", manifest_text,
              cancel); // publication marker last, not atomic/fsync
    return verified.summary();
}
} // namespace sv::research
