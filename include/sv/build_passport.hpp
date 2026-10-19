#pragma once
#include <boost/json.hpp>

namespace sv
{
// Declared configuration and compiler/header facts. Not an executable/runtime attestation.
boost::json::object configured_build_passport();
// Shared construction/verification uses bounded, sorted-object JSON and a domain-prefixed hash.
boost::json::object make_build_passport(const boost::json::object &descriptor);
// Invalid/missing/future claims return null; old reports remain readable.
boost::json::value verified_build_passport(const boost::json::value &);
} // namespace sv
