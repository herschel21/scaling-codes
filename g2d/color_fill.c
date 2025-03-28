#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    // EGL variables
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    
    // Native window (not used in this example, but needed for EGL)
    EGLNativeWindowType window = 0;

    // Initialize EGL
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        return 1;
    }

    // Initialize EGL
    if (eglInitialize(display, 0, 0) == EGL_FALSE) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return 1;
    }

    // Choose EGL configuration
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint num_configs;
    if (eglChooseConfig(display, config_attribs, &config, 1, &num_configs) == EGL_FALSE) {
        fprintf(stderr, "Failed to choose EGL config\n");
        eglTerminate(display);
        return 1;
    }

    // Create EGL context
    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
    if (context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        eglTerminate(display);
        return 1;
    }

    // Create EGL surface (using native window)
    surface = eglCreateWindowSurface(display, config, window, NULL);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        eglDestroyContext(display, context);
        eglTerminate(display);
        return 1;
    }

    // Bind the context to the current rendering thread
    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        fprintf(stderr, "Failed to make EGL context current\n");
        eglDestroySurface(display, surface);
        eglDestroyContext(display, context);
        eglTerminate(display);
        return 1;
    }

    // Clear screen to blue
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Swap buffers
    eglSwapBuffers(display, surface);

    // Wait for a few seconds
    sleep(5);

    // Clean up
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);

    return 0;
}
