#include "sv/input_identity.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <openssl/evp.h>
#include <stdexcept>

namespace sv
{
namespace
{
class Digest
{
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(),
                                                                     EVP_MD_CTX_free};

  public:

    Digest()
    {
        if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1)
        {
            throw std::runtime_error("input identity SHA256 initialization failed");
        }
    }

    void bytes(const void *data, size_t size)
    {
        if (EVP_DigestUpdate(context_.get(), data, size) != 1)
        {
            throw std::runtime_error("input identity SHA256 update failed");
        }
    }

    void integer(uint64_t value, size_t count)
    {
        unsigned char buffer[8];
        for (size_t i = 0; i < count; ++i)
        {
            buffer[count - i - 1] = static_cast<unsigned char>(value >> (8 * i));
        }
        bytes(buffer, count);
    }

    void real(double value)
    {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        if (!std::isfinite(value))
        {
            throw std::invalid_argument("input identity camera model must be finite");
        }
        if (value == 0)
        {
            value = 0.0;
        }
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        integer(bits, 8);
    }

    std::string finish()
    {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned size = 0;
        if (EVP_DigestFinal_ex(context_.get(), digest, &size) != 1 || size != 32)
        {
            throw std::runtime_error("input identity SHA256 finish failed");
        }
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        for (unsigned i = 0; i < size; ++i)
        {
            result += digits[digest[i] >> 4];
            result += digits[digest[i] & 15];
        }
        return result;
    }
};
} // namespace

std::string rgb8_image_sha256(const Image &image)
{
    if (image.width < 2 || image.width > 4096 || image.height < 2 || image.height > 2160 ||
        image.channels != 3 || image.pixels.size() != size_t(image.width) * image.height * 3)
    {
        throw std::invalid_argument("input identity requires packed RGB8 dimensions/buffer");
    }
    Digest digest;
    constexpr char domain[] = "sv-packed-rgb8-v1";
    digest.bytes(domain, sizeof(domain));
    digest.integer(image.width, 4);
    digest.integer(image.height, 4);
    digest.bytes(image.pixels.data(), image.pixels.size());
    return digest.finish();
}

std::string camera_model_sha256(const Camera &camera)
{
    if (camera.id < 0 || camera.id > 3 || camera.width < 2 || camera.height < 2)
    {
        throw std::invalid_argument("input identity camera dimensions/id");
    }
    Digest digest;
    constexpr char domain[] = "sv-camera-model-v1";
    digest.bytes(domain, sizeof(domain));
    digest.integer(camera.id, 4);
    digest.integer(camera.width, 4);
    digest.integer(camera.height, 4);
    for (const auto value :
         {camera.fx, camera.fy, camera.cx, camera.cy, camera.theta_max, camera.z_epsilon})
    {
        digest.real(value);
    }
    for (const auto value : camera.k)
    {
        digest.real(value);
    }
    for (const auto value : camera.T)
    {
        digest.real(value);
    }
    return digest.finish();
}

boost::json::object image_identity(const Image &image, const std::string &hash)
{
    return {{"algorithm", image_identity_algorithm},
            {"sha256", hash},
            {"width", image.width},
            {"height", image.height},
            {"pixel_format", "RGB8"},
            {"row_origin", "top_left"}};
}
} // namespace sv
