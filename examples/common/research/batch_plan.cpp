#include "batch_plan.hpp"
#include "digest.hpp"
#include "regular_input.hpp"
#include "report_output.hpp"
#include "sv/json.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

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
    return read_regular_file(path, 64 * 1024);
}

std::string write_batch_plan(const boost::json::object &plan, const std::filesystem::path &file)
{
    const auto text = boost::json::serialize(plan) + '\n';
    parse_batch_plan(text);
    const auto digest = sha256(text.data(), text.size());
    ReportOutput output(file);
    output.write(plan);
    return digest;
}
} // namespace sv::research
