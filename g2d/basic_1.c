#include <wayland-client.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

// Globals
struct wl_display *display;
EGLDisplay egl_display;
EGLContext egl_context;

void init_wayland() {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        exit(EXIT_FAILURE);
    }
}

void init_egl() {
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        exit(EXIT_FAILURE);
    }

    if (!eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        exit(EXIT_FAILURE);
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    static const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs);
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);

    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        exit(EXIT_FAILURE);
    }

    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context);
}

void print_gpu_info() {
    printf("OpenGL ES Version: %s\n", glGetString(GL_VERSION));
    printf("GLSL Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("GPU Vendor: %s\n", glGetString(GL_VENDOR));
    printf("GPU Renderer: %s\n", glGetString(GL_RENDERER));
}

int main() {
    init_wayland();
    init_egl();
    print_gpu_info();

    eglTerminate(egl_display);
    wl_display_disconnect(display);

    return 0;
}

