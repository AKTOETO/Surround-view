#pragma once
#include "quality_criteria.hpp"
#include "scenario.hpp"
#include <filesystem>
#include <memory>

namespace sv::research
{
// Client-side external RGB reference. Never sent to the algorithm under test.
class QualityReference
{
  public:

    explicit QualityReference(const std::filesystem::path &manifest,
                              std::optional<QualityCriteria> criteriaOverride = {});
    ~QualityReference();
    QualityReference(const QualityReference &) = delete;
    QualityReference &operator=(const QualityReference &) = delete;

    bool requires_content_identity() const;
    const std::optional<QualityCriteria> &criteria() const;
    void validate(const Scenario &) const;
    boost::json::object description() const;
    boost::json::object evaluate(const Sample &) const;

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sv::research
