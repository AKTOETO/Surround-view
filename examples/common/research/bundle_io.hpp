#pragma once
#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace sv::research::bundle_detail
{
constexpr size_t file_limit = 16 * 1024 * 1024, total_limit = 64 * 1024 * 1024;
using Files = std::map<std::string, std::string>;
void check_cancel(const std::atomic_bool *);
bool safe_file(std::string_view);
bool digest_string(std::string_view);
void write_new(const std::filesystem::path &, std::string_view, const std::atomic_bool *);
// Directory and leaf descriptors are opened with O_NOFOLLOW. Names use the fixed bundle layout.
std::string read_member(const std::filesystem::path &, const std::string &, size_t,
                        const std::atomic_bool *);

// Same fixed single-scenario layout inside run-0 .. run-7; every directory opened no-follow.
std::string read_run_member(const std::filesystem::path &, unsigned run, const std::string &,
                            size_t, const std::atomic_bool *);

struct TemporaryDirectory
{
    std::filesystem::path path;
    TemporaryDirectory();
    ~TemporaryDirectory();
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
};

void materialize(const Files &, const std::filesystem::path &, const std::atomic_bool *);
} // namespace sv::research::bundle_detail
