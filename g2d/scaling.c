#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <omp.h>

#define SRC_WIDTH 1024
#define SRC_HEIGHT 768
#define DST_WIDTH 1920
#define DST_HEIGHT 1080
#define PIXEL_SIZE 3  // RGB (3 bytes per pixel)
#define MAX_FRAMES 100  // Number of frames

// OpenGL Variables
Display *x_display;
Window x_window;
EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;
GLuint texture, shaderProgram, VBO;

// Timing Variables
struct timespec start, end;

// Function Prototypes
void initX11Window(int width, int height);
void initEGL();
void initOpenGL();
void updateTexture(unsigned char *src);
void render();
void startTimer();
void stopTimer();

// ✅ **Vertex Shader (Fixed)**
const char *vertexShaderSource =
    "attribute vec2 position;\n"
    "attribute vec2 texCoord;\n"
    "varying vec2 fragTexCoord;\n"
    "void main() {\n"
    "    fragTexCoord = texCoord;\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}";

// ✅ **Fragment Shader (Fixed)**
const char *fragmentShaderSource =
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "uniform sampler2D textureSampler;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(textureSampler, fragTexCoord);\n"
    "}";

// ✅ **Check and Print Shader Compilation Errors**
GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("Shader Compilation Error:\n%s\n", log);  // PRINT ERROR MESSAGE
        exit(1);
    }
    return shader;
}

// ✅ **Initialize X11 Window**
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

// ✅ **Initialize EGL**
void initEGL() {
    egl_display = eglGetDisplay((EGLNativeDisplayType)x_display);
    if (egl_display == EGL_NO_DISPLAY) {
        printf("EGL display failed\n");
        exit(1);
    }

    if (!eglInitialize(egl_display, NULL, NULL)) {
        printf("EGL initialization failed\n");
        exit(1);
    }

    printf("EGL initialized successfully\n");
}

// ✅ **Initialize OpenGL (Fixed)**
void initOpenGL() {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);

    // ✅ **Explicitly bind attributes to prevent location issues**
    glBindAttribLocation(shaderProgram, 0, "position");
    glBindAttribLocation(shaderProgram, 1, "texCoord");

    glLinkProgram(shaderProgram);
    glUseProgram(shaderProgram);

    GLfloat vertices[] = {
        // Position      // Texture Coordinates
        -1.0f, -1.0f,  0.0f, 1.0f, // Bottom Left
         1.0f, -1.0f,  1.0f, 1.0f, // Bottom Right
        -1.0f,  1.0f,  0.0f, 0.0f, // Top Left
         1.0f,  1.0f,  1.0f, 0.0f  // Top Right
    };

    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    printf("OpenGL initialized successfully\n");
}

// ✅ **Upload Texture to GPU (Fixed)**
void updateTexture(unsigned char *src) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, SRC_WIDTH, SRC_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, src);
    glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);
}

// ✅ **Render Frame**
void render() {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderProgram);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(egl_display, egl_surface);
}

// ✅ **Start Timer**
void startTimer() {
    clock_gettime(CLOCK_MONOTONIC, &start);
}

// ✅ **Stop Timer & Print FPS**
void stopTimer() {
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Total Time for %d frames: %.6f seconds\n", MAX_FRAMES, elapsed);
    printf("Average Time per Frame: %.6f seconds (%.2f FPS)\n", elapsed / MAX_FRAMES, MAX_FRAMES / elapsed);
}

// ✅ **Main Function**
int main() {
    initX11Window(DST_WIDTH, DST_HEIGHT);
    initEGL();
    initOpenGL();

    unsigned char *srcData = (unsigned char*)malloc(SRC_WIDTH * SRC_HEIGHT * PIXEL_SIZE);
    if (!srcData) {
        printf("Memory allocation failed\n");
        return 1;
    }

    // ✅ **Generate test image (random noise)**
    #pragma omp parallel for
    for (size_t i = 0; i < SRC_WIDTH * SRC_HEIGHT * PIXEL_SIZE; i++) {
        srcData[i] = rand() % 256;
    }

    startTimer();

    // ✅ **Run 100 frames**
    for (int frame = 0; frame < MAX_FRAMES; frame++) {
        printf("Frame %d\n", frame + 1);
        updateTexture(srcData);
        render();
        usleep(16000); // 60 FPS
    }

    stopTimer();

    free(srcData);
    return 0;
}

