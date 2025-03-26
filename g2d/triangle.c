#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Global variables
Display *x_display;
Window x_window;
EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;
GLuint shaderProgram, VAO, VBO;

// Vertex Shader source
const char *vertexShaderSource =
    "attribute vec3 position;\n"
    "attribute vec3 color;\n"
    "varying vec3 fragColor;\n"
    "void main() {\n"
    "    fragColor = color;\n"
    "    gl_Position = vec4(position, 1.0);\n"
    "}";

// Fragment Shader source
const char *fragmentShaderSource =
    "precision mediump float;\n"
    "varying vec3 fragColor;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(fragColor, 1.0);\n"
    "}";

void initX11Window(int width, int height) {
    x_display = XOpenDisplay(NULL);
    if (!x_display) {
        printf("Failed to open X display\n");
        exit(1);
    }

    int screen = DefaultScreen(x_display);
    x_window = XCreateSimpleWindow(x_display, RootWindow(x_display, screen),
                                   10, 10, width, height, 1,
                                   BlackPixel(x_display, screen), WhitePixel(x_display, screen));
    XMapWindow(x_display, x_window);
}

void initEGL() {
    egl_display = eglGetDisplay((EGLNativeDisplayType)x_display);
    eglInitialize(egl_display, NULL, NULL);

    EGLConfig config;
    EGLint numConfigs;
    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    eglChooseConfig(egl_display, configAttribs, &config, 1, &numConfigs);
    egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)x_window, NULL);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        printf("Shader Compilation Error:\n%s\n", log);
        exit(1);
    }
    return shader;
}

void initOpenGL() {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glUseProgram(shaderProgram);

    // Triangle vertex data (positions + colors)
    GLfloat vertices[] = {
        // Position         // Color
         0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // Top (Red)
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // Left (Green)
         0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // Right (Blue)
    };

    GLuint VBO;
    glGenBuffers(1, &VBO);
    glGenVertexArrays(1, &VAO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint posAttrib = glGetAttribLocation(shaderProgram, "position");
    glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)0);
    glEnableVertexAttribArray(posAttrib);

    GLint colAttrib = glGetAttribLocation(shaderProgram, "color");
    glVertexAttribPointer(colAttrib, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
    glEnableVertexAttribArray(colAttrib);
}

void render() {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderProgram);
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    eglSwapBuffers(egl_display, egl_surface);
}

int main() {
    initX11Window(800, 600);
    initEGL();
    initOpenGL();

    while (1) {
        render();
        usleep(16000);  // ~60 FPS
    }

    return 0;
}

