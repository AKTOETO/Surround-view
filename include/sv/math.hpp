#pragma once
#include <array>
#include <string>
#include <vector>

namespace sv
{
constexpr double pi = 3.14159265358979323846;

struct Vec3
{
    double x = 0, y = 0, z = 0;
};

Vec3 operator+(Vec3, Vec3);
Vec3 operator-(Vec3, Vec3);
Vec3 operator*(Vec3, double);
double dot(Vec3, Vec3);
Vec3 cross(Vec3, Vec3);
Vec3 normalized(Vec3);
using Mat4 = std::array<double, 16>; // row-major; column vectors
Mat4 identity();
Mat4 multiply(const Mat4 &, const Mat4 &);
Vec3 transform(const Mat4 &, Vec3);
Mat4 inverse_rigid(const Mat4 &);
Mat4 look_at(Vec3 eye, Vec3 target);
Mat4 perspective(double fov, double aspect, double near_z, double far_z);

struct Camera
{
    int id = 0, width = 0, height = 0;
    double fx = 0, fy = 0, cx = 0, cy = 0, theta_max = 1.45, z_epsilon = 1e-6;
    std::array<double, 4> k{};
    Mat4 T = identity();
    std::string name, calibration_id;
};

struct Pixel
{
    double u = 0, v = 0;
    bool valid = false;
};

Pixel project(const Camera &, Vec3 vehicle_point);

struct Surface
{
    double a = 2.6, b = 1.2, A = 6, B = 4.5, H = 1.5;
    int nx = 32, ny = 32;
    Vec3 point(double x, double y) const;
};

struct Mesh
{
    std::vector<Vec3> vertices;
    std::vector<unsigned> indices;
};

Mesh make_mesh(const Surface &);

struct View
{
    double azimuth = 0, elevation = .8, distance = 10, fov = 1;
    double near_z = .1, far_z = 50;
    Vec3 target{};
    Vec3 eye() const;
    Mat4 mvp(double aspect) const;
};

double decode_srgb(double);
double encode_srgb(double);
} // namespace sv
