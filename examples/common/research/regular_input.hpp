#pragma once
#include <atomic>
#include <filesystem>
#include <string>

namespace sv::research
{
std::string read_regular_file(const std::filesystem::path &, size_t maximum_bytes,
                              const std::atomic_bool *cancel = nullptr);
} // namespace sv::research
