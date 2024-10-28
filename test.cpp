#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <drm/drm.h>

// DRM structures
int drm_fd;
drmModeConnector *connector;
drmModeEncoder *encoder;
drmModeModeInfo mode;
uint32_t crtc_id;

// GBM structures
struct gbm_device *gbm_device;
struct gbm_surface *gbm_surface;

// EGL structures
EGLDisplay display;
EGLContext context;
EGLSurface surface;

// OpenGL ES structures
GLuint framebuffer;
GLuint renderbuffer;
GLuint program;

// Shader sources
const char *vertex_shader_source =
    "attribute vec4 position;    \n"
    "void main()                 \n"
    "{                           \n"
    "   gl_Position = position;  \n"
    "}                           \n";

const char *fragment_shader_source =
    "precision mediump float;    \n"
    "void main()                 \n"
    "{                           \n"
    "   gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);  \n"
    "}                           \n";

void init_drm() {
    drm_fd = open("/dev/dri/card0", O_RDWR);
    drmModeRes *resources = drmModeGetResources(drm_fd);

    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm_fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            break;
        }
        drmModeFreeConnector(connector);
    }

    mode = connector->modes[0];

    for (int i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(drm_fd, resources->encoders[i]);
        if (encoder->encoder_id == connector->encoder_id) {
            break;
        }
        drmModeFreeEncoder(encoder);
    }

    crtc_id = encoder->crtc_id;

    drmModeFreeResources(resources);
}

void init_gbm() {
    gbm_device = gbm_create_device(drm_fd);
    gbm_surface = gbm_surface_create(gbm_device, mode.hdisplay, mode.vdisplay, 
                                     GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
}

void init_egl() {
    display = eglGetDisplay((EGLNativeDisplayType)gbm_device);
    eglInitialize(display, NULL, NULL);

    EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_config;
    eglChooseConfig(display, attributes, &config, 1, &num_config);

    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)gbm_surface, NULL);

    EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);

    eglMakeCurrent(display, surface, surface, context);
}

GLuint create_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    return shader;
}

void init_gles() {
    GLuint vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    glUseProgram(program);

    // Create and bind framebuffer
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // Create and bind renderbuffer
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, mode.hdisplay, mode.vdisplay);

    // Attach renderbuffer to framebuffer
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("Framebuffer is not complete!\n");
        exit(1);
    }
}

void render() {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, mode.hdisplay, mode.vdisplay);

    glClear(GL_COLOR_BUFFER_BIT);

    GLfloat vertices[] = {
        0.0f,  0.5f, 0.0f,
       -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f
    };

    GLint position_attrib = glGetAttribLocation(program, "position");
    glVertexAttribPointer(position_attrib, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(position_attrib);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    eglSwapBuffers(display, surface);
}

int main() {
    init_drm();
    init_gbm();
    init_egl();
    init_gles();

    render();

    getchar();  // Wait for user input before closing

    // Cleanup
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteProgram(program);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    gbm_surface_destroy(gbm_surface);
    gbm_device_destroy(gbm_device);
    drmModeFreeConnector(connector);
    drmModeFreeEncoder(encoder);
    close(drm_fd);

    return 0;
}