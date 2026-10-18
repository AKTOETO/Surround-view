#include "regular_input.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace sv::research
{
namespace
{
class Input
{
  public:

    explicit Input(const std::filesystem::path &path)
        : fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK))
    {
        if (fd < 0)
        {
            throw std::invalid_argument("cannot open regular JSON input");
        }
    }

    ~Input()
    {
        ::close(fd);
    }

    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;
    const int fd;
};
} // namespace

std::string read_regular_file(const std::filesystem::path &path, size_t maximum_bytes,
                              const std::atomic_bool *cancel)
{
    if (!maximum_bytes || maximum_bytes > 16 * 1024 * 1024)
    {
        throw std::invalid_argument("invalid JSON input byte limit");
    }
    Input input(path);
    struct stat info{};
    if (::fstat(input.fd, &info) != 0 || !S_ISREG(info.st_mode))
    {
        throw std::invalid_argument("JSON input must be a regular file");
    }
    std::string text;
    text.reserve(std::min<size_t>(maximum_bytes, 64 * 1024));
    char chunk[64 * 1024];
    while (text.size() <= maximum_bytes)
    {
        if (cancel && cancel->load())
        {
            throw std::runtime_error("cancelled");
        }
        const auto count =
            ::read(input.fd, chunk, std::min(sizeof(chunk), maximum_bytes + 1 - text.size()));
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count < 0)
        {
            throw std::invalid_argument("cannot read regular JSON input");
        }
        if (count == 0)
        {
            break;
        }
        text.append(chunk, static_cast<size_t>(count));
    }
    if (text.empty() || text.size() > maximum_bytes)
    {
        throw std::invalid_argument("JSON input is empty or exceeds byte limit");
    }
    return text;
}
} // namespace sv::research
