#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

// Global variables
struct wl_display *display;
struct wl_surface *surface;
struct wl_compositor *compositor;
struct wl_shell *shell;
struct wl_shell_surface *shell_surface;
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
GLuint program, texture_id;

// Vertex & Fragment Shader sources
const char *vertex_shader_source =
    "attribute vec4 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "  v_texcoord = texcoord;\n"
    "}";

const char *fragment_shader_source =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture;\n"
    "uniform vec2 scale_factor;\n"
    "void main() {\n"
    "  vec2 scaled_texcoord = v_texcoord / scale_factor;\n"
    "  gl_FragColor = texture2D(texture, scaled_texcoord);\n"
    "}";

// Initialize Wayland
void init_wayland() {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        exit(EXIT_FAILURE);
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_display_dispatch(display);
}

// Initialize EGL
void init_egl() {
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    eglInitialize(egl_display, NULL, NULL);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_display, NULL, &config, 1, &num_configs);

    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, NULL);
    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)surface, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

// Compile shader
GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    return shader;
}

// Initialize OpenGL shaders & program
void init_gl() {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glUseProgram(program);

    // Setup vertex positions
    GLfloat vertices[] = {
        -1.0, -1.0, 0.0,  0.0, 0.0,
         1.0, -1.0, 0.0,  1.0, 0.0,
        -1.0,  1.0, 0.0,  0.0, 1.0,
         1.0,  1.0, 0.0,  1.0, 1.0,
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint pos_attrib = glGetAttribLocation(program, "position");
    glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(pos_attrib);

    GLint tex_attrib = glGetAttribLocation(program, "texcoord");
    glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(tex_attrib);
}

// Load an image using stb_image
unsigned char* load_image(const char *filename, int *width, int *height) {
    int channels;
    unsigned char *image_data = stbi_load(filename, width, height, &channels, 4);  // Force RGBA
    if (!image_data) {
        fprintf(stderr, "Failed to load image: %s\n", filename);
        exit(EXIT_FAILURE);
    }
    return image_data;
}

// Initialize texture
void init_texture(const char *image_path, int *image_width, int *image_height) {
    unsigned char *image_data = load_image(image_path, image_width, image_height);

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *image_width, *image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

    free(image_data);
}

// Render frame
void draw_frame(int image_width, int image_height, int target_width, int target_height) {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);

    // Compute scale factors
    float scale_x = (float) image_width / target_width;
    float scale_y = (float) image_height / target_height;
    GLint scale_uniform = glGetUniformLocation(program, "scale_factor");
    glUniform2f(scale_uniform, scale_x, scale_y);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    GLint tex_uniform = glGetUniformLocation(program, "texture");
    glUniform1i(tex_uniform, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(egl_display, egl_surface);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <image_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int target_width = 1920, target_height = 1080;  // Output resolution
    int image_width, image_height;

    init_wayland();
    init_egl();
    init_gl();
    init_texture(argv[1], &image_width, &image_height);

    while (1) {
        wl_display_dispatch_pending(display);
        draw_frame(image_width, image_height, target_width, target_height);
    }

    return 0;
}

