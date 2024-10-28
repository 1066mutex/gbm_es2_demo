#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <iostream>
#include <cassert>

// Simple vertex and fragment shaders
const char* vertex_shader_src = R"(
    attribute vec2 position;
    void main() {
        gl_Position = vec4(position, 0.0, 1.0);
    }
)";

const char* fragment_shader_src = R"(
    precision mediump float;
    void main() {
        gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0);  // Orange color
    }
)";

// DRM setup: Open the DRM device and initialize a GBM device
int init_drm(int &drm_fd, gbm_device* &gbm_dev) {
    drm_fd = open("/dev/dri/card0", O_RDWR);
    if (drm_fd < 0) {
        std::cerr << "Failed to open DRM device\n";
        return -1;
    }
    gbm_dev = gbm_create_device(drm_fd);
    return gbm_dev ? 0 : -1;
}

// Compile shader
GLuint compile_shader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compilation failed: " << log << "\n";
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Link shader program
GLuint create_program(const char* vertex_src, const char* fragment_src) {
    GLuint program = glCreateProgram();
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (vs && fs) {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// Initialize EGL, create context and surface
bool init_egl(EGLDisplay &egl_display, EGLContext &egl_context, EGLSurface &egl_surface, gbm_device* gbm_dev, gbm_bo *&bo) {
    egl_display = eglGetDisplay(gbm_dev);
    eglInitialize(egl_display, nullptr, nullptr);

    static const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_DONT_CARE,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs);

    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);

    bo = gbm_bo_create(gbm_dev, 1920, 1080, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
    EGLint khr_image_attrs[] = {
        EGL_WIDTH, (EGLint) gbm_bo_get_width(bo),
        EGL_HEIGHT, (EGLint) gbm_bo_get_height(bo),
        EGL_LINUX_DRM_FOURCC_EXT, (int) gbm_bo_get_format(bo),
        EGL_DMA_BUF_PLANE0_FD_EXT, gbm_bo_get_fd(bo),
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint) gbm_bo_get_stride(bo),
        EGL_NONE
    };

    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)bo, nullptr);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    return true;
}

// Function to create an FBO with an attached texture
GLuint create_fbo(GLuint &texture, int width, int height) {
    GLuint framebuffer;
    
    // Generate and bind the framebuffer
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // Create and attach a texture as color attachment
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    // Check for completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer not complete!" << std::endl;
        return 0;
    }

    // Unbind the framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return framebuffer;
}

int main() {
    int drm_fd;
    gbm_device* gbm_dev;
    if (init_drm(drm_fd, gbm_dev) < 0) return -1;

    gbm_bo* bo;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;

    if (!init_egl(egl_display, egl_context, egl_surface, gbm_dev, bo)) {
        std::cerr << "Failed to initialize EGL\n";
        return -1;
    }

    GLuint program = create_program(vertex_shader_src, fragment_shader_src);
    glUseProgram(program);

    // Define triangle vertices
    GLfloat vertices[] = {
        0.0f,  0.5f,
       -0.5f, -0.5f,
        0.5f, -0.5f
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint posAttrib = glGetAttribLocation(program, "position");
    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

    // Create an FBO and bind it
    GLuint fbo, texture;
    fbo = create_fbo(texture, 1920, 1080);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Render to FBO
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Unbind the FBO and display on screen
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    eglSwapBuffers(egl_display, egl_surface);

    // Cleanup
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    gbm_bo_destroy(bo);
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    gbm_device_destroy(gbm_dev);
    close(drm_fd);

    return 0;
}
