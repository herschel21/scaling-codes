#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>  // For timing the scaling operations

// X11 includes - add these for X11 support
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/eglplatform.h>

#define WINDOW_WIDTH  1920  // Set your desired width
#define WINDOW_HEIGHT 1080  // Set your desired height
#define SCALING_ITERATIONS 100  // Number of times to perform scaling

// Enum to track which display server we're using
typedef enum {
    DISPLAY_WAYLAND,
    DISPLAY_X11,
    DISPLAY_UNKNOWN
} DisplayServerType;

// Globals
DisplayServerType display_server_type = DISPLAY_UNKNOWN;

// Wayland globals
struct wl_display *wl_display;
struct wl_compositor *compositor;
struct wl_surface *wl_surface;
struct wl_egl_window *wl_egl_window;
struct wl_shell *shell;
struct wl_shell_surface *shell_surface;

// X11 globals
Display *x_display = NULL;
Window x_window;
Colormap x_colormap;
XVisualInfo *x_visual_info;

// Common EGL globals
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
EGLConfig egl_config;

// OpenGL variables
GLuint texture_id;
GLuint program;
GLuint vbo;
GLuint framebuffer;  // For offscreen rendering
GLuint output_texture;  // Texture to store the scaled result

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

// Simple structure to hold image data
typedef struct {
    unsigned char* data;
    int width;
    int height;
    int channels; // Number of channels (e.g., 3 for RGB, 4 for RGBA)
} Image;

// Function to detect available display server
DisplayServerType detect_display_server() {
    // Try to connect to Wayland first
    struct wl_display *test_display = wl_display_connect(NULL);
    if (test_display) {
        printf("Wayland display server detected\n");
        wl_display_disconnect(test_display);
        return DISPLAY_WAYLAND;
    }
    
    // Try X11 next
    Display *test_x_display = XOpenDisplay(NULL);
    if (test_x_display) {
        printf("X11 display server detected\n");
        XCloseDisplay(test_x_display);
        return DISPLAY_X11;
    }
    
    printf("No supported display server detected\n");
    return DISPLAY_UNKNOWN;
}

// Function to generate a random image
Image* generate_random_image(int width, int height) {
    Image* img = (Image*)malloc(sizeof(Image));
    if (!img) {
        return NULL;
    }

    img->width = width;
    img->height = height;
    img->channels = 3;  // RGB

    img->data = (unsigned char*)malloc(width * height * img->channels);
    if (!img->data) {
        free(img);
        return NULL;
    }

    // Seed the random number generator
    srand(time(NULL));

    // Fill the image data with changing RGB values
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = (y * width + x) * img->channels;
            img->data[index] = (x % 256);         // Red channel
            img->data[index + 1] = (y % 256);     // Green channel
            img->data[index + 2] = ((x + y) % 256); // Blue channel
        }
    }

    return img;
}

// Function to convert RGB to RGBA (adding alpha channel)
unsigned char* convert_rgb_to_rgba(const Image* img) {
    if (img->channels == 4) {
        // Already RGBA, just duplicate
        unsigned char* rgba = (unsigned char*)malloc(img->width * img->height * 4);
        if (!rgba) return NULL;
        memcpy(rgba, img->data, img->width * img->height * 4);
        return rgba;
    }
    
    // Allocate memory for RGBA data
    unsigned char* rgba = (unsigned char*)malloc(img->width * img->height * 4);
    if (!rgba) return NULL;
    
    // Convert RGB to RGBA (set alpha to 255)
    for (int i = 0; i < img->width * img->height; i++) {
        rgba[i*4]   = img->data[i*3];     // R
        rgba[i*4+1] = img->data[i*3+1];   // G
        rgba[i*4+2] = img->data[i*3+2];   // B
        rgba[i*4+3] = 255;                // A (fully opaque)
    }
    
    return rgba;
}

// Function to save a PPM image (simple format for demo purposes)
void save_ppm(const char* filename, unsigned char* data, int width, int height) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Error opening file for writing: %s\n", filename);
        return;
    }
    
    // Write PPM header
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    
    // Write RGB data (skip alpha channel)
    for (int i = 0; i < width * height; i++) {
        fputc(data[i*4], file);     // R
        fputc(data[i*4+1], file);   // G
        fputc(data[i*4+2], file);   // B
    }
    
    fclose(file);
    printf("Saved image to %s\n", filename);
}

// Free image resources
void free_image(Image* img) {
    if (img) {
        if (img->data) {
            free(img->data);
        }
        free(img);
    }
}

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

// Initialize offscreen framebuffer
void init_framebuffer() {
    // Create a framebuffer
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    // Create a texture to render to
    glGenTextures(1, &output_texture);
    glBindTexture(GL_TEXTURE_2D, output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Attach texture to framebuffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture, 0);
    
    // Check if framebuffer is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer is not complete!\n");
        exit(EXIT_FAILURE);
    }
    
    // Unbind framebuffer to return to default
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Initialize texture with the generated image
void init_texture() {
    // Generate a random image
    Image* img = generate_random_image(WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!img) {
        fprintf(stderr, "Failed to generate random image\n");
        exit(EXIT_FAILURE);
    }
    
    printf("Generated random image: %dx%d with %d channels\n", img->width, img->height, img->channels);
    
    // Convert to RGBA if needed
    unsigned char* rgba_data = convert_rgb_to_rgba(img);
    if (!rgba_data) {
        fprintf(stderr, "Failed to convert image to RGBA\n");
        free_image(img);
        exit(EXIT_FAILURE);
    }
    
    // Generate texture
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Load the image data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img->width, img->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    
    // Free the image data
    free(rgba_data);
    free_image(img);
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

// Initialize Wayland display
void init_wayland() {
    wl_display = wl_display_connect(NULL);
    if (!wl_display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        exit(EXIT_FAILURE);
    }
    struct wl_registry *registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wl_display);
    wl_surface = wl_compositor_create_surface(compositor);
    shell_surface = wl_shell_get_shell_surface(shell, wl_surface);
    wl_shell_surface_set_toplevel(shell_surface);
    wl_egl_window = wl_egl_window_create(wl_surface, WINDOW_WIDTH, WINDOW_HEIGHT);
}

// Initialize X11 display
void init_x11() {
    x_display = XOpenDisplay(NULL);
    if (!x_display) {
        fprintf(stderr, "Failed to open X11 display\n");
        exit(EXIT_FAILURE);
    }
    
    // Get the default screen
    int screen = DefaultScreen(x_display);
    
    // Get a visual
    XVisualInfo visual_template;
    visual_template.visualid = XVisualIDFromVisual(DefaultVisual(x_display, screen));
    int num_visuals;
    x_visual_info = XGetVisualInfo(x_display, VisualIDMask, &visual_template, &num_visuals);
    
    // Create color map
    x_colormap = XCreateColormap(x_display, RootWindow(x_display, screen),
                               x_visual_info->visual, AllocNone);
    
    // Set window attributes
    XSetWindowAttributes window_attrs;
    window_attrs.colormap = x_colormap;
    window_attrs.background_pixel = 0;
    window_attrs.border_pixel = 0;
    window_attrs.event_mask = ExposureMask | KeyPressMask;
    
    // Create window
    x_window = XCreateWindow(x_display, RootWindow(x_display, screen),
                          0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
                          x_visual_info->depth, InputOutput,
                          x_visual_info->visual,
                          CWBorderPixel | CWColormap | CWEventMask,
                          &window_attrs);
    
    // Set window name
    XStoreName(x_display, x_window, "EGL Scaling Demo");
    
    // Show window
    XMapWindow(x_display, x_window);
    XFlush(x_display);
}

// Initialize EGL for Wayland
void init_egl_wayland() {
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
    
    egl_display = eglGetDisplay((EGLNativeDisplayType)wl_display);
    eglInitialize(egl_display, NULL, NULL);
    
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs);
    eglBindAPI(EGL_OPENGL_ES_API);
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)wl_egl_window, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

// Initialize EGL for X11
void init_egl_x11() {
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
    
    egl_display = eglGetDisplay((EGLNativeDisplayType)x_display);
    eglInitialize(egl_display, NULL, NULL);
    
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs);
    eglBindAPI(EGL_OPENGL_ES_API);
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)x_window, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

void init_display() {
    // Detect display server type
    display_server_type = detect_display_server();
    
    // Initialize appropriate display server
    switch (display_server_type) {
        case DISPLAY_WAYLAND:
            printf("Initializing Wayland display\n");
            init_wayland();
            init_egl_wayland();
            break;
            
        case DISPLAY_X11:
            printf("Initializing X11 display\n");
            init_x11();
            init_egl_x11();
            break;
            
        default:
            fprintf(stderr, "No supported display server available\n");
            exit(EXIT_FAILURE);
    }
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
    
    // Initialize framebuffer for offscreen rendering
    init_framebuffer();
    
    // Initialize geometry
    init_geometry();
    
    // Set viewport to window dimensions
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
}

// Function to perform a single scaling operation
void perform_scaling() {
    // Generate a new random image for each iteration
    init_texture();

    // Bind to framebuffer for offscreen rendering
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    // Set viewport to target dimensions
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    
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
    
    // Unbind framebuffer to return to default
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Function to perform 100 scaling operations and track performance
void batch_scaling(const char* output_path) {
    printf("Starting batch scaling: %d iterations\n", SCALING_ITERATIONS);
    
    // Timing variables
    clock_t start, end;
    double cpu_time_used;
    
    // Start timing
    start = clock();
    
    // Perform scaling multiple times
    for (int i = 0; i < SCALING_ITERATIONS; i++) {
        perform_scaling();
    }
    
    // End timing
    end = clock();
    cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("Batch scaling completed in %f seconds\n", cpu_time_used);
    printf("Average time per operation: %f seconds\n", cpu_time_used / SCALING_ITERATIONS);
    
    // Read back the final scaled image
    unsigned char* scaled_data = (unsigned char*)malloc(WINDOW_WIDTH * WINDOW_HEIGHT * 4);
    if (!scaled_data) {
        fprintf(stderr, "Failed to allocate memory for scaled image data\n");
        return;
    }
    
    // Bind framebuffer to read from it
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    // Read pixels
    glReadPixels(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, scaled_data);
    
    // Unbind framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Save the final scaled image
    if (output_path) {
        save_ppm(output_path, scaled_data, WINDOW_WIDTH, WINDOW_HEIGHT);
    }
    
    // Free memory
    free(scaled_data);
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
    
    // After batch processing, display the result
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, output_texture);
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

// Handle X11 events
void handle_x11_events() {
    XEvent xev;
    while (XPending(x_display)) {
        XNextEvent(x_display, &xev);
        // Handle any relevant X11 events here
    }
}

// Main event loop
void run_event_loop() {
    printf("Scaling completed. Displaying result. Press Ctrl+C to exit.\n");
    while (1) {
        // Handle events for the appropriate display server
        switch (display_server_type) {
            case DISPLAY_WAYLAND:
                wl_display_dispatch_pending(wl_display);
                break;
                
            case DISPLAY_X11:
                handle_x11_events();
                break;
                
            default:
                // Should never reach here
                break;
        }
        
        draw_frame();
    }
}

void cleanup() {
    // Delete OpenGL resources
    glDeleteTextures(1, &texture_id);
    glDeleteTextures(1, &output_texture);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    
    // Destroy EGL resources
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    
    // Clean up display server resources
    switch (display_server_type) {
        case DISPLAY_WAYLAND:
            // Destroy Wayland resources
            wl_egl_window_destroy(wl_egl_window);
            wl_shell_surface_destroy(shell_surface);
            wl_surface_destroy(wl_surface);
            wl_shell_destroy(shell);
            wl_compositor_destroy(compositor);
            wl_display_disconnect(wl_display);
            break;
            
        case DISPLAY_X11:
            // Destroy X11 resources
            XFree(x_visual_info);
            XFreeColormap(x_display, x_colormap);
            XDestroyWindow(x_display, x_window);
            XCloseDisplay(x_display);
            break;
            
        default:
            // Should never reach here
            break;
    }
}

int main(int argc, char **argv) {
    // Check command line arguments
    const char* output_path = (argc == 3) ? argv[2] : NULL;
    
    // Initialize display (Wayland or X11)
    init_display();
    
    // Initialize OpenGL and load the image
    init_gl();
    
    // Perform batch scaling operations and measure performance
    batch_scaling(output_path);
    
    
    // Clean up resources (this will only be reached if run_event_loop() exits)
    cleanup();
    
    return EXIT_SUCCESS;
}
