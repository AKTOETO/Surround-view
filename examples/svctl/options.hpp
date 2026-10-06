#pragma once
#include "sv/client.hpp"
#include <vector>

namespace sv::ctl
{
struct Request
{
    std::string operation = "state";
    boost::json::object parameters;
};

struct Options
{
    client::Endpoint endpoint;
    Request request;
    unsigned timeout_ms = 5000;
    bool help = false;
};

Options parse(const std::vector<std::string> &arguments);
std::string usage();
int execute(const Options &);
} // namespace sv::ctl
