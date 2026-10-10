#include "sv/renderer.hpp"
#include "sv/shaders.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

// Extension declarations depend on the API types defined by gl3.h.
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sv
{
namespace
{
std::string source(const std::string &name)
{
    if (name == "surface.vert")
    {
        return std::string(surface_vertex);
    }
    if (name == "surface.frag")
    {
        return std::string(surface_fragment);
    }
    if (name == "projection.vert")
    {
        return std::string(projection_vertex);
    }
    if (name == "projection.frag")
    {
        return std::string(projection_fragment);
    }
    if (name == "vehicle.vert")
    {
        return std::string(vehicle_vertex);
    }
    if (name == "vehicle.frag")
    {
        return std::string(vehicle_fragment);
    }
    throw std::runtime_error("unknown embedded shader: " + name);
}

GLuint shader(GLenum kind, const std::string &text)
{
    GLuint s = glCreateShader(kind);
    const char *p = text.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096]{};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("shader compile: ") + log);
    }
    return s;
}

GLuint program(const std::string &v, const std::string &f)
{
    GLuint a = shader(GL_VERTEX_SHADER, source(v)), b = 0, p = 0;
    try
    {
        b = shader(GL_FRAGMENT_SHADER, source(f));
        p = glCreateProgram();
        glAttachShader(p, a);
        glAttachShader(p, b);
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[4096]{};
            glGetProgramInfoLog(p, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("shader link: ") + log);
        }
    }
    catch (...)
    {
        glDeleteShader(a);
        if (b)
        {
            glDeleteShader(b);
        }
        if (p)
        {
            glDeleteProgram(p);
        }
        throw;
    }
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

void matrix(GLuint p, const char *name, const Mat4 &m)
{
    float col[16];
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            col[4 * j + i] = static_cast<float>(m[4 * i + j]);
        }
    }
    glUniformMatrix4fv(glGetUniformLocation(p, name), 1, GL_FALSE, col);
}

void check(const char *step)
{
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
    {
        throw std::runtime_error(std::string(step) + ": GL error " + std::to_string(e));
    }
}

std::vector<float> vehicle_mesh(double length, double width)
{
    std::vector<float> vertices;
    const auto box = [&](Vec3 low, Vec3 high, Vec3 paint)
    {
        std::array<Vec3, 8> corners{{{low.x, low.y, low.z},
                                     {high.x, low.y, low.z},
                                     {high.x, high.y, low.z},
                                     {low.x, high.y, low.z},
                                     {low.x, low.y, high.z},
                                     {high.x, low.y, high.z},
                                     {high.x, high.y, high.z},
                                     {low.x, high.y, high.z}}};
        const int faces[6][6]{{0, 2, 1, 0, 3, 2}, {4, 5, 6, 4, 6, 7}, {0, 1, 5, 0, 5, 4},
                              {1, 2, 6, 1, 6, 5}, {2, 3, 7, 2, 7, 6}, {3, 0, 4, 3, 4, 7}};
        const double lighting[6]{.5, 1., .72, .92, .85, .7};
        for (int face = 0; face < 6; ++face)
        {
            for (int index : faces[face])
            {
                auto p = corners[index];
                for (double v : {p.x, p.y, p.z, paint.x * lighting[face], paint.y * lighting[face],
                                 paint.z * lighting[face]})
                {
                    vertices.push_back(float(v));
                }
            }
        }
    };
    const double x = length / 2, y = width / 2;
    box({-x, -y * .93, .35}, {x, y * .93, .85}, {.12, .42, .67});
    box({-x * .48, -y * .78, .85}, {x * .4, y * .78, 1.35}, {.08, .17, .23});
    box({-x * .48, -y * .8, 1.35}, {x * .4, y * .8, 1.4}, {.2, .55, .78});
    for (double axle : {-x * .65, x * .65})
    {
        for (double side : {-y, y})
        {
            box({axle - .3, side - .12, .12}, {axle + .3, side + .12, .55}, {.075, .08, .09});
        }
    }
    for (double side : {-y * .65, y * .65})
    {
        box({x, side - .13, .5}, {x + .025, side + .13, .67}, {.95, .94, .82});
        box({-x - .025, side - .13, .5}, {-x, side + .13, .67}, {.9, .12, .1});
    }
    return vertices;
}

void upload_mesh(const Mesh &mesh, GLuint &vao, GLuint &vbo, GLuint &ebo)
{
    std::vector<float> vertices;
    vertices.reserve(mesh.vertices.size() * 3);
    for (auto point : mesh.vertices)
    {
        vertices.insert(vertices.end(), {float(point.x), float(point.y), float(point.z)});
    }
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned),
                 mesh.indices.data(), GL_STATIC_DRAW);
}
} // namespace

struct Renderer::Impl
{
    Config config;
    Mesh mesh, floor_mesh, dome_mesh;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint prog = 0, projection = 0, vao = 0, vbo = 0, ebo = 0, fbo = 0, out = 0, depth = 0;
    GLuint vehicle_program = 0, vehicle_vao = 0, vehicle_vbo = 0;
    GLuint floor_vao = 0, floor_vbo = 0, floor_ebo = 0;
    GLuint dome_vao = 0, dome_vbo = 0, dome_ebo = 0;
    GLsizei vehicle_vertices = 0;
    std::array<GLuint, 4> inputs{};
    std::array<std::shared_ptr<const Image>, 4> uploaded{};
    uint64_t upload_count = 0, mesh_build_count = 0;
    RenderTiming timing;
    GLuint draw_query = 0;
    PFNGLGENQUERIESEXTPROC gen_queries = nullptr;
    PFNGLDELETEQUERIESEXTPROC delete_queries = nullptr;
    PFNGLBEGINQUERYEXTPROC begin_query = nullptr;
    PFNGLENDQUERYEXTPROC end_query = nullptr;
    PFNGLGETQUERYOBJECTUIVEXTPROC query_available = nullptr;
    PFNGLGETQUERYOBJECTUI64VEXTPROC query_result = nullptr;

    ~Impl()
    {
        if (context != EGL_NO_CONTEXT)
        {
            if (draw_query && delete_queries)
            {
                delete_queries(1, &draw_query);
            }
            glDeleteProgram(prog);
            glDeleteProgram(projection);
            glDeleteProgram(vehicle_program);
            glDeleteBuffers(1, &vehicle_vbo);
            glDeleteVertexArrays(1, &vehicle_vao);
            glDeleteBuffers(1, &floor_vbo);
            glDeleteBuffers(1, &floor_ebo);
            glDeleteVertexArrays(1, &floor_vao);
            glDeleteBuffers(1, &dome_vbo);
            glDeleteBuffers(1, &dome_ebo);
            glDeleteVertexArrays(1, &dome_vao);
            glDeleteBuffers(1, &vbo);
            glDeleteBuffers(1, &ebo);
            glDeleteVertexArrays(1, &vao);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &out);
            glDeleteRenderbuffers(1, &depth);
            glDeleteTextures(4, inputs.data());
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE)
        {
            eglDestroySurface(display, surface);
        }
        if (display != EGL_NO_DISPLAY)
        {
            eglTerminate(display);
        }
    }
};

Renderer::Renderer(const Config &c) : impl_(std::make_unique<Impl>())
{
    auto &i = *impl_;
    i.config = c;
    if (c.surface.type != "rectangular_bowl_v1")
    {
        if (c.surface.type == "cube_floor_v1")
        {
            auto floor = c.surface;
            floor.nx = floor.ny = floor.enclosure_cells;
            i.floor_mesh = make_mesh(floor);
            i.dome_mesh = make_box_shell(c.surface.enclosure_radius, c.surface.enclosure_height,
                                         c.surface.enclosure_cells);
        }
        else
        {
            i.floor_mesh = make_floor_mesh(c.surface.enclosure_radius, c.surface.floor_radial_cells,
                                           c.surface.dome_longitude_cells);
            i.dome_mesh =
                c.surface.type == "dome_floor_v1"
                    ? make_dome_mesh(c.surface.enclosure_radius, c.surface.dome_latitude_cells,
                                     c.surface.dome_longitude_cells)
                    : make_cylinder_shell(c.surface.enclosure_radius, c.surface.enclosure_height,
                                          c.surface.enclosure_cells, c.surface.dome_longitude_cells,
                                          c.surface.floor_radial_cells);
        }
    }
    else
    {
        i.mesh = make_mesh(c.surface);
    }
    ++i.mesh_build_count;
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    const char *backend = std::getenv("SV_EGL_PLATFORM");
    if (backend && std::string(backend) == "device")
    {
        auto query =
            reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
        EGLDeviceEXT devices[16];
        EGLint n = 0;
        if (!platform || !query || !query(16, devices, &n) || n == 0)
        {
            throw std::runtime_error("EGL device enumeration unavailable");
        }
        int index = 0;
        const char *selected = std::getenv("SV_EGL_DEVICE");
        if (selected)
        {
            index = std::stoi(selected);
        }
        if (index < 0 || index >= n)
        {
            throw std::runtime_error("SV_EGL_DEVICE out of range");
        }
        i.display = platform(EGL_PLATFORM_DEVICE_EXT, devices[index], nullptr);
    }
    else if (backend && std::string(backend) == "default")
    {
        i.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    else
    {
        i.display = platform ? platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                             : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    EGLint major, minor;
    if (i.display == EGL_NO_DISPLAY || !eglInitialize(i.display, &major, &minor))
    {
        throw std::runtime_error(
            "EGL display initialization failed (try EGL_PLATFORM=surfaceless)");
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API))
    {
        throw std::runtime_error("EGL ES API");
    }
    EGLint attrs[] = {EGL_SURFACE_TYPE,
                      EGL_PBUFFER_BIT,
                      EGL_RENDERABLE_TYPE,
                      EGL_OPENGL_ES3_BIT,
                      EGL_RED_SIZE,
                      8,
                      EGL_GREEN_SIZE,
                      8,
                      EGL_BLUE_SIZE,
                      8,
                      EGL_ALPHA_SIZE,
                      8,
                      EGL_NONE};
    EGLConfig cfg;
    EGLint count;
    if (!eglChooseConfig(i.display, attrs, &cfg, 1, &count) || count != 1)
    {
        throw std::runtime_error("EGL ES3 config");
    }
    EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    i.context = eglCreateContext(i.display, cfg, EGL_NO_CONTEXT, ctx);
    EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    i.surface = eglCreatePbufferSurface(i.display, cfg, pb);
    if (i.context == EGL_NO_CONTEXT || i.surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(i.display, i.surface, i.surface, i.context))
    {
        throw std::runtime_error("EGL context/pbuffer");
    }
    i.prog = program("surface.vert", "surface.frag");
    i.projection = program("projection.vert", "projection.frag");
    i.vehicle_program = program("vehicle.vert", "vehicle.frag");
    const auto body = vehicle_mesh(c.vehicle_length, c.vehicle_width);
    i.vehicle_vertices = body.size() / 6;
    glGenVertexArrays(1, &i.vehicle_vao);
    glBindVertexArray(i.vehicle_vao);
    glGenBuffers(1, &i.vehicle_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, i.vehicle_vbo);
    glBufferData(GL_ARRAY_BUFFER, body.size() * sizeof(float), body.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    const std::string extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (extensions.find("GL_EXT_disjoint_timer_query") != std::string::npos)
    {
        i.gen_queries =
            reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
        i.delete_queries =
            reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));
        i.begin_query =
            reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
        i.end_query = reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
        i.query_available = reinterpret_cast<PFNGLGETQUERYOBJECTUIVEXTPROC>(
            eglGetProcAddress("glGetQueryObjectuivEXT"));
        i.query_result = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(
            eglGetProcAddress("glGetQueryObjectui64vEXT"));
        if (i.gen_queries && i.delete_queries && i.begin_query && i.end_query &&
            i.query_available && i.query_result)
        {
            i.gen_queries(1, &i.draw_query);
        }
    }
    if (c.surface.type != "rectangular_bowl_v1")
    {
        upload_mesh(i.floor_mesh, i.floor_vao, i.floor_vbo, i.floor_ebo);
        upload_mesh(i.dome_mesh, i.dome_vao, i.dome_vbo, i.dome_ebo);
    }
    else
    {
        upload_mesh(i.mesh, i.vao, i.vbo, i.ebo);
    }
    glGenTextures(4, i.inputs.data());
    for (int k = 0; k < 4; k++)
    {
        glBindTexture(GL_TEXTURE_2D, i.inputs[k]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glGenTextures(1, &i.out);
    glBindTexture(GL_TEXTURE_2D, i.out);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c.width, c.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &i.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, i.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, i.out, 0);
    glGenRenderbuffers(1, &i.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, i.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, c.width, c.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, i.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        throw std::runtime_error("render FBO incomplete");
    }
    check("renderer init");
}

Renderer::~Renderer() = default;

Image Renderer::render(const FrameSet &set, const View &view)
{
    auto &i = *impl_;
    auto &c = i.config;
    i.timing = {};
    glBindFramebuffer(GL_FRAMEBUFFER, i.fbo);
    glViewport(0, 0, c.width, c.height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(.28f, .36f, .43f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(i.prog);
    matrix(i.prog, "mvp", view.mvp(double(c.width) / c.height));
    glUniform2f(glGetUniformLocation(i.prog, "vehicle"), c.vehicle_length / 2 + c.margin,
                c.vehicle_width / 2 + c.margin);
    GLint available[4]{};
    for (int k = 0; k < 4; k++)
    {
        const auto &cam = c.cameras[k];
        glActiveTexture(GL_TEXTURE0 + k);
        glBindTexture(GL_TEXTURE_2D, i.inputs[k]);
        if (set.frames[k])
        {
            auto image = set.frames[k]->image;
            if (image->width != cam.width || image->height != cam.height || image->channels != 3 ||
                image->pixels.size() != static_cast<size_t>(cam.width) * cam.height * 3)
            {
                throw std::runtime_error("input image/config mismatch");
            }
            if (i.uploaded[k] != image)
            {
                const auto upload_start = now_ns();
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, cam.width, cam.height, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, image->pixels.data());
                i.uploaded[k] = image;
                i.upload_count++;
                i.timing.upload_cpu_ms += (now_ns() - upload_start) / 1e6;
            }
            available[k] = 1;
        }
        std::string n = std::to_string(k);
        matrix(i.prog, ("pose[" + n + "]").c_str(), cam.T);
        glUniform4f(glGetUniformLocation(i.prog, ("intrinsics[" + n + "]").c_str()), cam.fx, cam.fy,
                    cam.cx, cam.cy);
        glUniform4f(glGetUniformLocation(i.prog, ("distortion[" + n + "]").c_str()), cam.k[0],
                    cam.k[1], cam.k[2], cam.k[3]);
        glUniform2f(glGetUniformLocation(i.prog, ("sizes[" + n + "]").c_str()), cam.width,
                    cam.height);
        glUniform2f(glGetUniformLocation(i.prog, ("limits[" + n + "]").c_str()), cam.theta_max,
                    cam.z_epsilon);
        glUniform1i(glGetUniformLocation(i.prog, ("input" + n).c_str()), k);
    }
    glUniform4iv(glGetUniformLocation(i.prog, "available"), 1, available);
    glUniform1i(glGetUniformLocation(i.prog, "fusion_mode"), c.fusion.mode == "hard_best_angle" ? 1
                                                             : c.fusion.mode == "angular_feather"
                                                                 ? 2
                                                                 : 0);
    glUniform1i(glGetUniformLocation(i.prog, "diagnostic_mode"),
                c.fusion.diagnostic == "coverage"  ? 1
                : c.fusion.diagnostic == "weights" ? 2
                                                   : 0);
    glUniform1f(glGetUniformLocation(i.prog, "edge_width_px"), c.fusion.edge_width_px);
    glUniform1f(glGetUniformLocation(i.prog, "angle_power"), c.fusion.angle_power);
    if (i.draw_query)
    {
        i.begin_query(GL_TIME_ELAPSED_EXT, i.draw_query);
    }
    const GLint region_location = glGetUniformLocation(i.prog, "surface_mode");
    const GLint radius_location = glGetUniformLocation(i.prog, "dome_radius");
    if (c.surface.type != "rectangular_bowl_v1")
    {
        glUniform1f(radius_location, c.surface.type == "dome_floor_v1"
                                         ? c.surface.enclosure_radius
                                         : c.surface.enclosure_height);
        glUniform1i(region_location, 0);
        glBindVertexArray(i.floor_vao);
        glDrawElements(GL_TRIANGLES, i.floor_mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
        glUniform1i(region_location, 1);
        glBindVertexArray(i.dome_vao);
        glDrawElements(GL_TRIANGLES, i.dome_mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
    }
    else
    {
        glUniform1f(radius_location, 1);
        glUniform1i(region_location, 0);
        glBindVertexArray(i.vao);
        glDrawElements(GL_TRIANGLES, i.mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
    }
    if (c.fusion.diagnostic == "color")
    {
        glUseProgram(i.vehicle_program);
        matrix(i.vehicle_program, "mvp", view.mvp(double(c.width) / c.height));
        glBindVertexArray(i.vehicle_vao);
        glDrawArrays(GL_TRIANGLES, 0, i.vehicle_vertices);
    }
    if (i.draw_query)
    {
        i.end_query(GL_TIME_ELAPSED_EXT);
    }
    const auto readback_start = now_ns();
    Image out{c.width, c.height, 4,
              std::vector<unsigned char>(static_cast<size_t>(c.width) * c.height * 4)},
        raw = out;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, c.width, c.height, GL_RGBA, GL_UNSIGNED_BYTE, raw.pixels.data());
    for (int y = 0; y < c.height; y++)
    {
        std::copy_n(raw.pixels.data() + static_cast<size_t>(c.height - 1 - y) * c.width * 4,
                    c.width * 4, out.pixels.data() + static_cast<size_t>(y) * c.width * 4);
    }
    check("render/readback");
    i.timing.readback_copy_cpu_ms = (now_ns() - readback_start) / 1e6;
    if (i.draw_query)
    {
        GLuint available = 0;
        GLint disjoint = 0;
        i.query_available(i.draw_query, GL_QUERY_RESULT_AVAILABLE_EXT, &available);
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
        i.timing.gpu_timer_status = disjoint     ? "disjoint"
                                    : !available ? "result_not_ready"
                                                 : "valid";
        if (available && !disjoint)
        {
            GLuint64 ns = 0;
            i.query_result(i.draw_query, GL_QUERY_RESULT_EXT, &ns);
            i.timing.gpu_draw_ms = ns / 1e6;
        }
        check("GPU timer query");
    }
    return out;
}

std::vector<Pixel> Renderer::project_points(const Camera &c, const std::vector<Vec3> &points)
{
    if (points.empty() || points.size() > 2048)
    {
        throw std::invalid_argument("projection point count");
    }
    auto &i = *impl_;
    GLuint tex = 0, fbo = 0, vao = 0, vbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, points.size(), 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        throw std::runtime_error("float projection FBO unsupported");
    }
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, points.size(), 1);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(i.projection);
    matrix(i.projection, "pose", c.T);
    glUniform4f(glGetUniformLocation(i.projection, "intrinsic"), c.fx, c.fy, c.cx, c.cy);
    glUniform4f(glGetUniformLocation(i.projection, "k"), c.k[0], c.k[1], c.k[2], c.k[3]);
    glUniform2f(glGetUniformLocation(i.projection, "size"), c.width, c.height);
    glUniform2f(glGetUniformLocation(i.projection, "limits"), c.theta_max, c.z_epsilon);
    glUniform1f(glGetUniformLocation(i.projection, "count"), points.size());
    std::vector<float> v;
    for (auto p : points)
    {
        v.push_back(p.x);
        v.push_back(p.y);
        v.push_back(p.z);
    }
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_POINTS, 0, points.size());
    std::vector<float> data(points.size() * 4);
    glReadPixels(0, 0, points.size(), 1, GL_RGBA, GL_FLOAT, data.data());
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    check("projection kernel");
    std::vector<Pixel> result;
    for (size_t k = 0; k < points.size(); k++)
    {
        result.push_back({data[4 * k], data[4 * k + 1], data[4 * k + 3] > .5f});
    }
    return result;
}

std::string Renderer::vendor() const
{
    return reinterpret_cast<const char *>(glGetString(GL_VENDOR));
}

std::string Renderer::device() const
{
    return reinterpret_cast<const char *>(glGetString(GL_RENDERER));
}

uint64_t Renderer::uploads() const
{
    return impl_->upload_count;
}

uint64_t Renderer::mesh_builds() const
{
    return impl_->mesh_build_count;
}

size_t Renderer::triangles() const
{
    if (impl_->config.surface.type != "rectangular_bowl_v1")
    {
        return (impl_->floor_mesh.indices.size() + impl_->dome_mesh.indices.size()) / 3;
    }
    return impl_->mesh.indices.size() / 3;
}

void Renderer::set_fusion(Fusion fusion) noexcept
{
    impl_->config.fusion = std::move(fusion);
}

RenderTiming Renderer::last_timing() const
{
    return impl_->timing;
}

boost::json::object Renderer::capabilities() const
{
    GLint texture = 0, renderbuffer = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer);
    return {
        {"gl_vendor", vendor()},
        {"gl_renderer", device()},
        {"gl_version", reinterpret_cast<const char *>(glGetString(GL_VERSION))},
        {"glsl_version", reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION))},
        {"gl_extensions", reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS))},
        {"egl_vendor", eglQueryString(impl_->display, EGL_VENDOR)},
        {"egl_version", eglQueryString(impl_->display, EGL_VERSION)},
        {"egl_extensions", eglQueryString(impl_->display, EGL_EXTENSIONS)},
        {"max_texture_size", texture},
        {"max_renderbuffer_size", renderbuffer},
        {"ego_vehicle_triangles", impl_->vehicle_vertices / 3},
        {"gpu_draw_timer_available", impl_->draw_query != 0}};
}
} // namespace sv
