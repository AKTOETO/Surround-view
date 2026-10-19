#include "scenario_bundle.hpp"
#include "bundle_io.hpp"
#include "digest.hpp"
#include "regular_input.hpp"
#include "report_evidence.hpp"
#include "sv/json.hpp"
#include <set>
#include <stdexcept>

namespace sv::research
{
namespace
{
using namespace bundle_detail;
constexpr const char *kind = "scenario_bundle_v1", *scope = "client_scenario_and_reference_only";

std::string path_string(const boost::json::value &value)
{
    const std::string path(value.as_string());
    if (path.empty() || path.size() > 4096 || path.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("invalid bundle source reference path");
    }
    return path;
}

void budget(const Files &files)
{
    size_t bytes = 0;
    if (files.empty() || files.size() > 514)
    {
        throw std::invalid_argument("bundle file count outside 1..514");
    }
    for (const auto &file : files)
    {
        if (file.second.empty() || file.second.size() > file_limit ||
            file.second.size() > total_limit - bytes)
        {
            throw std::invalid_argument("bundle exceeds per-file16MiB or total64MiB budget");
        }
        bytes += file.second.size();
    }
}

void closure(const Files &files)
{
    const auto scenario = sv::parse_json(files.at("scenario.json")).as_object();
    (void)parse_scenario(scenario);
    std::set<std::string> expected{"scenario.json"};
    if (const auto *reference = scenario.if_contains("quality_reference"))
    {
        if (*reference != "reference.json")
        {
            throw std::invalid_argument("bundle reference must be reference.json");
        }
        expected.insert("reference.json");
        const auto manifest = sv::parse_json(files.at("reference.json")).as_object();
        const auto &frames = manifest.at("frames").as_array();
        if (frames.empty() || frames.size() > 256)
        {
            throw std::invalid_argument("bundle reference frame count outside 1..256");
        }
        for (const auto &frame : frames)
        {
            for (const auto *role : {"rgb", "mask"})
            {
                const auto path = path_string(frame.as_object().at(role));
                if (path.rfind("assets/", 0) != 0 || !safe_file(path))
                {
                    throw std::invalid_argument("unsafe bundle reference asset path");
                }
                expected.insert(path);
            }
        }
    }
    if (expected.size() != files.size())
    {
        throw std::invalid_argument("bundle has unreferenced or missing members");
    }
    for (const auto &path : expected)
    {
        if (!files.count(path))
        {
            throw std::invalid_argument("bundle references an unlisted member");
        }
    }
}

boost::json::object build_manifest(const Files &files, const std::string &source,
                                   const boost::json::value &reference)
{
    boost::json::array members;
    for (const auto &file : files)
    {
        members.push_back(
            boost::json::object{{"path", file.first},
                                {"bytes", file.second.size()},
                                {"sha256", sha256(file.second.data(), file.second.size())}});
    }
    return {{"schema_version", 1},
            {"kind", kind},
            {"scope", scope},
            {"source_scenario_sha256", source},
            {"files", std::move(members)},
            {"reference_description", reference}};
}

boost::json::object receipt(const boost::json::object &manifest, const std::string &digest)
{
    return {{"verified", true},
            {"scope", scope},
            {"manifest_sha256", digest},
            {"source_scenario_sha256", manifest.at("source_scenario_sha256")},
            {"files", manifest.at("files").as_array().size()},
            {"server_recording_included", false}};
}
} // namespace

boost::json::object export_scenario_bundle(const std::filesystem::path &scenario_path,
                                           const std::filesystem::path &destination,
                                           const std::atomic_bool *cancel)
{
    check_cancel(cancel);
    if (scenario_path.empty() || destination.empty() ||
        scenario_path.string().find('\0') != std::string::npos ||
        destination.string().find('\0') != std::string::npos)
    {
        throw std::invalid_argument("invalid bundle source/destination path");
    }
    const auto source_path = std::filesystem::canonical(scenario_path);
    const auto text = read_regular_file(source_path, 1024 * 1024, cancel);
    return export_scenario_bundle(text, source_path.parent_path(), destination, cancel);
}

boost::json::object export_scenario_bundle(std::string_view text,
                                           const std::filesystem::path &reference_base,
                                           const std::filesystem::path &destination,
                                           const std::atomic_bool *cancel)
{
    check_cancel(cancel);
    if (text.empty() || text.size() > 1024 * 1024 || reference_base.empty() ||
        destination.empty() || reference_base.string().find('\0') != std::string::npos ||
        destination.string().find('\0') != std::string::npos)
    {
        throw std::invalid_argument("invalid memory scenario/base/destination or JSON exceeds1MiB");
    }
    auto scenario = sv::parse_json(text).as_object();
    const auto parsed = parse_scenario(scenario);
    Files files;
    if (parsed.quality_reference)
    {
        const auto reference_path = reference_base / *parsed.quality_reference;
        auto reference =
            sv::parse_json(read_regular_file(reference_path, 1024 * 1024, cancel)).as_object();
        auto &frames = reference.at("frames").as_array();
        if (frames.empty() || frames.size() > 256)
        {
            throw std::invalid_argument("bundle reference frame count outside 1..256");
        }
        for (size_t index = 0; index < frames.size(); ++index)
        {
            for (const auto *role : {"rgb", "mask"})
            {
                auto &path = frames[index].as_object().at(role);
                const auto name = "assets/frame-" + std::to_string(index) + "-" + role + ".ppm";
                files[name] = read_regular_file(reference_path.parent_path() / path_string(path),
                                                file_limit, cancel);
                path = name;
                budget(files);
            }
        }
        files["reference.json"] = boost::json::serialize(reference) + '\n';
        scenario["quality_reference"] = "reference.json";
    }
    files["scenario.json"] = boost::json::serialize(scenario) + '\n';
    budget(files);
    closure(files);
    TemporaryDirectory staging;
    materialize(files, staging.path, cancel);
    FileExperiment frozen(files.at("scenario.json"), staging.path);
    const auto manifest =
        build_manifest(files, sha256(text.data(), text.size()), frozen.reference_description());
    const auto manifest_text = boost::json::serialize(manifest) + '\n';
    if (manifest_text.size() > 1024 * 1024)
    {
        throw std::invalid_argument("bundle manifest exceeds1MiB");
    }
    check_cancel(cancel);
    if (!std::filesystem::create_directory(destination))
    {
        throw std::invalid_argument("bundle destination must be new");
    }
    // The manifest is the completion marker. Partial new output may remain on failure.
    materialize(files, destination, cancel);
    write_new(destination / "bundle.json", manifest_text, cancel);
    return receipt(manifest, sha256(manifest_text.data(), manifest_text.size()));
}

struct ScenarioBundle::Impl
{
    TemporaryDirectory staging;
    boost::json::object manifest;
    std::string digest;
    std::unique_ptr<FileExperiment> experiment;
};

ScenarioBundle::ScenarioBundle(const std::filesystem::path &root, const std::atomic_bool *cancel)
    : impl_(std::make_unique<Impl>())
{
    check_cancel(cancel);
    if (root.empty() || root.string().find('\0') != std::string::npos)
    {
        throw std::invalid_argument("invalid bundle directory path");
    }
    const auto text = read_member(root, "bundle.json", 1024 * 1024, cancel);
    auto manifest = sv::parse_json(text).as_object();
    const std::set<std::string> keys{"schema_version",         "kind",  "scope",
                                     "source_scenario_sha256", "files", "reference_description"};
    if (manifest.size() != keys.size())
    {
        throw std::invalid_argument("invalid bundle manifest fields");
    }
    for (const auto &field : manifest)
    {
        if (!keys.count(std::string(field.key())))
        {
            throw std::invalid_argument("unknown bundle field");
        }
    }
    const auto &version = manifest.at("schema_version");
    if ((!version.is_int64() && !version.is_uint64()) || version.to_number<double>() != 1 ||
        manifest.at("kind") != kind || manifest.at("scope") != scope ||
        !digest_string(std::string(manifest.at("source_scenario_sha256").as_string())))
    {
        throw std::invalid_argument("unknown bundle schema/kind/scope or source digest");
    }
    Files files;
    const auto &members = manifest.at("files").as_array();
    if (members.empty() || members.size() > 514)
    {
        throw std::invalid_argument("bundle file count outside 1..514");
    }
    size_t total = 0;
    for (const auto &value : members)
    {
        check_cancel(cancel);
        const auto &member = value.as_object();
        if (member.size() != 3)
        {
            throw std::invalid_argument("invalid bundle member fields");
        }
        const auto name = path_string(member.at("path"));
        const auto &size = member.at("bytes");
        const std::string digest(member.at("sha256").as_string());
        if (!safe_file(name) || files.count(name) || !digest_string(digest) ||
            (!size.is_int64() && !size.is_uint64()) || size.to_number<double>() <= 0 ||
            size.to_number<double>() >
                (name == "scenario.json" || name == "reference.json" ? 1024 * 1024 : file_limit) ||
            size.to_number<size_t>() > total_limit - total)
        {
            throw std::invalid_argument("unsafe/duplicate/oversized bundle member");
        }
        auto bytes = read_member(root, name, size.to_number<size_t>(), cancel);
        if (bytes.size() != size.to_number<size_t>() ||
            sha256(bytes.data(), bytes.size()) != digest)
        {
            throw std::invalid_argument("bundle member size/hash mismatch: " + name);
        }
        total += bytes.size();
        files.emplace(name, std::move(bytes));
    }
    budget(files);
    closure(files);
    materialize(files, impl_->staging.path, cancel);
    auto experiment =
        std::make_unique<FileExperiment>(files.at("scenario.json"), impl_->staging.path);
    if (!comparison::equal(experiment->reference_description(),
                           manifest.at("reference_description")))
    {
        throw std::invalid_argument("bundle frozen reference description mismatch");
    }
    impl_->digest = sha256(text.data(), text.size());
    experiment->set_bundle_identity(impl_->digest,
                                    std::string(manifest.at("source_scenario_sha256").as_string()));
    impl_->experiment = std::move(experiment);
    impl_->manifest = std::move(manifest);
}

ScenarioBundle::~ScenarioBundle() = default;

FileExperiment &ScenarioBundle::experiment()
{
    return *impl_->experiment;
}

boost::json::object ScenarioBundle::summary() const
{
    auto value = receipt(impl_->manifest, impl_->digest);
    value["trials"] = impl_->experiment->trial_count();
    value["reference_decoded_bytes"] = impl_->experiment->reference_bytes();
    return value;
}

boost::json::object ScenarioBundle::execute(const std::filesystem::path &report,
                                            const client::Options &options,
                                            const std::atomic_bool *cancel,
                                            std::function<void(unsigned, unsigned)> progress)
{
    check_cancel(cancel);
    impl_->experiment->reserve(report);
    return impl_->experiment->execute(options, cancel, std::move(progress));
}
} // namespace sv::research
