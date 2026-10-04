#include "sv/report.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <memory>
#include <numeric>
#include <openssl/evp.h>
#include <sstream>
#include <sys/resource.h>
#include <sys/utsname.h>

namespace sv
{
namespace
{
std::string hex(const unsigned char *data, size_t size)
{
    std::ostringstream out;
    for (size_t i = 0; i < size; ++i)
    {
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(data[i]);
    }
    return out.str();
}

std::string read_text(const char *path)
{
    std::ifstream input(path);
    return {(std::istreambuf_iterator<char>(input)), {}};
}
} // namespace

std::string sha256(std::string_view bytes)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr) != 1)
    {
        throw std::runtime_error("SHA-256 failed");
    }
    return hex(digest, size);
}

std::string file_sha256(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("hash: cannot open " + path.string());
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                    EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
    {
        throw std::runtime_error("SHA-256 initialization failed");
    }
    std::array<char, 65536> buffer;
    while (input)
    {
        input.read(buffer.data(), buffer.size());
        if (EVP_DigestUpdate(context.get(), buffer.data(), input.gcount()) != 1)
        {
            throw std::runtime_error("SHA-256 update failed");
        }
    }
    if (!input.eof())
    {
        throw std::runtime_error("hash: truncated read");
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest, &size) != 1)
    {
        throw std::runtime_error("SHA-256 finalization failed");
    }
    return hex(digest, size);
}

boost::json::object distribution(const std::vector<double> &values)
{
    if (values.empty())
    {
        throw std::invalid_argument("empty distribution");
    }
    auto ordered = values;
    std::sort(ordered.begin(), ordered.end());
    const auto quantile = [&](double p)
    {
        double index = p * (ordered.size() - 1);
        size_t low = static_cast<size_t>(index);
        return ordered[low] +
               (ordered[std::min(low + 1, ordered.size() - 1)] - ordered[low]) * (index - low);
    };
    double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double variance = 0;
    for (double v : values)
    {
        variance += (v - mean) * (v - mean);
    }
    return {{"count", values.size()}, {"min", ordered.front()},
            {"mean", mean},           {"p50", quantile(.5)},
            {"p95", quantile(.95)},   {"p99", quantile(.99)},
            {"max", ordered.back()},  {"population_stddev", std::sqrt(variance / values.size())}};
}

boost::json::object system_info()
{
    const auto timestamp = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&timestamp, &utc);
    std::ostringstream time;
    time << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    utsname info{};
    uname(&info);
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const auto cpu = read_text("/proc/cpuinfo");
    std::istringstream lines(cpu);
    std::string line, model = "unknown";
    while (std::getline(lines, line))
    {
        if (line.rfind("model name", 0) == 0 || line.rfind("Hardware", 0) == 0)
        {
            auto colon = line.find(':');
            if (colon != std::string::npos)
            {
                model = line.substr(colon + 1);
            }
            break;
        }
    }
    return {{"recorded_at_utc", time.str()},
            {"sysname", info.sysname},
            {"kernel", info.release},
            {"architecture", info.machine},
            {"cpu", model},
            {"os_release", read_text("/etc/os-release")},
            {"memory_info", read_text("/proc/meminfo")},
            {"process_status", read_text("/proc/self/status")},
            {"peak_rss_mib", usage.ru_maxrss / 1024.0},
            {"rss_scope", "Linux process CPU resident memory; excludes dedicated GPU memory"}};
}

void write_platform_report(const std::filesystem::path &directory,
                           const boost::json::object &report)
{
    std::filesystem::create_directories(directory);
    write_json(directory / "report.json", report);
    std::ofstream out(directory / "REPORT.md");
    out << "# Отчёт проверки платформы\n\n"
        << "Профиль: `" << report.at("suite_id").as_string() << "`. Состояние: **"
        << report.at("status").as_string() << "**.\n\n"
        << "Времена относятся к render/readback, включая CPU и финальное чтение. "
        << "Программная и физическая задержка дисплея не измерены. "
        << "Каждый повтор использует новый контекст в том же процессе.\n\n"
        << "| Критерий | Результат | Детали |\n|---|---|---|\n";
    for (const auto &criterion : report.at("criteria").as_array())
    {
        const auto &c = criterion.as_object();
        auto detail = std::string(c.at("detail").as_string());
        std::replace(detail.begin(), detail.end(), '|', '/');
        std::replace(detail.begin(), detail.end(), '\n', ' ');
        out << "| " << c.at("id").as_string() << " | " << c.at("status").as_string() << " | "
            << detail << " |\n";
    }
    out << "\n| Вариант | Повтор | p50, мс | p95, мс | p99, мс | Максимум, мс "
           "|\n|---|---:|---:|---:|---:|---:|\n";
    for (const auto &value : report.at("render").as_array())
    {
        const auto &r = value.as_object();
        const auto &stats = r.at("render_readback_ms").as_object();
        out << "| " << r.at("variant").as_string() << " | " << r.at("repeat").as_int64() << " | "
            << stats.at("p50") << " | " << stats.at("p95") << " | " << stats.at("p99") << " | "
            << stats.at("max") << " |\n";
    }
    out << "\n## Первичные данные\n\n<!-- sv-platform-report:v1 -->\n```json\n"
        << boost::json::serialize(report) << "\n```\n";
    if (!out)
    {
        throw std::runtime_error("cannot write platform Markdown report");
    }
}
} // namespace sv
