#include "options.hpp"
#include <cmath>
#include <stdexcept>

namespace sv::ctl
{
namespace
{
double number(const std::string &text)
{
    size_t used = 0;
    const double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value))
    {
        throw std::invalid_argument("expected a finite numeric value");
    }
    return value;
}

uint16_t port(const std::string &text)
{
    auto value = parse_decimal_u64(text);
    if (!value || value > 65535)
    {
        throw std::invalid_argument("port outside 1..65535");
    }
    return static_cast<uint16_t>(value);
}
} // namespace

Options parse(const std::vector<std::string> &args)
{
    Options options;
    size_t index = 0;
    bool endpoint_set = false;
    auto next = [&]() -> const std::string &
    {
        if (index >= args.size())
        {
            throw std::invalid_argument("missing argument");
        }
        return args[index++];
    };
    while (index < args.size() && args[index].rfind("--", 0) == 0)
    {
        const auto flag = next();
        if (flag == "--help")
        {
            options.help = true;
            return options;
        }
        if (flag == "--unix" || flag == "--tcp")
        {
            if (endpoint_set)
            {
                throw std::invalid_argument("specify exactly one endpoint");
            }
            endpoint_set = true;
            if (flag == "--unix")
            {
                options.endpoint.directory = next();
                if (options.endpoint.directory.empty())
                {
                    throw std::invalid_argument("empty Unix directory");
                }
            }
            else
            {
                options.endpoint.transport = client::Endpoint::Transport::Tcp;
                options.endpoint.host = next();
                if (options.endpoint.host.empty())
                {
                    throw std::invalid_argument("empty TCP host");
                }
                options.endpoint.control_port = port(next());
                options.endpoint.data_port = port(next());
            }
        }
        else if (flag == "--timeout-ms")
        {
            const auto value = parse_decimal_u64(next());
            if (value < 10 || value > 60000)
            {
                throw std::invalid_argument("timeout outside 10..60000 ms");
            }
            options.timeout_ms = static_cast<unsigned>(value);
        }
        else
        {
            throw std::invalid_argument("unknown option: " + flag);
        }
    }
    if (index == args.size())
    {
        return options;
    }
    options.request.operation = next();
    auto &operation = options.request.operation;
    auto &parameters = options.request.parameters;
    if (operation == "preset")
    {
        parameters["name"] = next();
    }
    else if (operation == "orbit")
    {
        parameters["azimuth_delta_rad"] = number(next());
        parameters["elevation_delta_rad"] = number(next());
    }
    else if (operation == "zoom")
    {
        parameters["distance_delta_m"] = number(next());
    }
    else if (operation == "calibration-status")
    {
        operation = "calibration_status";
        const auto &job_id = next();
        if (job_id.empty())
        {
            throw std::invalid_argument("empty job_id");
        }
        parameters["job_id"] = job_id;
    }
    else if (operation == "apply-calibration")
    {
        operation = "apply_calibration";
        const auto &job_id = next();
        if (job_id.empty())
        {
            throw std::invalid_argument("empty job_id");
        }
        parameters["job_id"] = job_id;
    }
    else if (operation == "cancel-calibration")
    {
        operation = "cancel_calibration";
        const auto &job_id = next();
        if (job_id.empty())
        {
            throw std::invalid_argument("empty job_id");
        }
        parameters["job_id"] = job_id;
    }
    else if (operation == "command")
    {
        operation = next();
        if (operation.empty())
        {
            throw std::invalid_argument("empty command");
        }
        if (index < args.size())
        {
            if (next() != "--params")
            {
                throw std::invalid_argument("expected --params JSON_OBJECT");
            }
            auto value = boost::json::parse(next());
            if (!value.is_object())
            {
                throw std::invalid_argument("parameters must be a JSON object");
            }
            parameters = std::move(value.as_object());
            for (const auto *key : {"command_id", "type", "session_id"})
            {
                if (parameters.contains(key))
                {
                    throw std::invalid_argument("reserved command parameter: " + std::string(key));
                }
            }
        }
    }
    else if (operation != "state" && operation != "pause" && operation != "resume" &&
             operation != "step")
    {
        throw std::invalid_argument("unknown operation; use command TYPE for a protocol extension");
    }
    if (index != args.size())
    {
        throw std::invalid_argument("unexpected trailing arguments");
    }
    return options;
}

std::string usage()
{
    return "svctl [--unix DIR | --tcp HOST CONTROL_PORT DATA_PORT] [--timeout-ms N]\n"
           "      state | pause | resume | step | preset NAME | orbit AZ_RAD EL_RAD | zoom METERS\n"
           "      calibration-status JOB_ID | cancel-calibration JOB_ID | apply-calibration "
           "JOB_ID\n"
           "      command TYPE [--params JSON_OBJECT]\n"
           "Default: Unix /tmp/sv-prototype, state. Output: one JSON ACK.\n"
           "Exit codes: 0 accepted, 2 arguments, 3 connection/timeout, 4 rejected.\n";
}
} // namespace sv::ctl
