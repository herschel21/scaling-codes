#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

// Shader source code
const char *vertexShaderSource = 
    "attribute vec3 aPosition;\n"
    "uniform mat4 uMVP;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

const char *fragmentShaderSource = 
    "precision mediump float;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n" // Red triangle
    "}\n";

// Simple 4x4 matrix for MVP (Model-View-Projection)
void createMVPMatrix(GLfloat *matrix, GLfloat angle) {
    // Simple rotation around Y-axis and orthographic projection
    GLfloat cosA = cos(angle);
    GLfloat sinA = sin(angle);
    GLfloat mvp[16] = {
        cosA, 0.0f, -sinA, 0.0f, // Model rotation
        0.0f, 1.0f, 0.0f,  0.0f,
        sinA, 0.0f, cosA,  0.0f,
        0.0f, 0.0f, 0.0f,  1.0f
    };
    for (int i = 0; i < 16; i++) matrix[i] = mvp[i];
}

int main() {
    // EGL setup
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    EGLint numConfigs;

    // Initialize EGL
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        printf("Failed to get EGL display\n");
        return -1;
    }

    eglInitialize(display, NULL, NULL);

    // Choose EGL configuration
    static const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);
    if (numConfigs <= 0) {
        printf("No matching EGL configs\n");
        eglTerminate(display);
        return -1;
    }

    // Create a native window (assuming framebuffer /dev/fb0)
    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL); // 0 for fb0
    if (surface == EGL_NO_SURFACE) {
        printf("Failed to create EGL surface\n");
        eglTerminate(display);
        return -1;
    }

    // Create EGL context
    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        printf("Failed to create EGL context\n");
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return -1;
    }

    eglMakeCurrent(display, surface, surface, context);

    // Compile shaders
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        printf("Vertex shader compilation failed\n");
        goto cleanup;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        printf("Fragment shader compilation failed\n");
        goto cleanup;
    }

    // Link program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        printf("Program linking failed\n");
        goto cleanup;
    }

    // Get attribute and uniform locations
    GLint posLocation = glGetAttribLocation(program, "aPosition");
    GLint mvpLocation = glGetUniformLocation(program, "uMVP");

    // Define a simple triangle
    GLfloat vertices[] = {
         0.0f,  0.5f, 0.0f, // Top
        -0.5f, -0.5f, 0.0f, // Bottom-left
         0.5f, -0.5f, 0.0f  // Bottom-right
    };

    // Set up VBO
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Main rendering loop
    glViewport(0, 0, 640, 480); // Set viewport size
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Black background
    GLfloat angle = 0.0f;

    while (1) {
        glClear(GL_COLOR_BUFFER_BIT);

        // Use the shader program
        glUseProgram(program);

        // Update MVP matrix with rotation
        GLfloat mvp[16];
        createMVPMatrix(mvp, angle);
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, mvp);

        // Set vertex data
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(posLocation);
        glVertexAttribPointer(posLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);

        // Draw the triangle
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Swap buffers
        eglSwapBuffers(display, surface);

        // Increment angle for rotation
        angle += 0.05f;
        if (angle > 2 * M_PI) angle -= 2 * M_PI;

        usleep(16666); // ~60 FPS
    }

cleanup:
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    return 0;
}
