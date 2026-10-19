#include "file_experiment.hpp"
#include "digest.hpp"
#include "quality_reference.hpp"
#include "report_output.hpp"
#include "sv/json.hpp"
#include "trial_plan.hpp"
#include <fstream>
#include <stdexcept>

namespace sv::research
{
struct FileExperiment::Impl
{
    std::filesystem::path source, base, report, captures;
    Scenario scenario;
    std::shared_ptr<const QualityReference> quality;
    std::unique_ptr<ReportOutput> output;
    std::string hash;
    bool attempted = false;
    boost::json::value bundle_identity;

    void load(std::string_view text, const std::filesystem::path &directory)
    {
        if (text.empty() || text.size() > 1024 * 1024)
        {
            throw std::invalid_argument("scenario must contain 1..1048576 bytes");
        }
        // Preserve symlink/.. semantics for explicitly supplied reference bases.
        base = std::filesystem::absolute(directory);
        hash = sha256(text.data(), text.size());
        scenario = parse_scenario(sv::parse_json(text));
        if (scenario.quality_reference)
        {
            quality = std::make_shared<QualityReference>(base / *scenario.quality_reference,
                                                         scenario.quality_criteria);
            quality->validate(scenario);
        }
    }
};

FileExperiment::FileExperiment(const std::filesystem::path &source)
    : impl_(std::make_unique<Impl>())
{
    impl_->source = std::filesystem::canonical(source);
    std::ifstream input(impl_->source, std::ios::binary);
    std::string text(1024 * 1024 + 1, '\0');
    input.read(text.data(), text.size());
    text.resize(static_cast<size_t>(input.gcount()));
    if (input.bad() || text.empty() || text.size() > 1024 * 1024)
    {
        throw std::invalid_argument("scenario must contain 1..1048576 bytes");
    }
    impl_->load(text, impl_->source.parent_path());
}

FileExperiment::FileExperiment(std::string_view text, const std::filesystem::path &base)
    : impl_(std::make_unique<Impl>())
{
    impl_->load(text, base);
}

FileExperiment::~FileExperiment() = default;

size_t FileExperiment::reference_bytes() const
{
    if (!impl_->quality)
    {
        return 0;
    }
    const auto description = impl_->quality->description();
    return description.at("width").to_number<size_t>() *
           description.at("height").to_number<size_t>() * 6 *
           description.at("frames").as_array().size();
}

size_t FileExperiment::trial_count() const
{
    return make_trial_plan(impl_->scenario).size();
}

boost::json::value FileExperiment::reference_description() const
{
    return impl_->quality ? boost::json::value(impl_->quality->description())
                          : boost::json::value{};
}

void FileExperiment::set_bundle_identity(const std::string &manifest, const std::string &source)
{
    if (impl_->output || impl_->attempted)
    {
        throw std::logic_error("bundle identity must precede output reservation");
    }
    impl_->bundle_identity = boost::json::object{{"policy", "scenario_bundle_v1"},
                                                 {"manifest_sha256", manifest},
                                                 {"source_scenario_sha256", source},
                                                 {"scope", "client_scenario_and_reference_only"}};
}

bool FileExperiment::resets_replay() const
{
    return impl_->scenario.reset_replay;
}

const std::string &FileExperiment::input_sha256() const
{
    return impl_->hash;
}

void FileExperiment::reserve(const std::filesystem::path &path)
{
    if (impl_->output || impl_->attempted)
    {
        throw std::logic_error("experiment output already reserved or attempted");
    }
    // Preserve the last path component: O_EXCL must reject even a dangling report symlink.
    impl_->report = std::filesystem::absolute(path).lexically_normal();
    if (!impl_->source.empty() && (impl_->report == impl_->source ||
                                   (std::filesystem::exists(impl_->report) &&
                                    std::filesystem::equivalent(impl_->source, impl_->report))))
    {
        throw std::invalid_argument("report must not overwrite scenario");
    }
    auto output = std::make_unique<ReportOutput>(impl_->report);
    if (impl_->scenario.capture_frames)
    {
        impl_->captures =
            impl_->report.parent_path() / (impl_->report.filename().string() + ".frames");
        if (!std::filesystem::create_directory(impl_->captures))
        {
            throw std::runtime_error("research capture directory already exists");
        }
    }
    impl_->output = std::move(output);
}

boost::json::object FileExperiment::execute(const client::Options &options,
                                            const std::atomic_bool *cancel,
                                            std::function<void(unsigned, unsigned)> progress)
{
    if (!impl_->output || impl_->attempted)
    {
        throw std::logic_error("experiment requires reserved output and a single attempt");
    }
    impl_->attempted = true;
    std::function<std::string(const Sample &)> capture;
    if (impl_->scenario.capture_frames)
    {
        capture = [directory = impl_->captures](const Sample &sample)
        {
            const auto name = "frame-" + std::to_string(sample.frame_index) + "-variant-" +
                              std::to_string(sample.variant) + "-block-" +
                              std::to_string(sample.block) + ".rgba";
            std::ofstream frame(directory / name, std::ios::binary);
            frame.write(reinterpret_cast<const char *>(sample.frame->payload.data()),
                        sample.frame->payload.size());
            frame.close();
            if (!frame)
            {
                throw std::runtime_error("research frame capture failed");
            }
            return (directory.filename() / name).generic_string();
        };
    }
    auto report = run(impl_->scenario, options, cancel, std::move(progress), std::move(capture),
                      impl_->quality);
    if (impl_->scenario.capture_frames)
    {
        report.at("observation_policy").as_object()["capture_consumer"] = "rgba8_files_v1";
    }
    if (impl_->quality && !report.contains("quality_reference"))
    {
        report["quality_reference"] = impl_->quality->description();
    }
    if (!impl_->bundle_identity.is_null())
    {
        report["input_bundle"] = impl_->bundle_identity;
    }
    report["scenario_input_sha256"] = impl_->hash;
    report["scenario_input_kind"] = !impl_->bundle_identity.is_null()
                                        ? "verified_bundle_snapshot_v1"
                                    : impl_->source.empty() ? "memory_json_bytes_v1"
                                                            : "file_bytes_v1";
    report["scenario_input_base_directory"] = impl_->base.string();
    if (!impl_->source.empty())
    {
        report["scenario_input_path"] = impl_->source.string();
    }
    impl_->output->write(report);
    return report;
}

std::string quality_status(const boost::json::object &report)
{
    const auto *acceptance = report.if_contains("quality_acceptance");
    return acceptance ? std::string(acceptance->as_object().at("status").as_string())
                      : "not_requested";
}

int research_exit_code(const boost::json::object &report)
{
    const auto status = quality_status(report);
    if (!report.at("success").as_bool() || !report.at("restored").as_bool() ||
        status == "incomplete")
    {
        return 4;
    }
    return status == "failed" ? 5 : 0;
}
} // namespace sv::research
