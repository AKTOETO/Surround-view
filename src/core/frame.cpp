#include "sv/frame.hpp"
#include "sv/protocol.hpp"
#include <fstream>
#include <limits>
#include <stdexcept>

namespace sv
{
Image read_ppm(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
    {
        throw std::runtime_error("missing PPM: " + p.string());
    }
    auto token = [&]()
    {
        std::string s;
        while (f >> s)
        {
            if (s.front() == '#')
            {
                std::string line;
                std::getline(f, line);
                continue;
            }
            return s;
        }
        throw std::runtime_error("PPM truncated header");
    };
    if (token() != "P6")
    {
        throw std::runtime_error("only P6 RGB8 supported");
    }
    Image im;
    im.width = std::stoi(token());
    im.height = std::stoi(token());
    if (im.width < 2 || im.width > 4096 || im.height < 2 || im.height > 2160 || token() != "255")
    {
        throw std::runtime_error("invalid PPM dimensions/range");
    }
    char separator = f.get();
    if (separator == '\r' && f.peek() == '\n')
    {
        f.get();
    }
    else if (separator != ' ' && separator != '\n' && separator != '\t')
    {
        throw std::runtime_error("PPM separator");
    }
    im.channels = 3;
    im.pixels.resize(static_cast<size_t>(im.width) * im.height * 3);
    f.read(reinterpret_cast<char *>(im.pixels.data()), im.pixels.size());
    if (!f)
    {
        throw std::runtime_error("PPM truncated pixels");
    }
    if (f.peek() != EOF)
    {
        throw std::runtime_error("PPM trailing bytes");
    }
    return im;
}

void write_ppm(const std::filesystem::path &p, const Image &i)
{
    if (!p.parent_path().empty())
    {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream f(p, std::ios::binary);
    f << "P6\n" << i.width << ' ' << i.height << "\n255\n";
    for (size_t k = 0; k < i.pixels.size(); k += i.channels)
    {
        f.write(reinterpret_cast<const char *>(i.pixels.data() + k), 3);
    }
    if (!f)
    {
        throw std::runtime_error("cannot write PPM");
    }
}

bool Synchronizer::push(Frame f)
{
    if (f.camera_id < 0 || f.camera_id > 3 || !f.image)
    {
        throw std::invalid_argument("frame: camera/image");
    }
    int id = f.camera_id;
    if (last_sequence_[id] && f.sequence <= *last_sequence_[id])
    {
        if (f.sequence == *last_sequence_[id])
        {
            duplicate++;
        }
        else
        {
            out_of_order++;
        }
        return false;
    }
    if (last_time_[id] && f.release_ns < *last_time_[id])
    {
        out_of_order++;
        return false;
    }
    last_sequence_[id] = f.sequence;
    last_time_[id] = f.release_ns;
    auto &q = queues_[id];
    if (q.size() == capacity_)
    {
        q.pop_front();
        dropped++;
    }
    q.push_back(std::move(f));
    return true;
}

FrameSet Synchronizer::select(uint64_t now) const
{
    FrameSet set;
    std::optional<uint64_t> anchor;
    for (const auto &q : queues_)
    {
        for (auto it = q.rbegin(); it != q.rend(); ++it)
        {
            if (it->release_ns <= now && now - it->release_ns <= age_)
            {
                anchor = anchor ? std::min(*anchor, it->release_ns) : it->release_ns;
                break;
            }
        }
    }
    if (!anchor)
    {
        return set;
    }
    uint64_t low = std::numeric_limits<uint64_t>::max(), high = 0;
    int count = 0;
    for (int i = 0; i < 4; i++)
    {
        uint64_t best = std::numeric_limits<uint64_t>::max();
        for (const auto &f : queues_[i])
        {
            if (f.release_ns > now || now - f.release_ns > age_)
            {
                continue;
            }
            uint64_t delta =
                f.release_ns > *anchor ? f.release_ns - *anchor : *anchor - f.release_ns;
            if (delta <= skew_ &&
                (delta < best ||
                 (delta == best && (!set.frames[i] || f.release_ns > set.frames[i]->release_ns))))
            {
                set.frames[i] = f;
                best = delta;
            }
        }
        if (set.frames[i])
        {
            count++;
            low = std::min(low, set.frames[i]->release_ns);
            high = std::max(high, set.frames[i]->release_ns);
        }
    }
    set.skew_ns = count ? high - low : 0;
    set.health = count == 4 && set.skew_ns <= skew_ ? "READY" : count ? "DEGRADED" : "NO_INPUT";
    // If selections straddle the anchor by more than the total allowed window, discard the newest
    // side.
    if (set.skew_ns > skew_)
    {
        for (auto &f : set.frames)
        {
            if (f && f->release_ns - low > skew_)
            {
                f.reset();
            }
        }
        set.skew_ns = 0;
        high = low;
        for (auto &f : set.frames)
        {
            if (f)
            {
                high = std::max(high, f->release_ns);
            }
        }
        set.skew_ns = high - low;
    }
    return set;
}

std::vector<ReplayRow> load_manifest(const std::filesystem::path &p, const Config &c)
{
    auto value = read_json(p);
    auto &o = value.as_object();
    if (o.at("schema_version").as_int64() != 1)
    {
        throw std::runtime_error("manifest version");
    }
    auto &ids = o.at("calibration_ids").as_array();
    if (ids.size() != 4)
    {
        throw std::runtime_error("manifest camera count");
    }
    for (int i = 0; i < 4; i++)
    {
        if (ids[i].as_string() != c.cameras[i].calibration_id)
        {
            throw std::runtime_error("manifest calibration mismatch");
        }
    }
    std::vector<ReplayRow> rows;
    for (auto &rv : o.at("frames").as_array())
    {
        ReplayRow r;
        auto &row = rv.as_object();
        r.scenario_ns = parse_decimal_u64(std::string(row.at("scenario_timestamp_ns").as_string()));
        if (!rows.empty() && r.scenario_ns <= rows.back().scenario_ns)
        {
            throw std::runtime_error("manifest timestamp order");
        }
        auto &paths = row.at("paths").as_array();
        auto &offsets = row.at("offset_ns").as_array();
        if (paths.size() != 4 || offsets.size() != 4)
        {
            throw std::runtime_error("manifest input count");
        }
        for (int i = 0; i < 4; i++)
        {
            if (!paths[i].is_null())
            {
                r.paths[i] = p.parent_path() / std::string(paths[i].as_string());
            }
            r.offset_ns[i] = offsets[i].as_int64();
            if (r.offset_ns[i] > 1000000000 || r.offset_ns[i] < -1000000000)
            {
                throw std::runtime_error("manifest offset limit");
            }
        }
        rows.push_back(r);
    }
    if (rows.empty() || rows.size() > 100000)
    {
        throw std::runtime_error("manifest length");
    }
    return rows;
}
} // namespace sv
