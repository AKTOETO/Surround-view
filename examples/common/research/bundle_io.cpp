#include "bundle_io.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <regex>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace sv::research::bundle_detail
{
namespace
{
struct Descriptor
{
    int fd;

    explicit Descriptor(int value) : fd(value)
    {
        if (fd < 0)
        {
            throw std::runtime_error("cannot open bundle file/directory");
        }
    }

    ~Descriptor()
    {
        if (fd >= 0)
        {
            ::close(fd);
        }
    }

    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};
} // namespace

void check_cancel(const std::atomic_bool *cancel)
{
    if (cancel && cancel->load())
    {
        throw std::runtime_error("cancelled");
    }
}

bool safe_file(std::string_view name)
{
    if (name == "scenario.json" || name == "reference.json")
    {
        return true;
    }
    static const std::regex asset("assets/frame-(0|[1-9][0-9]{0,2})-(rgb|mask)\\.ppm");
    return name.size() < 64 && std::regex_match(name.begin(), name.end(), asset);
}

bool digest_string(std::string_view value)
{
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

void write_new(const std::filesystem::path &path, std::string_view bytes,
               const std::atomic_bool *cancel)
{
    Descriptor file(
        ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    size_t offset = 0;
    while (offset < bytes.size())
    {
        check_cancel(cancel);
        const auto count =
            ::write(file.fd, bytes.data() + offset, std::min<size_t>(65536, bytes.size() - offset));
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            throw std::runtime_error("bundle write failed");
        }
        offset += size_t(count);
    }
    // close errors are checked as well as writes; no durability/fsync guarantee is made.
    const int fd = file.fd;
    file.fd = -1;
    if (::close(fd) != 0)
    {
        throw std::runtime_error("bundle close failed");
    }
}

namespace
{
std::string read_submember(const std::filesystem::path &root, int run, const std::string &name,
                           size_t limit, const std::atomic_bool *cancel)
{
    if (name != "bundle.json" && !safe_file(name))
    {
        throw std::invalid_argument("unsafe bundle member path");
    }
    auto directory_path = root;
    while (directory_path.filename().empty() && directory_path.parent_path() != directory_path)
    {
        directory_path = directory_path.parent_path();
    }
    Descriptor directory(
        ::open(directory_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    Descriptor run_directory(run >= 0
                                 ? ::openat(directory.fd, ("run-" + std::to_string(run)).c_str(),
                                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
                                 : ::dup(directory.fd));
    const bool asset = name.rfind("assets/", 0) == 0;
    Descriptor parent(asset ? ::openat(run_directory.fd, "assets",
                                       O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
                            : ::dup(run_directory.fd));
    const auto leaf = asset ? name.substr(7) : name;
    Descriptor file(
        ::openat(parent.fd, leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat status{};
    if (::fstat(file.fd, &status) || !S_ISREG(status.st_mode))
    {
        throw std::invalid_argument("bundle member must be a regular file");
    }
    std::string bytes;
    char chunk[65536];
    while (bytes.size() <= limit)
    {
        check_cancel(cancel);
        const auto count =
            ::read(file.fd, chunk, std::min(sizeof(chunk), limit + 1 - bytes.size()));
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count < 0)
        {
            throw std::runtime_error("bundle read failed");
        }
        if (!count)
        {
            break;
        }
        bytes.append(chunk, size_t(count));
    }
    if (bytes.empty() || bytes.size() > limit)
    {
        throw std::invalid_argument("bundle member is empty or exceeds limit");
    }
    return bytes;
}

} // namespace

std::string read_member(const std::filesystem::path &root, const std::string &name, size_t limit,
                        const std::atomic_bool *cancel)
{
    return read_submember(root, -1, name, limit, cancel);
}

std::string read_run_member(const std::filesystem::path &root, unsigned run,
                            const std::string &name, size_t limit, const std::atomic_bool *cancel)
{
    if (run > 7)
    {
        throw std::invalid_argument("invalid bundle run index");
    }
    return read_submember(root, static_cast<int>(run), name, limit, cancel);
}

TemporaryDirectory::TemporaryDirectory()
{
    auto pattern = (std::filesystem::temp_directory_path() / "sv-bundle-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back(0);
    const auto result = ::mkdtemp(writable.data());
    if (!result)
    {
        throw std::runtime_error("cannot create private bundle staging");
    }
    path = result;
}

TemporaryDirectory::~TemporaryDirectory()
{
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

void materialize(const Files &files, const std::filesystem::path &root,
                 const std::atomic_bool *cancel)
{
    if (files.size() > 1)
    {
        std::filesystem::create_directory(root / "assets");
    }
    for (const auto &entry : files)
    {
        if (!safe_file(entry.first))
        {
            throw std::invalid_argument("unsafe bundle member path");
        }
        write_new(root / entry.first, entry.second, cancel);
    }
}
} // namespace sv::research::bundle_detail
