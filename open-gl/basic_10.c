#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>  // For timing the scaling operations

#define WINDOW_WIDTH  1920  // Set your desired width
#define WINDOW_HEIGHT 1080  // Set your desired height
#define SCALING_ITERATIONS 100  // Number of times to perform scaling

// EGL globals
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

// Simple structure to hold image data
typedef struct {
    unsigned char* data;
    int width;
    int height;
    int channels; // Number of channels (e.g., 3 for RGB, 4 for RGBA)
} Image;

// Function to load a PPM image (P6 format - binary)
Image* load_ppm(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return NULL;
    }
    
    // Allocate image struct
    Image* img = (Image*)malloc(sizeof(Image));
    if (!img) {
        fclose(file);
        return NULL;
    }
    
    // Read PPM header
    char header[3];
    fread(header, 1, 3, file);
    if (header[0] != 'P' || header[1] != '6') {
        fprintf(stderr, "Not a valid P6 PPM file\n");
        free(img);
        fclose(file);
        return NULL;
    }
    
    // Skip comments
    int c = getc(file);
    while (c == '#') {
        while (getc(file) != '\n');
        c = getc(file);
    }
    ungetc(c, file);
    
    // Read width and height
    if (fscanf(file, "%d %d", &img->width, &img->height) != 2) {
        fprintf(stderr, "Invalid PPM file: could not read width/height\n");
        free(img);
        fclose(file);
        return NULL;
    }
    
    // Read max color value
    int maxval;
    if (fscanf(file, "%d", &maxval) != 1) {
        fprintf(stderr, "Invalid PPM file: could not read max color value\n");
        free(img);
        fclose(file);
        return NULL;
    }
    
    // Skip whitespace
    fgetc(file);
    
    // Set channels to 3 (RGB) for PPM
    img->channels = 3;
    
    // Allocate memory for image data
    img->data = (unsigned char*)malloc(img->width * img->height * img->channels);
    if (!img->data) {
        fprintf(stderr, "Could not allocate memory for image data\n");
        free(img);
        fclose(file);
        return NULL;
    }
    
    // Read image data
    if (fread(img->data, 1, img->width * img->height * img->channels, file) 
            != img->width * img->height * img->channels) {
        fprintf(stderr, "Error reading image data\n");
        free(img->data);
        free(img);
        fclose(file);
        return NULL;
    }
    
    fclose(file);
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
}

// Initialize texture with the loaded image
void init_texture(const char* image_path) {
    // Load the image
    Image* img = load_ppm(image_path);
    if (!img) {
        fprintf(stderr, "Failed to load image: %s\n", image_path);
        exit(EXIT_FAILURE);
    }
    
    printf("Loaded image: %dx%d with %d channels\n", img->width, img->height, img->channels);
    
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

// Initialize EGL for offscreen rendering
void init_egl_offscreen() {
    // Get default EGL display
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        exit(EXIT_FAILURE);
    }
    
    // Initialize EGL
    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        exit(EXIT_FAILURE);
    }
    
    printf("EGL version: %d.%d\n", major, minor);
    
    // Set up EGL configuration
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    
    EGLint num_configs;
    if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs)) {
        fprintf(stderr, "Failed to choose EGL config\n");
        exit(EXIT_FAILURE);
    }
    
    // Create a minimal pbuffer surface
    EGLint pbuffer_attribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    
    egl_surface = eglCreatePbufferSurface(egl_display, egl_config, pbuffer_attribs);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        exit(EXIT_FAILURE);
    }
    
    // Bind OpenGL ES API
    eglBindAPI(EGL_OPENGL_ES_API);
    
    // Create EGL context
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        exit(EXIT_FAILURE);
    }
    
    // Make the context current
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        exit(EXIT_FAILURE);
    }
    
    printf("EGL initialized successfully for offscreen rendering\n");
}

// Function to perform a single scaling operation
void perform_scaling() {
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
}

// Function to perform multiple scaling operations and track performance
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
    
    // If no output path specified, we're done
    if (!output_path) {
        printf("No output path specified, skipping save\n");
        return;
    }
    
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
    
    // Save the final scaled image
    save_ppm(output_path, scaled_data, WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // Free memory
    free(scaled_data);
}

void cleanup() {
    // Delete OpenGL resources
    glDeleteTextures(1, &texture_id);
    glDeleteTextures(1, &output_texture);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    
    // Destroy EGL resources
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_display, egl_surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    
    printf("Resources cleaned up\n");
}

int main(int argc, char **argv) {
    // Check command line arguments
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <input_image.ppm> [output_image.ppm]\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    const char* input_path = argv[1];
    const char* output_path = (argc == 3) ? argv[2] : NULL;
    
    // Initialize EGL for offscreen rendering
    init_egl_offscreen();
    
    // Initialize OpenGL resources
    program = init_shaders();
    if (!program) {
        fprintf(stderr, "Failed to initialize shaders\n");
        exit(EXIT_FAILURE);
    }
    
    // Initialize texture with loaded image
    init_texture(input_path);
    
    // Initialize framebuffer for offscreen rendering
    init_framebuffer();
    
    // Initialize geometry
    init_geometry();
    
    // Set viewport to target dimensions
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // Perform batch scaling operations and measure performance
    batch_scaling(output_path);
    
    // Clean up resources
    cleanup();
    
    printf("Offscreen rendering completed successfully\n");
    return EXIT_SUCCESS;
}
