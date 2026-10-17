#include "batch_plan.hpp"
#include "sv/json.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace sv::research
{
namespace
{
void keys(const boost::json::object &object, std::initializer_list<std::string_view> allowed)
{
    for (const auto &entry : object)
    {
        if (std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end())
        {
            throw std::invalid_argument("unknown batch field: " + std::string(entry.key()));
        }
    }
}

bool safe_id(std::string_view text)
{
    return !text.empty() && text.size() <= 64 &&
           std::all_of(text.begin(), text.end(),
                       [](char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '-' || c == '_';
                       });
}

class Input
{
  public:

    explicit Input(const std::filesystem::path &path)
        : fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK))
    {
        if (fd < 0)
        {
            throw std::invalid_argument("cannot open batch plan");
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

boost::json::object parse_batch_plan(std::string_view text)
{
    if (text.empty() || text.size() > 64 * 1024)
    {
        throw std::invalid_argument("batch plan must contain 1..65536 bytes");
    }
    auto plan = sv::parse_json(text).as_object();
    keys(plan, {"schema_version", "runs"});
    const auto &version = plan.at("schema_version");
    if ((!version.is_int64() && !version.is_uint64()) || version.to_number<uint64_t>() != 1)
    {
        throw std::invalid_argument("batch schema_version must be integer 1");
    }
    const auto &runs = plan.at("runs").as_array();
    if (runs.empty() || runs.size() > 8)
    {
        throw std::invalid_argument("batch must have 1..8 runs");
    }
    std::set<std::string> ids;
    for (const auto &value : runs)
    {
        const auto &row = value.as_object();
        keys(row, {"id", "scenario"});
        const std::string id(row.at("id").as_string());
        const std::string path(row.at("scenario").as_string());
        if (!safe_id(id) || id == "batch" || !ids.insert(id).second || path.empty() ||
            path.size() > 4096 || path.find('\0') != std::string::npos)
        {
            throw std::invalid_argument(
                "batch requires unique safe IDs and nonempty scenario paths");
        }
    }
    return plan;
}

std::string read_batch_plan(const std::filesystem::path &path)
{
    Input input(path);
    struct stat info{};
    if (::fstat(input.fd, &info) != 0 || !S_ISREG(info.st_mode))
    {
        throw std::invalid_argument("batch plan must be a regular file");
    }
    std::string text(64 * 1024 + 1, '\0');
    size_t offset = 0;
    while (offset < text.size())
    {
        const auto count = ::read(input.fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count < 0)
        {
            throw std::invalid_argument("cannot read batch plan");
        }
        if (count == 0)
        {
            break;
        }
        offset += static_cast<size_t>(count);
    }
    text.resize(offset);
    if (text.empty() || text.size() > 64 * 1024)
    {
        throw std::invalid_argument("batch plan must contain 1..65536 bytes");
    }
    return text;
}
} // namespace sv::research
