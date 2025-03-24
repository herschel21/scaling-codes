#include <EGL/egl.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Function to check if OpenGL is using GPU or CPU rendering
void checkGPU() {
    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);

    printf("OpenGL Vendor  : %s\n", vendor);
    printf("OpenGL Renderer: %s\n", renderer);
    printf("OpenGL Version : %s\n", version);

    if (strstr((const char *)renderer, "llvmpipe") || strstr((const char *)renderer, "softpipe")) {
        printf("Software rendering detected (CPU). OpenGL is not using the GPU.\n");
    } else {
        printf("GPU is being used for OpenGL rendering.\n");
    }
}

int main() {
    // Initialize EGL display connection
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        printf("Failed to get EGL display\n");
        return -1;
    }

    if (!eglInitialize(display, NULL, NULL)) {
        printf("Failed to initialize EGL\n");
        return -1;
    }

    // Choose an EGL configuration
    EGLConfig config;
    EGLint numConfigs;
    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs)) {
        printf("Failed to choose EGL config\n");
        return -1;
    }

    // Create an EGL context
    eglBindAPI(EGL_OPENGL_API);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if (context == EGL_NO_CONTEXT) {
        printf("Failed to create EGL context\n");
        return -1;
    }

    // Create a PBuffer surface (off-screen rendering)
    static const EGLint pbufferAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        printf("Failed to create EGL surface\n");
        return -1;
    }

    // Make the context current
    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("Failed to make EGL context current\n");
        return -1;
    }

    // Check GPU or CPU rendering
    checkGPU();

    // Cleanup
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    
    return 0;
}

