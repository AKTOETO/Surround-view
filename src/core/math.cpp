#include "sv/math.hpp"
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <stdexcept>

namespace sv
{
Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalized(Vec3 v)
{
    double n = std::sqrt(dot(v, v));
    if (!(n > 1e-12))
    {
        throw std::invalid_argument("zero direction");
    }
    return v * (1 / n);
}

Mat4 identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Mat4 multiply(const Mat4 &a, const Mat4 &b)
{
    Mat4 c{};
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                c[4 * i + j] += a[4 * i + k] * b[4 * k + j];
            }
        }
    }
    return c;
}

Vec3 transform(const Mat4 &m, Vec3 p)
{
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
            m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}

Mat4 inverse_rigid(const Mat4 &m)
{
    Mat4 r = identity();
    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            r[i * 4 + j] = m[j * 4 + i];
        }
    }
    Vec3 t = transform(r, {m[3], m[7], m[11]});
    r[3] = -t.x;
    r[7] = -t.y;
    r[11] = -t.z;
    return r;
}

namespace
{
Mat4 rows(const glm::dmat4 &m)
{
    Mat4 out{};
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            out[r * 4 + c] = m[c][r];
        }
    }
    return out;
}
} // namespace

Mat4 look_at(Vec3 eye, Vec3 target)
{
    Vec3 f = normalized(target - eye), up = {0, 0, 1};
    if (std::abs(dot(f, up)) > .999999)
    {
        up = {0, 1, 0};
    }
    return rows(glm::lookAtRH(glm::dvec3(eye.x, eye.y, eye.z),
                              glm::dvec3(target.x, target.y, target.z),
                              glm::dvec3(up.x, up.y, up.z)));
}

Mat4 perspective(double fov, double aspect, double n, double f)
{
    if (!(fov > 0 && fov < pi && aspect > 0 && n > 0 && f > n))
    {
        throw std::invalid_argument("invalid perspective");
    }
    return rows(glm::perspectiveRH_NO(fov, aspect, n, f));
}

Pixel project(const Camera &c, Vec3 p)
{
    p = transform(c.T, p);
    if (!(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && p.z > c.z_epsilon))
    {
        return {};
    }
    double a = p.x / p.z, b = p.y / p.z, r = std::hypot(a, b), th = std::atan(r);
    if (th > c.theta_max)
    {
        return {};
    }
    double t2 = th * th,
           td = th * (1 + t2 * (c.k[0] + t2 * (c.k[1] + t2 * (c.k[2] + t2 * c.k[3]))));
    double q = r > 1e-14 ? td / r : 1;
    Pixel z{c.fx * a * q + c.cx, c.fy * b * q + c.cy, false};
    z.valid = std::isfinite(z.u) && std::isfinite(z.v) && z.u >= 0 && z.v >= 0 &&
              z.u <= c.width - 1 && z.v <= c.height - 1;
    return z;
}

Vec3 Surface::point(double x, double y) const
{
    double dx = std::max(std::abs(x) - a, 0.0) / (A - a),
           dy = std::max(std::abs(y) - b, 0.0) / (B - b);
    return {x, y, H * (dx * dx + dy * dy) / 2};
}

Mesh make_mesh(const Surface &s)
{
    auto axis = [](double extent, double flat, int n)
    {
        std::vector<double> out;
        for (int i = 0; i <= n; i++)
        {
            out.push_back(-extent + 2 * extent * i / n);
        }
        out.push_back(-flat);
        out.push_back(flat);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end(),
                              [](double a, double b) { return std::abs(a - b) < 1e-10; }),
                  out.end());
        return out;
    };
    auto xs = axis(s.A, s.a, s.nx), ys = axis(s.B, s.b, s.ny);
    Mesh m;
    for (double y : ys)
    {
        for (double x : xs)
        {
            m.vertices.push_back(s.point(x, y));
        }
    }
    unsigned w = xs.size();
    for (unsigned y = 0; y + 1 < ys.size(); y++)
    {
        for (unsigned x = 0; x + 1 < xs.size(); x++)
        {
            unsigned a = y * w + x, b = a + 1, c = a + w, d = c + 1;
            m.indices.insert(m.indices.end(), {a, b, d, a, d, c});
        }
    }
    return m;
}

Mesh make_floor_mesh(double radius, int radial_cells, int angular_cells)
{
    if (!(std::isfinite(radius) && radius > 0) || radial_cells < 1 || angular_cells < 3)
    {
        throw std::invalid_argument("invalid floor mesh dimensions");
    }
    Mesh mesh;
    mesh.vertices.push_back({0, 0, 0});
    for (int r = 1; r <= radial_cells; ++r)
    {
        const double distance = radius * r / radial_cells;
        for (int a = 0; a < angular_cells; ++a)
        {
            const double angle = 2 * pi * a / angular_cells;
            mesh.vertices.push_back({distance * std::cos(angle), distance * std::sin(angle), 0});
        }
    }
    for (int a = 0; a < angular_cells; ++a)
    {
        const unsigned next = (a + 1) % angular_cells;
        mesh.indices.insert(mesh.indices.end(),
                            {0, static_cast<unsigned>(1 + a), static_cast<unsigned>(1 + next)});
    }
    for (int r = 0; r < radial_cells - 1; ++r)
    {
        const unsigned inner = 1 + r * angular_cells, outer = inner + angular_cells;
        for (int a = 0; a < angular_cells; ++a)
        {
            const unsigned next = (a + 1) % angular_cells;
            const unsigned i0 = inner + a, i1 = inner + next, o0 = outer + a, o1 = outer + next;
            mesh.indices.insert(mesh.indices.end(), {i0, o0, o1, i0, o1, i1});
        }
    }
    return mesh;
}

Mesh make_dome_mesh(double radius, int latitude_cells, int longitude_cells)
{
    if (!(std::isfinite(radius) && radius > 0) || latitude_cells < 2 || longitude_cells < 3)
    {
        throw std::invalid_argument("invalid dome mesh dimensions");
    }
    Mesh mesh;
    for (int latitude = 0; latitude < latitude_cells; ++latitude)
    {
        const double elevation = (pi / 2) * latitude / latitude_cells;
        const double ring_radius = radius * std::cos(elevation);
        for (int longitude = 0; longitude < longitude_cells; ++longitude)
        {
            const double angle = 2 * pi * longitude / longitude_cells;
            mesh.vertices.push_back({ring_radius * std::cos(angle), ring_radius * std::sin(angle),
                                     radius * std::sin(elevation)});
        }
    }
    const unsigned apex = static_cast<unsigned>(mesh.vertices.size());
    mesh.vertices.push_back({0, 0, radius});
    for (int latitude = 0; latitude < latitude_cells - 1; ++latitude)
    {
        const unsigned lower = latitude * longitude_cells;
        const unsigned upper = lower + longitude_cells;
        for (int longitude = 0; longitude < longitude_cells; ++longitude)
        {
            const unsigned next = (longitude + 1) % longitude_cells;
            const unsigned a = lower + longitude, b = lower + next;
            const unsigned c = upper + longitude, d = upper + next;
            mesh.indices.insert(mesh.indices.end(), {a, b, d, a, d, c});
        }
    }
    const unsigned last_ring = (latitude_cells - 1) * longitude_cells;
    for (int longitude = 0; longitude < longitude_cells; ++longitude)
    {
        const unsigned next = (longitude + 1) % longitude_cells;
        mesh.indices.insert(mesh.indices.end(),
                            {last_ring + static_cast<unsigned>(longitude), last_ring + next, apex});
    }
    return mesh;
}

Mesh make_cylinder_shell(double radius, double height, int vertical_cells, int angular_cells,
                         int cap_radial_cells)
{
    if (!std::isfinite(radius) || radius <= 0 || !std::isfinite(height) || height <= 0 ||
        vertical_cells < 1 || angular_cells < 3 || cap_radial_cells < 1)
    {
        throw std::invalid_argument("invalid cylinder dimensions");
    }
    Mesh mesh;
    for (int z = 0; z <= vertical_cells; ++z)
    {
        for (int a = 0; a < angular_cells; ++a)
        {
            const double angle = 2 * pi * a / angular_cells;
            mesh.vertices.push_back(
                {radius * std::cos(angle), radius * std::sin(angle), height * z / vertical_cells});
        }
    }
    for (int z = 0; z < vertical_cells; ++z)
    {
        for (int a = 0; a < angular_cells; ++a)
        {
            const unsigned lower = z * angular_cells;
            const unsigned next = (a + 1) % angular_cells;
            const unsigned p = lower + a, q = lower + next;
            mesh.indices.insert(mesh.indices.end(),
                                {p, q, q + angular_cells, p, q + angular_cells, p + angular_cells});
        }
    }
    const auto cap = make_floor_mesh(radius, cap_radial_cells, angular_cells);
    const unsigned offset = mesh.vertices.size();
    for (auto point : cap.vertices)
    {
        point.z = height;
        mesh.vertices.push_back(point);
    }
    for (auto index : cap.indices)
    {
        mesh.indices.push_back(offset + index);
    }
    return mesh;
}

Mesh make_box_shell(double half_extent, double height, int cells)
{
    if (!std::isfinite(half_extent) || half_extent <= 0 || !std::isfinite(height) || height <= 0 ||
        cells < 1)
    {
        throw std::invalid_argument("invalid box dimensions");
    }
    Mesh mesh;
    auto plane = [&](Vec3 origin, Vec3 u, Vec3 v)
    {
        const unsigned offset = mesh.vertices.size();
        for (int y = 0; y <= cells; ++y)
        {
            for (int x = 0; x <= cells; ++x)
            {
                mesh.vertices.push_back(origin + u * (double(x) / cells) + v * (double(y) / cells));
            }
        }
        for (int y = 0; y < cells; ++y)
        {
            for (int x = 0; x < cells; ++x)
            {
                const unsigned a = offset + y * (cells + 1) + x, b = a + 1;
                const unsigned c = a + cells + 1, d = c + 1;
                mesh.indices.insert(mesh.indices.end(), {a, b, d, a, d, c});
            }
        }
    };
    const double r = half_extent;
    plane({r, -r, 0}, {0, 2 * r, 0}, {0, 0, height});
    plane({-r, r, 0}, {0, -2 * r, 0}, {0, 0, height});
    plane({r, r, 0}, {-2 * r, 0, 0}, {0, 0, height});
    plane({-r, -r, 0}, {2 * r, 0, 0}, {0, 0, height});
    plane({-r, -r, height}, {2 * r, 0, 0}, {0, 2 * r, 0});
    return mesh;
}

bool safe_view(const Surface &surface, const View &view, double clearance)
{
    const auto eye = view.eye();
    if (surface.type == "rectangular_bowl_v1")
    {
        return eye.z > surface.H + .1;
    }
    if (eye.z <= clearance)
    {
        return false;
    }
    const double radius = surface.enclosure_radius - clearance;
    if (surface.type == "dome_floor_v1")
    {
        return dot(eye, eye) < radius * radius;
    }
    if (eye.z >= surface.enclosure_height - clearance)
    {
        return false;
    }
    if (surface.type == "cylinder_floor_v1")
    {
        return std::hypot(eye.x, eye.y) < radius;
    }
    return surface.type == "cube_floor_v1" && std::abs(eye.x) < radius && std::abs(eye.y) < radius;
}

Vec3 View::eye() const
{
    return target + Vec3{std::cos(elevation) * std::cos(azimuth),
                         std::cos(elevation) * std::sin(azimuth), std::sin(elevation)} *
                        distance;
}

Mat4 View::mvp(double aspect) const
{
    return multiply(perspective(fov, aspect, near_z, far_z), look_at(eye(), target));
}

double decode_srgb(double v)
{
    return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
}

double encode_srgb(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    return v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055;
}
} // namespace sv
