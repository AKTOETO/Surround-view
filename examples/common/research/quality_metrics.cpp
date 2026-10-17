#include "quality_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace sv::research
{
boost::json::object masked_rgb_metrics(const std::vector<unsigned char> &rgba,
                                       const std::vector<unsigned char> &rgb,
                                       const std::vector<unsigned char> &mask)
{
    if (rgb.empty() || rgb.size() % 3 || rgb.size() != mask.size() ||
        rgba.size() != rgb.size() / 3 * 4)
    {
        throw std::invalid_argument("quality pixel buffer sizes differ");
    }
    uint64_t pixels = 0, absolute = 0, squared = 0;
    unsigned maximum = 0;
    for (size_t pixel = 0; pixel < rgb.size() / 3; ++pixel)
    {
        const auto selected = mask[pixel * 3];
        if ((selected != 0 && selected != 255) || mask[pixel * 3 + 1] != selected ||
            mask[pixel * 3 + 2] != selected)
        {
            throw std::invalid_argument("quality mask must be binary RGB (0 or 255)");
        }
        if (!selected)
        {
            continue;
        }
        ++pixels;
        for (size_t channel = 0; channel < 3; ++channel)
        {
            const auto delta = static_cast<unsigned>(
                std::abs(int(rgba[pixel * 4 + channel]) - int(rgb[pixel * 3 + channel])));
            absolute += delta;
            squared += delta * delta;
            maximum = std::max(maximum, delta);
        }
    }
    if (!pixels)
    {
        throw std::invalid_argument("quality mask selects no pixels");
    }
    const double channels = double(pixels) * 3;
    const double mse = double(squared) / channels;
    boost::json::object result{
        {"selected_pixels", pixels},   {"mae_rgb8", double(absolute) / channels},
        {"rmse_rgb8", std::sqrt(mse)}, {"max_absolute_rgb8", maximum},
        {"exact_match", squared == 0}, {"psnr_db", nullptr}};
    // JSON has no infinity. null + exact_match=true denotes a perfect match.
    if (squared)
    {
        result["psnr_db"] = 10 * std::log10(255.0 * 255.0 / mse);
    }
    return result;
}

boost::json::array quality_summaries(const boost::json::array &samples, size_t variants)
{
    boost::json::array result;
    for (size_t variant = 0; variant < variants; ++variant)
    {
        // Paused repeats are not independent frames. Keep one observation per frame,
        // and reject changed quality results between repeats of the same inputs.
        std::map<unsigned, boost::json::object> frames;
        for (const auto &value : samples)
        {
            const auto &sample = value.as_object();
            if (sample.at("warmup").as_bool() ||
                sample.at("variant").to_number<size_t>() != variant)
            {
                continue;
            }
            const auto *quality = sample.if_contains("quality");
            if (!quality)
            {
                continue;
            }
            const auto index = sample.at("frame_index").to_number<unsigned>();
            const auto inserted = frames.emplace(index, quality->as_object());
            if (!inserted.second && inserted.first->second != quality->as_object())
            {
                throw std::runtime_error("quality results changed across paused repeats");
            }
        }
        if (frames.empty())
        {
            result.push_back(boost::json::object{{"variant", variant}, {"frames", 0}});
            continue;
        }
        double mae = 0, squared = 0;
        uint64_t pixels = 0;
        for (const auto &entry : frames)
        {
            const auto &quality = entry.second;
            const auto count = quality.at("selected_pixels").to_number<uint64_t>();
            const auto rmse = quality.at("rmse_rgb8").to_number<double>();
            pixels += count;
            mae += quality.at("mae_rgb8").to_number<double>() * count;
            squared += rmse * rmse * count;
        }
        result.push_back(
            boost::json::object{{"variant", variant},
                                {"frames", frames.size()},
                                {"selected_pixels", pixels},
                                {"pixel_weighted_mae_rgb8", mae / pixels},
                                {"pixel_weighted_rmse_rgb8", std::sqrt(squared / pixels)}});
    }
    return result;
}
} // namespace sv::research
