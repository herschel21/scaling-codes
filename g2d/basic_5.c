#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <string.h>

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
GLuint texture;

// Vertex and fragment shaders for rendering the texture
const char *vertex_shader_src = R"(
    attribute vec4 a_position;
    attribute vec2 a_texCoord;
    varying vec2 v_texCoord;
    void main() {
        gl_Position = a_position;
        v_texCoord = a_texCoord;
    }
)";

const char *fragment_shader_src = R"(
    precision mediump float;
    uniform sampler2D u_texture;
    varying vec2 v_texCoord;
    void main() {
        gl_FragColor = texture2D(u_texture, v_texCoord);
    }
)";

// Shader compilation function
GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        printf("Shader compile error: %s\n", log);
    }

    return shader;
}

// Program linking function
GLuint create_program(const char *vertex_src, const char *fragment_src) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, NULL, log);
        printf("Program link error: %s\n", log);
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}

// Load BMP image (basic uncompressed BMP format)
GLuint load_bmp_image(const char *image_path, int *width, int *height) {
    FILE *file = fopen(image_path, "rb");
    if (!file) {
        printf("Failed to open image file %s\n", image_path);
        return 0;
    }

    // Read the BMP header
    fseek(file, 18, SEEK_SET);
    fread(width, sizeof(int), 1, file);
    fread(height, sizeof(int), 1, file);

    fseek(file, 54, SEEK_SET);  // Skip the BMP header

    int size = (*width) * (*height) * 3;  // 3 bytes per pixel for RGB
    unsigned char *data = (unsigned char *)malloc(size);
    fread(data, 1, size, file);
    fclose(file);

    // Convert RGB to RGBA (add alpha channel)
    unsigned char *rgba_data = (unsigned char *)malloc(*width * *height * 4);
    for (int i = 0; i < *width * *height; ++i) {
        rgba_data[i * 4 + 0] = data[i * 3 + 0];  // Red
        rgba_data[i * 4 + 1] = data[i * 3 + 1];  // Green
        rgba_data[i * 4 + 2] = data[i * 3 + 2];  // Blue
        rgba_data[i * 4 + 3] = 255;               // Alpha (fully opaque)
    }

    free(data);

    // Generate OpenGL texture
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);

    free(rgba_data);
    return texture_id;
}

// Initialize EGL and OpenGL ES context
void init_egl() {
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl_display, NULL, NULL);

    EGLConfig config;
    EGLint num_configs;
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs);

    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, NULL);
    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType) NULL, NULL);

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

// Main rendering loop: Scaling the image
void render(int target_width, int target_height) {
    GLfloat vertices[] = {
        -1.0f,  1.0f, 0.0f, 1.0f, // top left
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom left
         1.0f,  1.0f, 1.0f, 1.0f, // top right
         1.0f, -1.0f, 1.0f, 0.0f  // bottom right
    };

    GLuint program = create_program(vertex_shader_src, fragment_shader_src);
    glUseProgram(program);

    GLuint position_loc = glGetAttribLocation(program, "a_position");
    GLuint texcoord_loc = glGetAttribLocation(program, "a_texCoord");
    GLuint texture_loc = glGetUniformLocation(program, "u_texture");

    GLuint vbo, vao;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(position_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(position_loc);

    glVertexAttribPointer(texcoord_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid*)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(texcoord_loc);

    int width, height;
    texture = load_bmp_image("input_image.bmp", &width, &height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(texture_loc, 0);  // Texture unit 0

    glViewport(0, 0, target_width, target_height); // Set the viewport to the target resolution

    glClear(GL_COLOR_BUFFER_BIT);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    eglSwapBuffers(egl_display, egl_surface);
}

// Main function
int main() {
    init_egl();

    for (int i = 0; i < 100; i++) {
        int target_width = 640;  // Example target width
        int target_height = 480; // Example target height
        render(target_width, target_height);  // Scale the image and render to the new resolution
        printf("Scaled image %d rendered\n", i);
    }

    return 0;
}

