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

// Supplied vehicle-frame height field, not an RGB-derived depth estimate.
struct LocalSupportPatch
{
    bool enabled = false;
    double center_x = 0, center_y = 0;
    double half_length = .2, half_width = .2, height = .8, transition = .2;
    double offset(double x, double y) const;
};

struct OcclusionUncertainty
{
    double radius = 0;
    std::string ambiguous_policy = "nominal"; // Internal default; explicit config: accept/reject.
};

// One explicitly supplied opaque vehicle-frame AABB; research visibility control.
struct SourceOcclusionBox
{
    bool enabled = false;
    Vec3 minimum, maximum;
    double tolerance = .02;
    std::string mode = "center";
    int guard_texels = 0;
    double support_tolerance = 1e-5;
    OcclusionUncertainty uncertainty{};
    bool require_facing_surface = false;
};

struct Surface
{
    std::string type = "rectangular_bowl_v1";
    double a = 2.6, b = 1.2, A = 6, B = 4.5, H = 1.5;
    // Assumed support height for rectangular carriers; not an estimated scene depth.
    double reference_height = 0;
    LocalSupportPatch local_support;
    SourceOcclusionBox source_occlusion;
    double enclosure_radius = 12, enclosure_height = 12;
    int nx = 32, ny = 32;
    int dome_latitude_cells = 64, dome_longitude_cells = 128, floor_radial_cells = 32;
    int enclosure_cells = 32;
    Vec3 point(double x, double y) const;
};

struct Mesh
{
    std::vector<Vec3> vertices;
    std::vector<unsigned> indices;
};

Mesh make_mesh(const Surface &);
Mesh make_floor_mesh(double radius, int radial_cells, int angular_cells);
Mesh make_dome_mesh(double radius, int latitude_cells, int longitude_cells);
Mesh make_cylinder_shell(double radius, double height, int vertical_cells, int angular_cells,
                         int cap_radial_cells);
Mesh make_box_shell(double half_extent, double height, int cells);

struct View
{
    double azimuth = 0, elevation = .8, distance = 10, fov = 1;
    double near_z = .1, far_z = 50;
    Vec3 target{};
    Vec3 eye() const;
    Mat4 mvp(double aspect) const;
};

bool safe_view(const Surface &, const View &, double clearance = .25);

double decode_srgb(double);
double encode_srgb(double);
} // namespace sv
