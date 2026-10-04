#include "sv/math.hpp"
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <stdexcept>
namespace sv {
Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 operator*(Vec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalized(Vec3 v) {
    double n = std::sqrt(dot(v, v));
    if (!(n > 1e-12))
        throw std::invalid_argument("zero direction");
    return v * (1 / n);
}
Mat4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
Mat4 multiply(const Mat4 &a, const Mat4 &b) {
    Mat4 c{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++)
                c[4 * i + j] += a[4 * i + k] * b[4 * k + j];
    return c;
}
Vec3 transform(const Mat4 &m, Vec3 p) {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
            m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}
Mat4 inverse_rigid(const Mat4 &m) {
    Mat4 r = identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r[i * 4 + j] = m[j * 4 + i];
    Vec3 t = transform(r, {m[3], m[7], m[11]});
    r[3] = -t.x;
    r[7] = -t.y;
    r[11] = -t.z;
    return r;
}
namespace {
Mat4 rows(const glm::dmat4 &m) {
    Mat4 out{};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = m[c][r];
    return out;
}
} // namespace
Mat4 look_at(Vec3 eye, Vec3 target) {
    Vec3 f = normalized(target - eye), up = {0, 0, 1};
    if (std::abs(dot(f, up)) > .999999)
        up = {0, 1, 0};
    return rows(glm::lookAtRH(glm::dvec3(eye.x, eye.y, eye.z),
                              glm::dvec3(target.x, target.y, target.z),
                              glm::dvec3(up.x, up.y, up.z)));
}
Mat4 perspective(double fov, double aspect, double n, double f) {
    if (!(fov > 0 && fov < pi && aspect > 0 && n > 0 && f > n))
        throw std::invalid_argument("invalid perspective");
    return rows(glm::perspectiveRH_NO(fov, aspect, n, f));
}
Pixel project(const Camera &c, Vec3 p) {
    p = transform(c.T, p);
    if (!(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && p.z > c.z_epsilon))
        return {};
    double a = p.x / p.z, b = p.y / p.z, r = std::hypot(a, b), th = std::atan(r);
    if (th > c.theta_max)
        return {};
    double t2 = th * th,
           td = th * (1 + t2 * (c.k[0] + t2 * (c.k[1] + t2 * (c.k[2] + t2 * c.k[3]))));
    double q = r > 1e-14 ? td / r : 1;
    Pixel z{c.fx * a * q + c.cx, c.fy * b * q + c.cy, false};
    z.valid = std::isfinite(z.u) && std::isfinite(z.v) && z.u >= 0 && z.v >= 0 &&
              z.u <= c.width - 1 && z.v <= c.height - 1;
    return z;
}
Vec3 Surface::point(double x, double y) const {
    double dx = std::max(std::abs(x) - a, 0.0) / (A - a),
           dy = std::max(std::abs(y) - b, 0.0) / (B - b);
    return {x, y, H * (dx * dx + dy * dy) / 2};
}
Mesh make_mesh(const Surface &s) {
    auto axis = [](double extent, double flat, int n) {
        std::vector<double> out;
        for (int i = 0; i <= n; i++)
            out.push_back(-extent + 2 * extent * i / n);
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
        for (double x : xs)
            m.vertices.push_back(s.point(x, y));
    unsigned w = xs.size();
    for (unsigned y = 0; y + 1 < ys.size(); y++)
        for (unsigned x = 0; x + 1 < xs.size(); x++) {
            unsigned a = y * w + x, b = a + 1, c = a + w, d = c + 1;
            m.indices.insert(m.indices.end(), {a, b, d, a, d, c});
        }
    return m;
}
Vec3 View::eye() const {
    return target + Vec3{std::cos(elevation) * std::cos(azimuth),
                         std::cos(elevation) * std::sin(azimuth), std::sin(elevation)} *
                        distance;
}
Mat4 View::mvp(double aspect) const {
    return multiply(perspective(fov, aspect, near_z, far_z), look_at(eye(), target));
}
double decode_srgb(double v) {
    return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
}
double encode_srgb(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055;
}
} // namespace sv
