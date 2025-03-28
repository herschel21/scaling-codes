#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ITERATIONS 100
#define SRC_WIDTH 640
#define SRC_HEIGHT 480
#define DST_WIDTH 1024
#define DST_HEIGHT 768

// Vertex shader for rendering
const char* vertex_shader_source = 
    "attribute vec2 position;"
    "attribute vec2 texCoord;"
    "varying vec2 v_texCoord;"
    "void main() {"
    "   gl_Position = vec4(position, 0.0, 1.0);"
    "   v_texCoord = texCoord;"
    "}";

// Fragment shader for nearest neighbor scaling
const char* fragment_shader_source = 
    "precision mediump float;"
    "uniform sampler2D texture;"
    "uniform vec2 srcResolution;"
    "uniform vec2 dstResolution;"
    "varying vec2 v_texCoord;"
    "void main() {"
    "   vec2 scaleFactor = srcResolution / dstResolution;"
    "   vec2 sourceTexCoord = floor(v_texCoord * dstResolution) * scaleFactor;"
    "   gl_FragColor = texture2D(texture, sourceTexCoord);"
    "}";

// Shader compilation utility
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    // Error checking
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, sizeof(infoLog), NULL, infoLog);
        fprintf(stderr, "Shader compilation failed: %s\n", infoLog);
        return 0;
    }

    return shader;
}

// Create and link shader program
GLuint createShaderProgram() {
    // Compile shaders
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragment_shader_source);

    // Create and link program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    // Error checking
    GLint success;
    GLchar infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, sizeof(infoLog), NULL, infoLog);
        fprintf(stderr, "Shader program linking failed: %s\n", infoLog);
        return 0;
    }

    // Clean up individual shaders
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

int main() {
    // EGL initialization (simplified)
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, 0, 0);

    // EGL configuration (simplified)
    EGLConfig config;
    EGLint num_config;
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    eglChooseConfig(display, config_attribs, &config, 1, &num_config);

    // Create EGL context
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    
    // Create surface (simplified, you'd typically use a Wayland or native window)
    EGLSurface surface = eglCreateWindowSurface(display, config, 0, NULL);
    
    // Make context current
    eglMakeCurrent(display, surface, surface, context);

    // Create shader program
    GLuint program = createShaderProgram();
    glUseProgram(program);

    // Generate source texture
    GLuint sourceTexture;
    glGenTextures(1, &sourceTexture);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    
    // Create source image data
    size_t sourceSize = SRC_WIDTH * SRC_HEIGHT * 4;
    unsigned char* sourceData = malloc(sourceSize);
    
    // Fill source data with random pixels
    for (size_t i = 0; i < sourceSize; i++) {
        sourceData[i] = rand() % 256;
    }

    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SRC_WIDTH, SRC_HEIGHT, 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, sourceData);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Get uniform locations
    GLint srcResolutionLoc = glGetUniformLocation(program, "srcResolution");
    GLint dstResolutionLoc = glGetUniformLocation(program, "dstResolution");

    // Vertex data for full-screen quad
    GLfloat vertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };

    // Create and bind VAO and VBO
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    GLint posAttrib = glGetAttribLocation(program, "position");
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(posAttrib);

    // Texture coordinate attribute
    GLint texCoordAttrib = glGetAttribLocation(program, "texCoord");
    glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(texCoordAttrib);

    // Prepare destination buffer
    size_t destSize = DST_WIDTH * DST_HEIGHT * 4;
    unsigned char* destBuffer = malloc(destSize);

    // Timing
    clock_t start = clock();

    // Perform scaling iterations
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        // Set uniforms
        glUniform2f(srcResolutionLoc, SRC_WIDTH, SRC_HEIGHT);
        glUniform2f(dstResolutionLoc, DST_WIDTH, DST_HEIGHT);

        // Render to FBO or default framebuffer
        glViewport(0, 0, DST_WIDTH, DST_HEIGHT);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Draw full-screen quad
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        // Read pixels (simulating write to memory)
        glReadPixels(0, 0, DST_WIDTH, DST_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, destBuffer);
    }

    // Calculate time
    clock_t end = clock();
    double total_time = (double)(end - start) / CLOCKS_PER_SEC;

    // Cleanup
    free(sourceData);
    free(destBuffer);
    glDeleteTextures(1, &sourceTexture);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(program);

    // EGL cleanup
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);

    printf("Completed %d read/scale/write operations in %.6f seconds\n", 
           MAX_ITERATIONS, total_time);

    return 0;
}
