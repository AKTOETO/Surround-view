#include "sv/config.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace sv
{
namespace
{
void keys(const boost::json::object &o, std::initializer_list<const char *> list,
          const std::string &p)
{
    std::set<std::string> allowed;
    for (auto k : list)
    {
        allowed.insert(k);
    }
    for (auto &kv : o)
    {
        if (!allowed.count(std::string(kv.key())))
        {
            throw std::invalid_argument(p + "." + std::string(kv.key()) + ": unknown field");
        }
    }
    for (auto k : list)
    {
        if (!o.contains(k))
        {
            throw std::invalid_argument(p + "." + k + ": missing field");
        }
    }
}

double num(const boost::json::value &v)
{
    double x = v.is_double()   ? v.as_double()
               : v.is_int64()  ? static_cast<double>(v.as_int64())
               : v.is_uint64() ? static_cast<double>(v.as_uint64())
                               : throw std::invalid_argument("expected number");
    if (!std::isfinite(x))
    {
        throw std::invalid_argument("nonfinite number");
    }
    return x;
}

int integer(const boost::json::value &v, int lo, int hi)
{
    double x = num(v);
    if (x != std::floor(x) || x < lo || x > hi)
    {
        throw std::invalid_argument("integer out of range");
    }
    return static_cast<int>(x);
}

void require(bool ok, const std::string &msg)
{
    if (!ok)
    {
        throw std::invalid_argument(msg);
    }
}

std::string str(const boost::json::value &v)
{
    return std::string(v.as_string());
}

// Find all real roots on a compact interval by recursively partitioning at derivative roots.
std::vector<double> roots(std::vector<double> c, double lo, double hi)
{
    while (c.size() > 1 && std::abs(c.back()) < 1e-18)
    {
        c.pop_back();
    }
    if (c.size() == 1)
    {
        return {};
    }
    auto eval = [&](double x)
    {
        double y = 0;
        for (auto i = c.rbegin(); i != c.rend(); ++i)
        {
            y = y * x + *i;
        }
        return y;
    };
    std::vector<double> d;
    for (size_t i = 1; i < c.size(); i++)
    {
        d.push_back(i * c[i]);
    }
    auto cuts = roots(d, lo, hi);
    cuts.insert(cuts.begin(), lo);
    cuts.push_back(hi);
    std::vector<double> out;
    for (double x : cuts)
    {
        if (std::abs(eval(x)) < 1e-12)
        {
            out.push_back(x);
        }
    }
    for (size_t i = 1; i < cuts.size(); i++)
    {
        double a = cuts[i - 1], b = cuts[i], fa = eval(a), fb = eval(b);
        if (fa * fb < 0)
        {
            for (int j = 0; j < 70; j++)
            {
                double m = (a + b) / 2, fm = eval(m);
                if (fa * fm <= 0)
                {
                    b = m;
                }
                else
                {
                    a = m;
                    fa = fm;
                }
            }
            out.push_back((a + b) / 2);
        }
    }
    return out;
}
} // namespace

boost::json::value read_json(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
    {
        throw std::runtime_error("cannot read " + p.string());
    }
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n < 0 || n > 16 * 1024 * 1024)
    {
        throw std::runtime_error("JSON file size limit");
    }
    f.seekg(0);
    std::string text(static_cast<size_t>(n), '\0');
    f.read(text.data(), n);
    if (!f)
    {
        throw std::runtime_error("truncated JSON");
    }
    return boost::json::parse(text);
}

void write_json(const std::filesystem::path &p, const boost::json::value &v)
{
    if (!p.parent_path().empty())
    {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream f(p);
    f << boost::json::serialize(v) << '\n';
    if (!f)
    {
        throw std::runtime_error("cannot write " + p.string());
    }
}

Config load_config(const std::filesystem::path &p)
{
    try
    {
        return parse_config(read_json(p));
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error(p.string() + ": " + e.what());
    }
}

Config parse_config(const boost::json::value &value)
{
    const auto &o = value.as_object();
    keys(o,
         {"schema_version", "profile_id", "units", "vehicle", "cameras", "surface",
          "virtual_camera", "output", "runtime"},
         "config");
    require(integer(o.at("schema_version"), 1, 1) == 1, "schema_version");
    Config c;
    c.effective = value;
    c.profile_id = str(o.at("profile_id"));
    const auto &units = o.at("units").as_object();
    keys(units, {"length", "angle", "time"}, "units");
    require(str(units.at("length")) == "m" && str(units.at("angle")) == "rad" &&
                str(units.at("time")) == "ns",
            "units: expected m/rad/ns");
    const auto &v = o.at("vehicle").as_object();
    keys(v, {"length_m", "width_m", "mask_margin_m"}, "vehicle");
    c.vehicle_length = num(v.at("length_m"));
    c.vehicle_width = num(v.at("width_m"));
    c.margin = num(v.at("mask_margin_m"));
    require(c.vehicle_length > 0 && c.vehicle_width > 0 && c.margin >= 0,
            "vehicle: invalid dimensions");
    const auto &cams = o.at("cameras").as_array();
    require(cams.size() == 4, "cameras: exactly four required");
    std::set<int> ids;
    for (auto &cv : cams)
    {
        const auto &co = cv.as_object();
        keys(co,
             {"id", "name", "calibration_id", "resolution", "projection", "T_camera_from_vehicle"},
             "camera");
        Camera a;
        a.id = integer(co.at("id"), 0, 3);
        require(ids.insert(a.id).second, "camera.id: duplicate");
        a.name = str(co.at("name"));
        a.calibration_id = str(co.at("calibration_id"));
        require(!a.calibration_id.empty(), "camera.calibration_id: empty");
        const auto &r = co.at("resolution").as_object();
        keys(r, {"width", "height"}, "camera.resolution");
        a.width = integer(r.at("width"), 2, 4096);
        a.height = integer(r.at("height"), 2, 2160);
        const auto &p = co.at("projection").as_object();
        keys(p, {"model", "fx", "fy", "cx", "cy", "alpha", "k", "theta_max_rad", "z_epsilon_m"},
             "camera.projection");
        require(str(p.at("model")) == "opencv_fisheye" && num(p.at("alpha")) == 0,
                "projection: unsupported model/skew");
        a.fx = num(p.at("fx"));
        a.fy = num(p.at("fy"));
        a.cx = num(p.at("cx"));
        a.cy = num(p.at("cy"));
        a.theta_max = num(p.at("theta_max_rad"));
        a.z_epsilon = num(p.at("z_epsilon_m"));
        require(a.fx > 0 && a.fy > 0 && a.theta_max > 0 && a.theta_max < pi / 2 && a.z_epsilon > 0,
                "camera.projection: invalid range");
        const auto &ks = p.at("k").as_array();
        require(ks.size() == 4, "projection.k: expected four");
        for (int i = 0; i < 4; i++)
        {
            a.k[i] = num(ks[i]);
        }
        auto eval = [&](double x)
        { return 1 + x * (3 * a.k[0] + x * (5 * a.k[1] + x * (7 * a.k[2] + x * 9 * a.k[3]))); };
        double end = a.theta_max * a.theta_max;
        auto probes = roots({3 * a.k[0], 10 * a.k[1], 21 * a.k[2], 36 * a.k[3]}, 0, end);
        probes.push_back(0);
        probes.push_back(end);
        for (double t : probes)
        {
            require(eval(t) > 1e-8, "projection.k: nonmonotonic");
        }
        const auto &T = co.at("T_camera_from_vehicle").as_array();
        require(T.size() == 4, "camera.T: four rows required");
        for (int i = 0; i < 4; i++)
        {
            const auto &row = T[i].as_array();
            require(row.size() == 4, "camera.T: four columns required");
            for (int j = 0; j < 4; j++)
            {
                a.T[4 * i + j] = num(row[j]);
            }
        }
        for (int j = 0; j < 4; j++)
        {
            require(std::abs(a.T[12 + j] - (j == 3 ? 1 : 0)) < 1e-8, "camera.T: homogeneous row");
        }
        for (int i = 0; i < 3; i++)
        {
            for (int j = 0; j < 3; j++)
            {
                double d = 0;
                for (int k = 0; k < 3; k++)
                {
                    d += a.T[4 * k + i] * a.T[4 * k + j];
                }
                require(std::abs(d - (i == j ? 1 : 0)) < 1e-8, "camera.T: nonorthogonal rotation");
            }
        }
        Vec3 x{a.T[0], a.T[4], a.T[8]}, y{a.T[1], a.T[5], a.T[9]}, z{a.T[2], a.T[6], a.T[10]};
        require(std::abs(dot(cross(x, y), z) - 1) < 1e-8, "camera.T: reflection");
        c.cameras[a.id] = a;
    }
    const auto &s = o.at("surface").as_object();
    c.surface.type = str(s.at("type"));
    if (c.surface.type == "rectangular_bowl_v1")
    {
        keys(s,
             {"type", "flat_half_length_m", "flat_half_width_m", "outer_half_length_m",
              "outer_half_width_m", "corner_height_m", "uniform_cells"},
             "surface");
        c.surface.a = num(s.at("flat_half_length_m"));
        c.surface.b = num(s.at("flat_half_width_m"));
        c.surface.A = num(s.at("outer_half_length_m"));
        c.surface.B = num(s.at("outer_half_width_m"));
        c.surface.H = num(s.at("corner_height_m"));
        require(c.surface.A > c.surface.a && c.surface.a > c.vehicle_length / 2 + c.margin &&
                    c.surface.B > c.surface.b && c.surface.b > c.vehicle_width / 2 + c.margin &&
                    c.surface.H >= 0,
                "surface: invalid bounds/mask");
        const auto &cells = s.at("uniform_cells").as_array();
        require(cells.size() == 2, "surface.uniform_cells");
        c.surface.nx = integer(cells[0], 2, 512);
        c.surface.ny = integer(cells[1], 2, 512);
    }
    else if (c.surface.type == "dome_floor_v1")
    {
        keys(s,
             {"type", "dome_radius_m", "dome_latitude_cells", "dome_longitude_cells",
              "floor_radial_cells"},
             "surface");
        c.surface.dome_radius = num(s.at("dome_radius_m"));
        c.surface.dome_latitude_cells = integer(s.at("dome_latitude_cells"), 8, 256);
        c.surface.dome_longitude_cells = integer(s.at("dome_longitude_cells"), 16, 512);
        c.surface.floor_radial_cells = integer(s.at("floor_radial_cells"), 4, 256);
        require(c.surface.dome_radius >
                    std::hypot(c.vehicle_length / 2 + c.margin, c.vehicle_width / 2 + c.margin),
                "surface: dome is too small to contain the vehicle");
        c.surface.A = c.surface.B = c.surface.dome_radius;
        c.surface.a = c.vehicle_length / 2 + c.margin;
        c.surface.b = c.vehicle_width / 2 + c.margin;
        c.surface.H = 0;
    }
    else
    {
        throw std::invalid_argument("surface: unsupported type");
    }
    const auto &view = o.at("virtual_camera").as_object();
    keys(view, {"azimuth_rad", "elevation_rad", "distance_m", "fov_y_rad", "clip_m"},
         "virtual_camera");
    c.view.azimuth = num(view.at("azimuth_rad"));
    c.view.elevation = num(view.at("elevation_rad"));
    c.view.distance = num(view.at("distance_m"));
    c.view.fov = num(view.at("fov_y_rad"));
    auto clips = view.at("clip_m").as_array();
    require(clips.size() == 2, "virtual_camera.clip_m");
    c.view.near_z = num(clips[0]);
    c.view.far_z = num(clips[1]);
    const bool safe_view =
        c.surface.type == "dome_floor_v1"
            ? c.view.distance >= 6 && c.view.distance < c.surface.dome_radius - .25
            : c.view.eye().z > c.surface.H + .1;
    require(c.view.elevation >= .35 && c.view.elevation <= pi / 2 && c.view.distance >= 6 &&
                c.view.distance <= 18 && safe_view,
            "virtual_camera: unsafe position");
    c.view.mvp(1);
    const auto &out = o.at("output").as_object();
    keys(out, {"width", "height"}, "output");
    c.width = integer(out.at("width"), 2, 2048);
    c.height = integer(out.at("height"), 2, 2048);
    const auto &rt = o.at("runtime").as_object();
    keys(rt, {"input_queue_per_camera", "skew_window_ms", "max_input_age_ms"}, "runtime");
    c.queue_size = integer(rt.at("input_queue_per_camera"), 1, 16);
    double skew = num(rt.at("skew_window_ms")), age = num(rt.at("max_input_age_ms"));
    require(skew > 0 && age >= skew && age <= 10000, "runtime: invalid time bounds");
    c.skew_ns = static_cast<uint64_t>(skew * 1e6);
    c.age_ns = static_cast<uint64_t>(age * 1e6);
    return c;
}

uint64_t now_ns()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}
} // namespace sv
