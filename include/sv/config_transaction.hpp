#pragma once
#include "sv/config.hpp"
#include <functional>
#include <memory>

namespace sv
{
// Preparation may fail; publishing prepared state must not fail.
class PreparedConfiguration
{
  public:

    virtual ~PreparedConfiguration() = default;
    virtual void publish() noexcept = 0;
};

using PrepareConfiguration = std::function<std::unique_ptr<PreparedConfiguration>(const Config &)>;
} // namespace sv
