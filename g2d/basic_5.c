#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_WIDTH  1920  // Set your desired width
#define WINDOW_HEIGHT 1080  // Set your desired height

// Image generation parameters
#define IMAGE_WIDTH  256    // Source image width
#define IMAGE_HEIGHT 256    // Source image height

// Globals
struct wl_display *display;
struct wl_compositor *compositor;
struct wl_surface *surface;
struct wl_egl_window *egl_window;
struct wl_shell *shell;
struct wl_shell_surface *shell_surface;
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;

// OpenGL variables
GLuint texture_id;
GLuint program;
GLuint vbo;

// Shader sources
const char *vertex_shader_source =
    "attribute vec3 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 1.0);\n"
    "  v_texcoord = texcoord;\n"
    "}\n";

const char *fragment_shader_source =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(texture, v_texcoord);\n"
    "}\n";

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

// Compile shader
GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);
            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            fprintf(stderr, "Error compiling shader: %s\n", info_log);
            free(info_log);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

// Initialize shaders
GLuint init_shaders() {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);
            glGetProgramInfoLog(program, info_len, NULL, info_log);
            fprintf(stderr, "Error linking program: %s\n", info_log);
            free(info_log);
        }
        glDeleteProgram(program);
        return 0;
    }
    
    return program;
}

// Generate image data programmatically
unsigned char* generate_image() {
    unsigned char* data = malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 4);  // RGBA format
    
    for (int y = 0; y < IMAGE_HEIGHT; y++) {
        for (int x = 0; x < IMAGE_WIDTH; x++) {
            int index = (y * IMAGE_WIDTH + x) * 4;
            
            // Create a gradient pattern with some circular elements
            float dx = x - IMAGE_WIDTH/2;
            float dy = y - IMAGE_HEIGHT/2;
            float distance = sqrtf(dx*dx + dy*dy);
            
            // Normalize coordinates to [0,1]
            float nx = (float)x / IMAGE_WIDTH;
            float ny = (float)y / IMAGE_HEIGHT;
            
            // Create a pattern (this is just an example - you can modify for your needs)
            data[index] = (unsigned char)(sin(nx * 10) * 128 + 127);  // R
            data[index + 1] = (unsigned char)(cos(ny * 10) * 128 + 127);  // G
            data[index + 2] = (unsigned char)(sin(distance/10) * 128 + 127);  // B
            data[index + 3] = 255;  // A (fully opaque)
        }
    }
    
    return data;
}

// Initialize texture with our generated image
void init_texture() {
    // Generate texture
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Generate and load the image data
    unsigned char* image_data = generate_image();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, IMAGE_WIDTH, IMAGE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    
    // Free the image data as it's now in GPU memory
    free(image_data);
}

void init_geometry() {
    // Define vertices for a fullscreen quad (two triangles)
    // Position (x,y,z) and texture coordinates (s,t)
    float vertices[] = {
        // Position      // Texcoords
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, // Bottom-left
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f, // Bottom-right
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f, // Top-left
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f  // Top-right
    };
    
    // Create VBO
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
}

void init_wayland() {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        exit(EXIT_FAILURE);
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    surface = wl_compositor_create_surface(compositor);
    shell_surface = wl_shell_get_shell_surface(shell, surface);
    wl_shell_surface_set_toplevel(shell_surface);
    egl_window = wl_egl_window_create(surface, WINDOW_WIDTH, WINDOW_HEIGHT);
}

void init_egl() {
    static const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    
    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    eglInitialize(egl_display, NULL, NULL);
    EGLConfig config;
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs);
    eglBindAPI(EGL_OPENGL_ES_API);
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)egl_window, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

void init_gl() {
    // Initialize shaders
    program = init_shaders();
    if (!program) {
        fprintf(stderr, "Failed to initialize shaders\n");
        exit(EXIT_FAILURE);
    }
    
    // Initialize texture with generated image
    init_texture();
    
    // Initialize geometry
    init_geometry();
    
    // Set viewport to window dimensions
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
}

void draw_frame() {
    // Clear the color buffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Use shader program
    glUseProgram(program);
    
    // Bind VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    
    // Set position attribute
    GLint pos_attrib = glGetAttribLocation(program, "position");
    glEnableVertexAttribArray(pos_attrib);
    glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
    
    // Set texture coordinate attribute
    GLint tex_attrib = glGetAttribLocation(program, "texcoord");
    glEnableVertexAttribArray(tex_attrib);
    glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    
    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    GLint tex_uniform = glGetUniformLocation(program, "texture");
    glUniform1i(tex_uniform, 0);
    
    // Draw the quad (as triangle strip)
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Disable vertex attributes
    glDisableVertexAttribArray(pos_attrib);
    glDisableVertexAttribArray(tex_attrib);
    
    // Swap buffers
    eglSwapBuffers(egl_display, egl_surface);
}

void cleanup() {
    // Delete OpenGL resources
    glDeleteTextures(1, &texture_id);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    
    // Destroy EGL resources
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    
    // Destroy Wayland resources
    wl_egl_window_destroy(egl_window);
    wl_shell_surface_destroy(shell_surface);
    wl_surface_destroy(surface);
    wl_shell_destroy(shell);
    wl_compositor_destroy(compositor);
    wl_display_disconnect(display);
}

int main() {
    init_wayland();
    init_egl();
    init_gl();
    
    while (1) {
        wl_display_dispatch_pending(display);
        draw_frame();
    }
    
    cleanup();
    return 0;
}
