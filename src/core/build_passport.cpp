#include "sv/build_passport.hpp"
#include "sv/build_configuration.hpp"
#include <algorithm>
#include <boost/version.hpp>
#include <glm/detail/setup.hpp>
#include <openssl/opensslv.h>
#include <openssl/sha.h>
#include <stdexcept>
#include <vector>

namespace sv
{
namespace
{
constexpr size_t max_bytes = 16384;
constexpr const char *policy = "configured_build_v1";

bool hex_digest(const boost::json::value &v)
{
    return v.is_string() && v.as_string().size() == 64 &&
           std::all_of(v.as_string().begin(), v.as_string().end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

boost::json::value sorted(const boost::json::value &v, unsigned depth, size_t &nodes)
{
    if (depth > 8 || ++nodes > 512)
    {
        throw std::invalid_argument("build passport structure limit");
    }
    if (v.is_string() && v.as_string().size() > 8192)
    {
        throw std::invalid_argument("build passport string limit");
    }
    if (v.is_object())
    {
        std::vector<std::string> keys;
        for (const auto &entry : v.as_object())
        {
            keys.emplace_back(entry.key());
        }
        std::sort(keys.begin(), keys.end());
        boost::json::object result;
        for (const auto &key : keys)
        {
            result[key] = sorted(v.as_object().at(key), depth + 1, nodes);
        }
        return result;
    }
    if (v.is_array())
    {
        boost::json::array result;
        for (const auto &entry : v.as_array())
        {
            result.push_back(sorted(entry, depth + 1, nodes));
        }
        return result;
    }
    return v;
}

void keys(const boost::json::object &object, std::initializer_list<const char *> names)
{
    if (object.size() != names.size())
    {
        throw std::invalid_argument("build passport fields");
    }
    for (const auto *name : names)
    {
        if (!object.contains(name))
        {
            throw std::invalid_argument("build passport missing field");
        }
    }
}

void strings(const boost::json::object &object, std::initializer_list<const char *> names,
             bool nonempty = false)
{
    for (const auto *name : names)
    {
        const auto &v = object.at(name);
        if (!v.is_string() || (nonempty && v.as_string().empty()))
        {
            throw std::invalid_argument("build passport string field");
        }
    }
}

long integer(const boost::json::value &v)
{
    if (!v.is_int64() && !v.is_uint64())
    {
        throw std::invalid_argument("build passport integer");
    }
    return v.to_number<long>();
}

void validate(const boost::json::object &d)
{
    keys(d, {"schema_version", "scope", "source", "toolchain", "target", "configuration",
             "dependencies", "features", "headers"});
    if (integer(d.at("schema_version")) != 1 || d.at("scope") != policy)
    {
        throw std::invalid_argument("build passport schema/scope");
    }
    const auto &source = d.at("source").as_object();
    keys(source, {"fingerprint", "revision", "version"});
    strings(source, {"revision", "version"}, true);
    if (!hex_digest(source.at("fingerprint")))
    {
        throw std::invalid_argument("build source fingerprint");
    }
    const auto &toolchain = d.at("toolchain").as_object();
    keys(toolchain, {"compiler_id", "compiler_version", "compiler_target", "cxx_standard",
                     "toolchain_sha256"});
    strings(toolchain, {"compiler_id", "compiler_version"}, true);
    strings(toolchain, {"compiler_target"});
    if (integer(toolchain.at("cxx_standard")) < 17 || integer(toolchain.at("cxx_standard")) > 26 ||
        (!toolchain.at("toolchain_sha256").is_null() &&
         !hex_digest(toolchain.at("toolchain_sha256"))))
    {
        throw std::invalid_argument("build toolchain fields");
    }
    const auto &target = d.at("target").as_object();
    keys(target, {"system", "system_version", "processor", "sysroot", "sdk_id"});
    strings(target, {"system", "processor", "sdk_id"}, true);
    strings(target, {"system_version", "sysroot"});
    const auto &configuration = d.at("configuration").as_object();
    keys(configuration,
         {"name", "generator", "cxx_flags", "executable_link_flags", "project_compile_options",
          "project_link_options", "interprocedural_optimization", "position_independent_code"});
    strings(configuration, {"name", "generator"}, true);
    strings(configuration, {"cxx_flags", "executable_link_flags", "interprocedural_optimization",
                            "position_independent_code"});
    for (const auto *name : {"project_compile_options", "project_link_options"})
    {
        const auto &options = configuration.at(name).as_array();
        if (options.size() > 32)
        {
            throw std::invalid_argument("build options count");
        }
        for (const auto &option : options)
        {
            if (!option.is_string())
            {
                throw std::invalid_argument("build option type");
            }
        }
    }
    const auto &dependencies = d.at("dependencies").as_object();
    keys(dependencies, {"opencv_configured_version", "opencv_provision", "glm_provision",
                        "egl_configured_version", "gles_configured_version"});
    strings(dependencies, {"opencv_configured_version", "opencv_provision", "glm_provision"}, true);
    strings(dependencies, {"egl_configured_version", "gles_configured_version"});
    const auto &features = d.at("features").as_object();
    keys(features, {"SV_GPU", "SV_SANITIZERS", "SV_AURORA", "SV_BUNDLE_OPENCV", "SV_OPENCV_FETCHED",
                    "CMAKE_CROSSCOMPILING"});
    for (const auto &feature : features)
    {
        if (!feature.value().is_bool())
        {
            throw std::invalid_argument("build feature type");
        }
    }
    const auto &headers = d.at("headers").as_object();
    keys(headers, {"compiler_version", "boost", "glm", "openssl", "stdlib", "stdlib_version",
                   "cxx11_abi", "pointer_bits", "cplusplus"});
    strings(headers, {"compiler_version", "boost", "openssl", "stdlib"}, true);
    if (integer(headers.at("cplusplus")) < 201703 || integer(headers.at("glm")) <= 0 ||
        integer(headers.at("stdlib_version")) < 0 || integer(headers.at("cxx11_abi")) < -1 ||
        integer(headers.at("cxx11_abi")) > 1 ||
        (integer(headers.at("pointer_bits")) != 32 && integer(headers.at("pointer_bits")) != 64))
    {
        throw std::invalid_argument("build header facts");
    }
}

std::string digest(const std::string &payload)
{
    static constexpr char domain[] = "sv-configured-build-v1\0";
    const auto input = std::string(domain, sizeof(domain) - 1) + payload;
    unsigned char bytes[SHA256_DIGEST_LENGTH];
    if (!SHA256(reinterpret_cast<const unsigned char *>(input.data()), input.size(), bytes))
    {
        throw std::runtime_error("build passport SHA256 failed");
    }
    std::string result;
    for (const auto byte : bytes)
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}
} // namespace

boost::json::object make_build_passport(const boost::json::object &descriptor)
{
    size_t nodes = 0;
    auto normalized = sorted(descriptor, 0, nodes).as_object();
    validate(normalized);
    const auto payload = boost::json::serialize(normalized);
    if (payload.size() > max_bytes)
    {
        throw std::invalid_argument("build passport exceeds16KiB");
    }
    return {{"schema_version", 1},
            {"policy", policy},
            {"sha256", digest(payload)},
            {"descriptor", normalized}};
}

boost::json::value verified_build_passport(const boost::json::value &value)
{
    try
    {
        const auto &envelope = value.as_object();
        keys(envelope, {"schema_version", "policy", "sha256", "descriptor"});
        if (integer(envelope.at("schema_version")) != 1 || envelope.at("policy") != policy ||
            !hex_digest(envelope.at("sha256")))
        {
            return nullptr;
        }
        auto expected = make_build_passport(envelope.at("descriptor").as_object());
        return expected.at("sha256") == envelope.at("sha256") ? boost::json::value(expected)
                                                              : boost::json::value(nullptr);
    }
    catch (const std::exception &)
    {
        return nullptr;
    }
}

boost::json::object configured_build_passport()
{
    static const auto value = []
    {
        auto descriptor = boost::json::parse(build_detail::configured_json).as_object();
        boost::json::object headers{{"compiler_version", __VERSION__},
                                    {"boost", BOOST_LIB_VERSION},
                                    {"glm", GLM_VERSION},
                                    {"openssl", OPENSSL_VERSION_TEXT},
                                    {"pointer_bits", sizeof(void *) * 8},
                                    {"cplusplus", __cplusplus},
                                    {"cxx11_abi", -1}};
#if defined(__GLIBCXX__)
        headers["stdlib"] = "libstdc++";
        headers["stdlib_version"] = __GLIBCXX__;
#elif defined(_LIBCPP_VERSION)
        headers["stdlib"] = "libc++";
        headers["stdlib_version"] = _LIBCPP_VERSION;
#else
        headers["stdlib"] = "unreported";
        headers["stdlib_version"] = 0;
#endif
#ifdef _GLIBCXX_USE_CXX11_ABI
        headers["cxx11_abi"] = _GLIBCXX_USE_CXX11_ABI;
#endif
        descriptor["headers"] = std::move(headers);
        return make_build_passport(descriptor);
    }();
    return value;
}
} // namespace sv
