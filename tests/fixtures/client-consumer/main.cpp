#include "sv/client.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        sv::client::Options options;
        unsigned frames = 3;
        bool exercise = false, allow_reconnect = false, support = false, visibility = false,
             disagreement = false, configuration = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            auto next = [&]()
            {
                if (++i >= argc)
                {
                    throw std::runtime_error("missing option value");
                }
                return argv[i];
            };
            if (arg == "--unix")
            {
                options.endpoint.directory = next();
            }
            else if (arg == "--tcp")
            {
                options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
                options.endpoint.host = next();
                auto port = [&]()
                {
                    auto n = sv::parse_decimal_u64(next());
                    if (!n || n > 65535)
                    {
                        throw std::runtime_error("invalid port");
                    }
                    return uint16_t(n);
                };
                options.endpoint.control_port = port();
                options.endpoint.data_port = port();
            }
            else if (arg == "--frames")
            {
                auto n = sv::parse_decimal_u64(next());
                if (!n || n > 10000)
                {
                    throw std::runtime_error("invalid frame count");
                }
                frames = n;
            }
            else if (arg == "--allow-reconnect")
            {
                allow_reconnect = true;
            }
            else if (arg == "--exercise")
            {
                exercise = true;
            }
            else if (arg == "--disagreement")
            {
                disagreement = true;
            }
            else if (arg == "--configuration")
            {
                configuration = true;
            }
            else if (arg == "--visibility")
            {
                visibility = true;
            }
            else if (arg == "--support")
            {
                support = true;
            }
            else
            {
                throw std::runtime_error(
                    "usage: sv-client-probe --unix DIR|--tcp HOST "
                    "CONTROL_PORT DATA_PORT [--frames N --exercise|--support]");
            }
        }
        if ((support && exercise) || (visibility && (support || exercise)))
        {
            throw std::runtime_error("--support and --exercise are separate probe modes");
        }
        if (configuration && (support || visibility || disagreement || exercise))
        {
            throw std::runtime_error("--configuration is a separate probe mode");
        }
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<sv::client::Event> queue;
        bool overflow = false;
        sv::client::Client client(options,
                                  [&](sv::client::Event event)
                                  {
                                      std::lock_guard<std::mutex> lock(mutex);
                                      if (queue.size() >= 64)
                                      {
                                          overflow = true;
                                      }
                                      else
                                      {
                                          queue.push_back(std::move(event));
                                      }
                                      cv.notify_one();
                                  });
        unsigned received = 0, acknowledgements = 0;
        const unsigned expected_acks = configuration ? 3 : exercise ? 5 : support ? 3 : 1;
        uint64_t configuration_revision = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (received < frames || acknowledgements < expected_acks)
        {
            sv::client::Event event;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!cv.wait_until(lock, deadline, [&] { return !queue.empty() || overflow; }))
                {
                    throw std::runtime_error("probe timeout");
                }
                if (overflow)
                {
                    throw std::runtime_error("consumer queue full");
                }
                event = std::move(queue.front());
                queue.pop_front();
            }
            if (event.kind == sv::client::Event::Kind::Error)
            {
                if (!allow_reconnect)
                {
                    throw std::runtime_error(event.detail);
                }
                std::cout << boost::json::serialize(
                                 boost::json::object{{"client_error", event.detail}})
                          << std::endl;
                continue;
            }
            if (event.kind == sv::client::Event::Kind::State)
            {
                if (allow_reconnect)
                {
                    std::cout << boost::json::serialize(
                                     boost::json::object{{"client_state", event.detail}})
                              << std::endl;
                }
                if (event.detail == "retry_exhausted")
                {
                    throw std::runtime_error(event.detail);
                }
                if (event.detail == "ready")
                {
                    if (configuration)
                    {
                        client.read_configuration();
                    }
                    else
                    {
                        client.state();
                    }
                    if (support || visibility || disagreement)
                    {
                        client.output_catalog();
                        client.configure_output(
                            disagreement ? sv::client::OutputProduct::SourceDisagreement
                            : visibility ? sv::client::OutputProduct::SourceVisibility
                                         : sv::client::OutputProduct::RenderSupport);
                    }
                    if (exercise)
                    {
                        client.pause();
                        client.preset("top");
                        client.step();
                        client.command("unknown_test_command");
                    }
                }
                continue;
            }
            if (!event.message)
            {
                continue;
            }
            auto header = event.message->header;
            header["message_type"] = event.message->type;
            if (configuration && event.message->type == 21)
            {
                if (header.contains("configuration"))
                {
                    const auto snapshot = sv::client::parse_configuration_snapshot(*event.message);
                    configuration_revision = snapshot.revision;
                    header["validated_configuration_revision"] = std::to_string(snapshot.revision);
                    client.configuration_catalog();
                }
                else if (header.contains("configuration_catalog"))
                {
                    const auto catalog = sv::client::parse_configuration_catalog(*event.message);
                    header["validated_configuration_catalog"] = !catalog.replaceable_groups.empty();
                    client.validate_configuration(
                        configuration_revision,
                        {{"fusion", boost::json::object{{"mode", "angular_feather"}}}});
                }
                else
                {
                    const auto result = sv::client::parse_configuration_validation(*event.message);
                    header["validated_configuration_base_revision"] =
                        std::to_string(result.base_revision);
                }
            }
            if (event.message->type == 11)
            {
                const auto product = sv::client::validate_output_product(*event.message);
                if ((support && product != sv::client::OutputProduct::RenderSupport) ||
                    (visibility && product != sv::client::OutputProduct::SourceVisibility) ||
                    (disagreement && product != sv::client::OutputProduct::SourceDisagreement))
                {
                    client.release(event.message->header);
                    continue; // An old product may already be in flight when the command applies.
                }
                if (support)
                {
                    const sv::client::RenderSupportView view(event.message);
                    unsigned eligible = 0, positive = 0;
                    for (int y = 0; y < view.height(); ++y)
                    {
                        for (int x = 0; x < view.width(); ++x)
                        {
                            const auto pixel = view.at(x, y);
                            eligible += pixel.eligible_cameras > 0;
                            positive += pixel.positive_local_weights > 0;
                        }
                    }
                    header["eligible_pixels"] = eligible;
                    header["positive_local_weight_pixels"] = positive;
                }
                if (disagreement)
                {
                    const sv::client::SourceDisagreementView view(event.message);
                    unsigned defined = 0;
                    for (int y = 0; y < view.height(); ++y)
                    {
                        for (int x = 0; x < view.width(); ++x)
                        {
                            defined += view.at(x, y).defined;
                        }
                    }
                    header["defined_comparison_pixels"] = defined;
                }
                if (visibility)
                {
                    const sv::client::SourceVisibilityView view(event.message);
                    unsigned eligible = 0;
                    for (int y = 0; y < view.height(); ++y)
                    {
                        for (int x = 0; x < view.width(); ++x)
                        {
                            for (int camera = 0; camera < 4; ++camera)
                            {
                                eligible += sv::source_visibility_eligible(view.at(x, y, camera));
                            }
                        }
                    }
                    header["eligible_camera_pairs"] = eligible;
                }
                // Consumer retains an owning immutable Message; release does not invalidate it.
                uint64_t hash = 14695981039346656037ULL;
                for (auto byte : event.message->payload)
                {
                    hash ^= byte;
                    hash *= 1099511628211ULL;
                }
                header["rgba_fnv1a64"] = std::to_string(hash);
                client.release(event.message->header);
                ++received;
                if (exercise && received < frames)
                {
                    client.step();
                }
            }
            else if (event.message->type == 21)
            {
                if ((support || visibility) && !header.at("accepted").as_bool())
                {
                    throw std::runtime_error("support probe command rejected: " +
                                             std::string(header.at("reason").as_string()));
                }
                ++acknowledgements;
            }
            std::cout << boost::json::serialize(header) << std::endl;
        }
        client.stop();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
