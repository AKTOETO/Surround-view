#pragma once
#include "sv/frame.hpp"

namespace sv
{
inline constexpr const char *image_identity_algorithm = "packed_rgb8_sha256_v1";
inline constexpr const char *camera_identity_algorithm = "camera_model_sha256_v1";
// Domain-separated, dimension-bound packed top-left RGB8 bytes.
std::string rgb8_image_sha256(const Image &);
// Renderer camera fields, canonical big-endian IEEE754 doubles; -0 becomes +0.
std::string camera_model_sha256(const Camera &);
boost::json::object image_identity(const Image &, const std::string &sha256);
} // namespace sv
