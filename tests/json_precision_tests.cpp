#include "sv/config_store.hpp"
#include "sv/protocol.hpp"
#include <boost/version.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace
{
uint64_t bits(double value)
{
    uint64_t result;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

struct TemporaryConfig
{
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("sv-json-precision-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "config.json";

    ~TemporaryConfig()
    {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
} // namespace

TEST(JsonPrecision, WireRoundTripPreservesDoubleBitsAndIntegerKinds)
{
#if BOOST_VERSION < 108300
    GTEST_SKIP() << "Precise Boost.JSON number mode requires Boost >= 1.83";
#endif
    std::vector<double> values{std::nextafter(.1, 1.),
                               .000001 * 23,
                               2.1892304845413264,
                               -1.391858428704209,
                               std::numeric_limits<double>::denorm_min(),
                               std::numeric_limits<double>::min(),
                               std::numeric_limits<double>::max(),
                               -0.0};
    uint64_t seed = 0x123456789abcdef0;
    for (unsigned i = 0; i < 512; ++i)
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        const uint64_t representation =
            (seed & 0x800fffffffffffffULL) | (uint64_t(1000 + i % 48) << 52);
        double value;
        std::memcpy(&value, &representation, sizeof(value));
        values.push_back(value);
    }
    boost::json::array numbers;
    for (const auto value : values)
    {
        numbers.push_back(value);
    }
    sv::Message message{
        21, {{"numbers", numbers}, {"integer", INT64_MIN}, {"unsigned", UINT64_MAX}}, {1, 2, 3}};
    for (unsigned cycle = 0; cycle < 10; ++cycle)
    {
        const auto bytes = sv::encode(message);
        sv::Decoder decoder;
        EXPECT_TRUE(decoder.feed(bytes.data(), 23).empty());
        auto decoded = decoder.feed(bytes.data() + 23, bytes.size() - 23);
        ASSERT_EQ(decoded.size(), 1U);
        message = std::move(decoded[0]);
        EXPECT_EQ(sv::encode(message), bytes);
        const auto &actual = message.header.at("numbers").as_array();
        ASSERT_EQ(actual.size(), values.size());
        for (size_t i = 0; i < values.size(); ++i)
        {
            ASSERT_EQ(bits(actual[i].as_double()), bits(values[i]))
                << "cycle=" << cycle << " value=" << i;
        }
        EXPECT_TRUE(message.header.at("integer").is_int64());
        EXPECT_EQ(message.header.at("integer").as_int64(), INT64_MIN);
        EXPECT_TRUE(message.header.at("unsigned").is_uint64());
        EXPECT_EQ(message.header.at("unsigned").as_uint64(), UINT64_MAX);
        EXPECT_EQ(message.payload, (std::vector<unsigned char>{1, 2, 3}));
    }
}

TEST(JsonPrecision, ConfigurationLoadWirePersistRestartDoesNotDriftCalibration)
{
#if BOOST_VERSION < 108300
    GTEST_SKIP() << "Precise Boost.JSON number mode requires Boost >= 1.83";
#endif
    TemporaryConfig file;
    auto effective = sv::load_config(SV_TEST_CONFIG_PATH).effective;
    auto &camera = effective.as_object().at("cameras").as_array()[0].as_object();
    camera.at("projection").as_object()["fx"] = std::nextafter(.1, 1.);
    camera.at("projection").as_object()["cx"] = .000001 * 23;
    camera.at("T_camera_from_vehicle").as_array()[1].as_array()[3] = []
    {
        const uint64_t representation = 13726986393446128461ULL;
        double value;
        std::memcpy(&value, &representation, sizeof(value));
        return value;
    }();
    const auto original = sv::parse_config(effective);
    sv::write_json(file.path, effective);
    for (unsigned cycle = 0; cycle < 10; ++cycle)
    {
        auto loaded = sv::load_config(file.path);
        ASSERT_EQ(loaded.effective, original.effective) << "load cycle=" << cycle;
        const auto bytes = sv::encode({21, {{"config", loaded.effective}}, {}});
        sv::Decoder decoder;
        const auto decoded = decoder.feed(bytes.data(), bytes.size());
        ASSERT_EQ(decoded.size(), 1U);
        const auto candidate = sv::parse_config(decoded[0].header.at("config"));
        ASSERT_EQ(candidate.effective, original.effective) << "wire cycle=" << cycle;
        sv::ConfigStore store(loaded, file.path);
        std::string error;
        ASSERT_TRUE(store.update(candidate, error)) << error;
        EXPECT_EQ(store.active()->effective, original.effective);
        EXPECT_EQ(store.persisted()->effective, original.effective);
        EXPECT_EQ(bits(store.active()->cameras[0].fx), bits(original.cameras[0].fx));
        EXPECT_EQ(bits(store.active()->cameras[0].T[7]), bits(original.cameras[0].T[7]));
    }
}

TEST(JsonPrecision, PolicyIsExplicitAndDatasetLoaderRetainsLegacyDefault)
{
    TemporaryConfig file;
    auto config = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::write_json(file.path, config.effective);
    const auto text = boost::json::serialize(config.effective);
    EXPECT_EQ(sv::read_json(file.path), sv::parse_json(text, sv::JsonNumberPolicy::Legacy));
    EXPECT_EQ(sv::load_config(file.path, sv::JsonNumberPolicy::Legacy).effective,
              sv::parse_config(sv::read_json(file.path)).effective);
    EXPECT_EQ(sv::read_json(file.path, sv::JsonNumberPolicy::Precise), sv::parse_json(text));
    EXPECT_EQ(sv::json_precise_numbers_available, BOOST_VERSION >= 108300);
}

TEST(JsonPrecision, PrecisePolicyDoesNotRelaxJsonSyntax)
{
    for (const auto *text :
         {"{\"v\":NaN}", "{\"v\":Infinity}", "{\"v\":1,}", "{\"v\":1}tail", "{/*comment*/\"v\":1}"})
    {
        EXPECT_ANY_THROW(sv::parse_json(text)) << text;
    }
}
