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
    std::filesystem::path source, report, captures;
    Scenario scenario;
    std::shared_ptr<const QualityReference> quality;
    std::unique_ptr<ReportOutput> output;
    std::string hash;
    bool attempted = false;
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
    impl_->hash = sha256(text.data(), text.size());
    impl_->scenario = parse_scenario(sv::parse_json(text));
    if (impl_->scenario.quality_reference)
    {
        impl_->quality = std::make_shared<QualityReference>(impl_->source.parent_path() /
                                                            *impl_->scenario.quality_reference);
        impl_->quality->validate(impl_->scenario);
    }
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
    if (impl_->report == impl_->source ||
        (std::filesystem::exists(impl_->report) &&
         std::filesystem::equivalent(impl_->source, impl_->report)))
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
    report["scenario_input_sha256"] = impl_->hash;
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
