#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ITERATIONS 100
#define SRC_WIDTH 640
#define SRC_HEIGHT 480
#define DST_WIDTH 1024
#define DST_HEIGHT 768

// Vertex shader for rendering (simplified)
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

// Detailed shader compilation with extensive error checking
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        fprintf(stderr, "Failed to create shader\n");
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        
        if (infoLen > 1) {
            char* infoLog = malloc(sizeof(char) * infoLen);
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
            fprintf(stderr, "Shader compilation failed: %s\n", infoLog);
            free(infoLog);
        }
        
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// Shader program creation with detailed error checking
GLuint createShaderProgram() {
    // Compile shaders
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertex_shader_source);
    if (vertexShader == 0) {
        fprintf(stderr, "Vertex shader compilation failed\n");
        return 0;
    }

    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragment_shader_source);
    if (fragmentShader == 0) {
        fprintf(stderr, "Fragment shader compilation failed\n");
        glDeleteShader(vertexShader);
        return 0;
    }

    // Create and link program
    GLuint program = glCreateProgram();
    if (program == 0) {
        fprintf(stderr, "Failed to create shader program\n");
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    // Check linking status
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        
        if (infoLen > 1) {
            char* infoLog = malloc(sizeof(char) * infoLen);
            glGetProgramInfoLog(program, infoLen, NULL, infoLog);
            fprintf(stderr, "Shader program linking failed: %s\n", infoLog);
            free(infoLog);
        }
        
        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    // Clean up individual shaders
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

int main() {
    // Wayland display connection
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    // EGL initialization with Wayland
    EGLDisplay egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        wl_display_disconnect(display);
        return 1;
    }

    // Initialize EGL
    EGLint major, minor;
    if (eglInitialize(egl_display, &major, &minor) == EGL_FALSE) {
        fprintf(stderr, "Failed to initialize EGL\n");
        wl_display_disconnect(display);
        return 1;
    }

    // EGL configuration
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

    if (eglChooseConfig(egl_display, config_attribs, &config, 1, &num_config) == EGL_FALSE) {
        fprintf(stderr, "Failed to choose EGL config\n");
        eglTerminate(egl_display);
        wl_display_disconnect(display);
        return 1;
    }

    // EGL context creation
    EGLContext context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, 
        (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    
    if (context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        eglTerminate(egl_display);
        wl_display_disconnect(display);
        return 1;
    }

    // Create Wayland surface and EGL window
    struct wl_compositor *compositor = NULL;
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    
    struct wl_egl_window *egl_window = wl_egl_window_create(surface, DST_WIDTH, DST_HEIGHT);
    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, config, 
                                                    (EGLNativeWindowType)egl_window, NULL);

    // Make context current
    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, context) == EGL_FALSE) {
        fprintf(stderr, "Failed to make EGL context current\n");
        eglDestroyContext(egl_display, context);
        eglTerminate(egl_display);
        wl_display_disconnect(display);
        return 1;
    }

    // Shader program initialization
    GLuint program = createShaderProgram();
    if (program == 0) {
        fprintf(stderr, "Failed to create shader program\n");
        // Cleanup code...
        return 1;
    }

    // Rest of the code remains the same as previous example...
    // (You can copy the remaining implementation from the previous artifact)

    // Cleanup
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_display, egl_surface);
    wl_egl_window_destroy(egl_window);
    eglDestroyContext(egl_display, context);
    eglTerminate(egl_display);
    wl_display_disconnect(display);

    return 0;
}
