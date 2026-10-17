#pragma once
#include <boost/json.hpp>
#include <filesystem>

namespace sv::research
{
// Reserve an unused report path before contacting the server. A failed process may leave an
// empty/partial file; retry must use a new path. This is not a durable checkpoint service.
class ReportOutput
{
  public:

    explicit ReportOutput(const std::filesystem::path &);
    ~ReportOutput();
    ReportOutput(const ReportOutput &) = delete;
    ReportOutput &operator=(const ReportOutput &) = delete;
    void write(const boost::json::object &);

  private:

    int fd_ = -1;
};
} // namespace sv::research
