#include "report_output.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace sv::research
{
ReportOutput::ReportOutput(const std::filesystem::path &path)
{
    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd_ < 0)
    {
        throw std::runtime_error(std::string("cannot reserve new research report: ") +
                                 std::strerror(errno));
    }
}

ReportOutput::~ReportOutput()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

void ReportOutput::write(const boost::json::object &report)
{
    if (fd_ < 0)
    {
        throw std::logic_error("research report already closed");
    }
    const auto text = boost::json::serialize(report) + '\n';
    size_t offset = 0;
    while (offset < text.size())
    {
        const auto written = ::write(fd_, text.data() + offset, text.size() - offset);
        if (written < 0 && errno == EINTR)
        {
            continue;
        }
        if (written <= 0)
        {
            throw std::runtime_error("research report write failed");
        }
        offset += static_cast<size_t>(written);
    }
    const int descriptor = fd_;
    fd_ = -1;
    if (::close(descriptor) < 0)
    {
        throw std::runtime_error("research report close failed");
    }
}
} // namespace sv::research
