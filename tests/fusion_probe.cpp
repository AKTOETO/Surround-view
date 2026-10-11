#include "sv/fusion.hpp"
#include <iostream>

int main()
{
    try
    {
        const auto input = boost::json::parse(std::cin).as_object();
        const int width = input.at("width").to_number<int>(),
                  height = input.at("height").to_number<int>();
        if (width < 1 || height < 1 || width * height > 4096)
        {
            throw std::runtime_error("probe budget");
        }
        sv::FusionSamples samples;
        const auto &colors = input.at("colors").as_array(),
                   &validity = input.at("validity").as_array(),
                   &edges = input.at("edges").as_array();
        for (int c = 0; c < 4; ++c)
        {
            samples.colors[c] = cv::Mat(height, width, CV_32FC3);
            samples.validity[c] = cv::Mat(height, width, CV_8UC1);
            samples.edge_weights[c] = cv::Mat(height, width, CV_32FC1);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const size_t n = (size_t(y) * width + x) * 4 + c;
                    samples.validity[c].at<uchar>(y, x) = validity.at(n).as_bool() ? 255 : 0;
                    samples.edge_weights[c].at<float>(y, x) = edges.at(n).to_number<float>();
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        samples.colors[c].at<cv::Vec3f>(y, x)[channel] =
                            colors.at(n * 3 + channel).to_number<float>();
                    }
                }
            }
        }
        sv::Fusion settings;
        settings.mode = std::string(input.at("mode").as_string());
        settings.pyramid_levels = input.at("levels").to_number<unsigned>();
        settings.smoothness_weight = input.at("smoothness").to_number<double>();
        if (auto v = input.if_contains("boundary"))
        {
            settings.pyramid_boundary = std::string(v->as_string());
        }
        if (auto v = input.if_contains("seam_solver"))
        {
            settings.seam_solver = std::string(v->as_string());
        }
        const auto result = sv::fuse_research(samples, settings);
        boost::json::array color, weights;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                for (float v : result.color.at<cv::Vec3f>(y, x).val)
                {
                    color.push_back(v);
                }
                for (float v : result.weights.at<cv::Vec4f>(y, x).val)
                {
                    weights.push_back(v);
                }
            }
        }
        std::cout << boost::json::serialize(
                         boost::json::object{{"color", color}, {"weights", weights}})
                  << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
