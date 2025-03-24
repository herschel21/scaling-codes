#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

// Hardcoded DRM Device and Resolution
#define DRM_DEVICE "/dev/dri/card2"
#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define SRC_WIDTH 640
#define SRC_HEIGHT 480
#define PIXEL_SIZE 4  // 4 bytes per pixel (RGBA)

// OpenGL Shaders
const char *vertex_shader_src =
    "attribute vec2 pos;\n"
    "attribute vec2 texCoord;\n"
    "varying vec2 fragTexCoord;\n"
    "void main() {\n"
    "   gl_Position = vec4(pos, 0.0, 1.0);\n"
    "   fragTexCoord = texCoord;\n"
    "}";

const char *fragment_shader_src =
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "uniform sampler2D textureSampler;\n"
    "void main() {\n"
    "   gl_FragColor = texture2D(textureSampler, fragTexCoord);\n"
    "}";

// Global variables
int drm_fd;
struct gbm_device *gbm;
struct gbm_surface *gbm_surface;
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
GLuint texture_id;

// Function to create an OpenGL context
void init_opengl() {
    drm_fd = open(DRM_DEVICE, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        perror("Cannot open DRM device");
        exit(EXIT_FAILURE);
    }

    gbm = gbm_create_device(drm_fd);
    gbm_surface = gbm_surface_create(gbm, SCREEN_WIDTH, SCREEN_HEIGHT, GBM_FORMAT_XRGB8888,
                                     GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

    egl_display = eglGetDisplay((EGLNativeDisplayType)gbm);
    eglInitialize(egl_display, NULL, NULL);

    EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
                               EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                               EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLConfig egl_config;
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs);

    EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);

    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)gbm_surface, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    // Load OpenGL shaders
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_src, NULL);
    glCompileShader(vertex_shader);

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_src, NULL);
    glCompileShader(fragment_shader);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glUseProgram(program);

    // Create OpenGL texture
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// Function to render the image using OpenGL
void render_image(unsigned char *image_data) {
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SRC_WIDTH, SRC_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

    GLfloat quad_vertices[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad_vertices);
    glEnableVertexAttribArray(0);

    GLfloat tex_coords[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, tex_coords);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    eglSwapBuffers(egl_display, egl_surface);
}

// Function to generate a random image
void generate_random_image(unsigned char *image_data) {
    srand(time(NULL));
    for (int i = 0; i < SRC_WIDTH * SRC_HEIGHT * 4; i++) {
        image_data[i] = rand() % 256;
    }
}

int main() {
    printf("Using DRM device: %s\n", DRM_DEVICE);
    printf("Rendering at resolution: %dx%d (Scaled from %dx%d)\n", SCREEN_WIDTH, SCREEN_HEIGHT, SRC_WIDTH, SRC_HEIGHT);

    init_opengl();

    unsigned char *image_data = malloc(SRC_WIDTH * SRC_HEIGHT * 4);
    generate_random_image(image_data);

    while (1) {
        render_image(image_data);
        usleep(16000); // ~60 FPS
    }

    free(image_data);
    close(drm_fd);

    return 0;
}

