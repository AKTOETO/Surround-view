#pragma once

#include "sv/config.hpp"
#include "sv/protocol.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace sv
{

class IClock
{
public:
    virtual ~IClock() = default;
    virtual uint64_t now_ns() const = 0;
};

class SystemClock : public IClock
{
public:
    uint64_t now_ns() const override
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
};

class MockClock : public IClock
{
public:
    explicit MockClock(uint64_t initial_time_ns = 1000000000ULL)
        : current_time_ns_(initial_time_ns)
    {
    }

    uint64_t now_ns() const override
    {
        return current_time_ns_;
    }

    void advance_ms(uint64_t ms)
    {
        current_time_ns_ += ms * 1000000ULL;
    }

    void set_time_ns(uint64_t ns)
    {
        current_time_ns_ = ns;
    }

private:
    uint64_t current_time_ns_;
};

class IConfigStore
{
public:
    virtual ~IConfigStore() = default;
    virtual std::shared_ptr<const Config> active() const = 0;
    virtual uint64_t revision() const = 0;
    virtual bool update(const Config &new_config, std::string &error_reason) = 0;
    virtual void persist() = 0;
};

class IProductSink
{
public:
    virtual ~IProductSink() = default;
    virtual bool publish(Message message) = 0;
};

} // namespace sv
