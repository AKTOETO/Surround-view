#include "sv/renderer.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace sv {
namespace {
std::string source(const std::string &name) {
    std::ifstream f(std::string(SV_SHADER_DIR) + "/" + name);
    if (!f)
        throw std::runtime_error("shader missing: " + name);
    return {(std::istreambuf_iterator<char>(f)), {}};
}
GLuint shader(GLenum kind, const std::string &text) {
    GLuint s = glCreateShader(kind);
    const char *p = text.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("shader compile: ") + log);
    }
    return s;
}
GLuint program(const std::string &v, const std::string &f) {
    GLuint a = shader(GL_VERTEX_SHADER, source(v)), b = 0, p = 0;
    try {
        b = shader(GL_FRAGMENT_SHADER, source(f));
        p = glCreateProgram();
        glAttachShader(p, a);
        glAttachShader(p, b);
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096]{};
            glGetProgramInfoLog(p, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("shader link: ") + log);
        }
    } catch (...) {
        glDeleteShader(a);
        if (b)
            glDeleteShader(b);
        if (p)
            glDeleteProgram(p);
        throw;
    }
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}
void matrix(GLuint p, const char *name, const Mat4 &m) {
    float col[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            col[4 * j + i] = static_cast<float>(m[4 * i + j]);
    glUniformMatrix4fv(glGetUniformLocation(p, name), 1, GL_FALSE, col);
}
void check(const char *step) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
        throw std::runtime_error(std::string(step) + ": GL error " + std::to_string(e));
}
} // namespace
struct Renderer::Impl {
    Config config;
    Mesh mesh;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    GLuint prog = 0, projection = 0, vao = 0, vbo = 0, ebo = 0, fbo = 0, out = 0, depth = 0;
    std::array<GLuint, 4> inputs{};
    std::array<std::shared_ptr<const Image>, 4> uploaded{};
    uint64_t upload_count = 0;
    ~Impl() {
        if (context != EGL_NO_CONTEXT) {
            glDeleteProgram(prog);
            glDeleteProgram(projection);
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
            eglDestroySurface(display, surface);
        if (display != EGL_NO_DISPLAY)
            eglTerminate(display);
    }
};
Renderer::Renderer(const Config &c) : impl_(std::make_unique<Impl>()) {
    auto &i = *impl_;
    i.config = c;
    i.mesh = make_mesh(c.surface);
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    const char *backend = std::getenv("SV_EGL_PLATFORM");
    if (backend && std::string(backend) == "device") {
        auto query =
            reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
        EGLDeviceEXT devices[16];
        EGLint n = 0;
        if (!platform || !query || !query(16, devices, &n) || n == 0)
            throw std::runtime_error("EGL device enumeration unavailable");
        int index = 0;
        const char *selected = std::getenv("SV_EGL_DEVICE");
        if (selected)
            index = std::stoi(selected);
        if (index < 0 || index >= n)
            throw std::runtime_error("SV_EGL_DEVICE out of range");
        i.display = platform(EGL_PLATFORM_DEVICE_EXT, devices[index], nullptr);
    } else
        i.display = platform ? platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                             : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    if (i.display == EGL_NO_DISPLAY || !eglInitialize(i.display, &major, &minor))
        throw std::runtime_error(
            "EGL display initialization failed (try EGL_PLATFORM=surfaceless)");
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        throw std::runtime_error("EGL ES API");
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
        throw std::runtime_error("EGL ES3 config");
    EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    i.context = eglCreateContext(i.display, cfg, EGL_NO_CONTEXT, ctx);
    EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    i.surface = eglCreatePbufferSurface(i.display, cfg, pb);
    if (i.context == EGL_NO_CONTEXT || i.surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(i.display, i.surface, i.surface, i.context))
        throw std::runtime_error("EGL context/pbuffer");
    i.prog = program("surface.vert", "surface.frag");
    i.projection = program("projection.vert", "projection.frag");
    std::vector<float> vertices;
    for (auto p : i.mesh.vertices) {
        vertices.push_back(p.x);
        vertices.push_back(p.y);
        vertices.push_back(p.z);
    }
    glGenVertexArrays(1, &i.vao);
    glBindVertexArray(i.vao);
    glGenBuffers(1, &i.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, i.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenBuffers(1, &i.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, i.mesh.indices.size() * sizeof(unsigned),
                 i.mesh.indices.data(), GL_STATIC_DRAW);
    glGenTextures(4, i.inputs.data());
    for (int k = 0; k < 4; k++) {
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
        throw std::runtime_error("render FBO incomplete");
    check("renderer init");
}
Renderer::~Renderer() = default;
Image Renderer::render(const FrameSet &set, const View &view) {
    auto &i = *impl_;
    auto &c = i.config;
    glBindFramebuffer(GL_FRAMEBUFFER, i.fbo);
    glViewport(0, 0, c.width, c.height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(.02f, .025f, .04f, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(i.prog);
    matrix(i.prog, "mvp", view.mvp(double(c.width) / c.height));
    glUniform2f(glGetUniformLocation(i.prog, "vehicle"), c.vehicle_length / 2 + c.margin,
                c.vehicle_width / 2 + c.margin);
    GLint available[4]{};
    for (int k = 0; k < 4; k++) {
        const auto &cam = c.cameras[k];
        glActiveTexture(GL_TEXTURE0 + k);
        glBindTexture(GL_TEXTURE_2D, i.inputs[k]);
        if (set.frames[k]) {
            auto image = set.frames[k]->image;
            if (image->width != cam.width || image->height != cam.height || image->channels != 3 ||
                image->pixels.size() != static_cast<size_t>(cam.width) * cam.height * 3)
                throw std::runtime_error("input image/config mismatch");
            if (i.uploaded[k] != image) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, cam.width, cam.height, 0, GL_RGB,
                             GL_UNSIGNED_BYTE, image->pixels.data());
                i.uploaded[k] = image;
                i.upload_count++;
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
    glBindVertexArray(i.vao);
    glDrawElements(GL_TRIANGLES, i.mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
    Image out{c.width, c.height, 4,
              std::vector<unsigned char>(static_cast<size_t>(c.width) * c.height * 4)},
        raw = out;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, c.width, c.height, GL_RGBA, GL_UNSIGNED_BYTE, raw.pixels.data());
    for (int y = 0; y < c.height; y++)
        std::copy_n(raw.pixels.data() + static_cast<size_t>(c.height - 1 - y) * c.width * 4,
                    c.width * 4, out.pixels.data() + static_cast<size_t>(y) * c.width * 4);
    check("render/readback");
    return out;
}
std::vector<Pixel> Renderer::project_points(const Camera &c, const std::vector<Vec3> &points) {
    if (points.empty() || points.size() > 2048)
        throw std::invalid_argument("projection point count");
    auto &i = *impl_;
    GLuint tex = 0, fbo = 0, vao = 0, vbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, points.size(), 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
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
    for (auto p : points) {
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
        result.push_back({data[4 * k], data[4 * k + 1], data[4 * k + 3] > .5f});
    return result;
}
std::string Renderer::vendor() const {
    return reinterpret_cast<const char *>(glGetString(GL_VENDOR));
}
std::string Renderer::device() const {
    return reinterpret_cast<const char *>(glGetString(GL_RENDERER));
}
uint64_t Renderer::uploads() const {
    return impl_->upload_count;
}
size_t Renderer::triangles() const {
    return impl_->mesh.indices.size() / 3;
}
} // namespace sv
