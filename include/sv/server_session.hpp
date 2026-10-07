#pragma once

#include "sv/config.hpp"
#include "sv/protocol.hpp"
#include <array>
#include <boost/json.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sv
{

struct SessionSubscriptions
{
    bool receive_final_output = true;
    bool receive_stitched_canvas = false;
    std::array<bool, 4> camera_mask = {true, true, true, true};
    unsigned target_fps = 60;
};

struct ClientSession
{
    std::string session_id;
    std::string data_token;
    std::string client_profile;
    bool is_control_only = true;
    View view;
    SessionSubscriptions subscriptions;
    uint64_t last_command_id = 0;
    std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_active = std::chrono::steady_clock::now();
};

class SessionRegistry
{
public:
    SessionRegistry() = default;

    std::shared_ptr<ClientSession> create_session(const std::string &session_id,
                                                   const std::string &token,
                                                   const View &default_view)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto session = std::make_shared<ClientSession>();
        session->session_id = session_id;
        session->data_token = token;
        session->view = default_view;
        sessions_[session_id] = session;
        return session;
    }

    std::shared_ptr<ClientSession> find_session(const std::string &session_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            return it->second;
        }
        return nullptr;
    }

    bool attach_data_channel(const std::string &session_id, const std::string &token)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if (it != sessions_.end() && it->second->data_token == token)
        {
            it->second->is_control_only = false;
            it->second->last_active = std::chrono::steady_clock::now();
            return true;
        }
        return false;
    }

    void remove_session(const std::string &session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(session_id);
    }

    std::vector<std::shared_ptr<ClientSession>> active_sessions() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::shared_ptr<ClientSession>> result;
        result.reserve(sessions_.size());
        for (const auto &kv : sessions_)
        {
            result.push_back(kv.second);
        }
        return result;
    }

    size_t count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ClientSession>> sessions_;
};

} // namespace sv
