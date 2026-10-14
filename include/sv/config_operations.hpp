#pragma once
#include "sv/protocol.hpp"
#include <algorithm>
#include <cstring>
#include <list>
#include <stdexcept>
#include <string>

namespace sv
{
// Render-thread only. Entries survive reconnect, but never restart or automatic eviction.
class ConfigOperations
{
  public:

    static constexpr size_t capacity = 128;
    static constexpr size_t max_request_bytes = 16384;

    struct Request
    {
        uint64_t base_revision;
        boost::json::object changes;
        bool persist;

        bool operator==(const Request &other) const
        {
            return base_revision == other.base_revision && changes == other.changes &&
                   persist == other.persist;
        }
    };

    struct Entry
    {
        enum class State
        {
            Pending,
            Committed,
            Rejected
        };
        std::string id, reason;
        Request request;
        State state = State::Pending;
    };

    struct Execution
    {
        const Entry &entry;
        bool replayed;
    };

  private:

    std::string instance_;
    std::list<Entry> entries_;

    void check(const std::string &instance, const std::string &id) const
    {
        if (instance != instance_)
        {
            throw std::invalid_argument("configuration_server_instance_mismatch");
        }
        if (id.size() < 16 || id.size() > 64 ||
            !std::all_of(id.begin(), id.end(),
                         [](unsigned char c)
                         {
                             return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9') || c == '-' || c == '_';
                         }))
        {
            throw std::invalid_argument("configuration_operation_id_invalid");
        }
    }

  public:

    explicit ConfigOperations(std::string instance) : instance_(std::move(instance))
    {
        if (instance_.empty())
        {
            throw std::invalid_argument("configuration_server_instance_empty");
        }
    }

    const std::string &instance_id() const noexcept
    {
        return instance_;
    }

    const Entry *find(const std::string &instance, const std::string &id) const
    {
        check(instance, id);
        for (const auto &entry : entries_)
        {
            if (entry.id == id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    template <class Apply>
    Execution execute(const std::string &instance, const std::string &id, Request request,
                      Apply apply)
    {
        if (const auto *previous = find(instance, id))
        {
            if (!(previous->request == request))
            {
                throw std::invalid_argument("configuration_operation_id_conflict");
            }
            return {*previous, true};
        }
        if (entries_.size() == capacity)
        {
            throw std::length_error("configuration_operation_capacity_exhausted");
        }
        if (request.base_revision == UINT64_MAX)
        {
            throw std::overflow_error("config_revision_exhausted");
        }
        if (boost::json::serialize(request.changes).size() > max_request_bytes)
        {
            throw std::length_error("configuration_operation_request_too_large");
        }
        // Allocate the journal entry and failure storage before calling any mutating code.
        Entry pending{id, {}, std::move(request)};
        pending.reason.reserve(1024);
        entries_.push_back(std::move(pending));
        auto &entry = entries_.back();
        try
        {
            // Callback must throw only before commit; ConfigService::apply has this contract.
            apply(entry.request);
            entry.state = Entry::State::Committed; // No allocation after commit.
        }
        catch (const std::exception &error)
        {
            entry.reason.assign(error.what(), std::min<size_t>(std::strlen(error.what()), 1024));
            entry.state = Entry::State::Rejected;
        }
        // A non-standard exception leaves Pending; it must never be re-executed.
        return {entry, false};
    }

    boost::json::object status(const std::string &instance, const std::string &id,
                               bool replayed = false) const
    {
        const auto *entry = find(instance, id);
        boost::json::object result{{"schema_version", 1},
                                   {"scope", "config_store_operation"},
                                   {"server_instance_id", instance_},
                                   {"operation_id", id},
                                   {"state", "not_found"},
                                   {"replayed", replayed}};
        if (entry)
        {
            result["state"] = entry->state == Entry::State::Committed  ? "committed"
                              : entry->state == Entry::State::Rejected ? "rejected"
                                                                       : "pending";
            result["base_config_revision"] = std::to_string(entry->request.base_revision);
            result["persisted"] = entry->state == Entry::State::Committed && entry->request.persist;
            result["reason"] = entry->reason;
            if (entry->state == Entry::State::Committed)
            {
                result["revision"] = std::to_string(entry->request.base_revision + 1);
            }
        }
        return result;
    }
};
} // namespace sv
