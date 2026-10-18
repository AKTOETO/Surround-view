#pragma once
#include <boost/json.hpp>

namespace sv::research
{
// Observed settings are report claims, not hardware/build attestation.
boost::json::object observation_policy(bool capture, bool evaluate, bool progress = false);
// Missing, unknown, inconsistent or unstable policies remain readable but unverified.
boost::json::value verified_observation_policy(const boost::json::object &report);
} // namespace sv::research
