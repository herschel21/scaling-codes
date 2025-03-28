#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
    // Wayland display connection
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    // Wayland registry
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    // Create a Wayland surface
    struct wl_compositor *compositor = NULL;
    struct wl_shell *shell = NULL;

    // Wayland registry listener (you'd typically implement this more robustly)
    struct wl_registry_listener registry_listener = {
        // These would normally be filled with callback functions to handle 
        // compositor and shell interfaces
    };
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // Sync Wayland display
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    // Check if we have necessary Wayland interfaces
    if (!compositor || !shell) {
        fprintf(stderr, "Failed to get Wayland compositor or shell\n");
        wl_display_disconnect(display);
        return 1;
    }

    // Create Wayland surface
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    struct wl_shell_surface *shell_surface = wl_shell_get_shell_surface(shell, surface);

    // EGL initialization
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
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig egl_config;
    EGLint num_configs;
    if (eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs) == EGL_FALSE) {
        fprintf(stderr, "Failed to choose EGL config\n");
        eglTerminate(egl_display);
        wl_display_disconnect(display);
        return 1;
    }

    // Create EGL context
    EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, NULL);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        eglTerminate(egl_display);
        wl_display_disconnect(display);
        return 1;
    }

    // Create EGL window surface
    struct wl_egl_window *egl_window = wl_egl_window_create(surface, 640, 480);
    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, egl_config, 
                                                    (EGLNativeWindowType)egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        wl_egl_window_destroy(egl_window);
        eglDestroyContext(egl_display, egl_context);
        eglTerminate(egl_display);
        wl_display_disconnect(display);
        return 1;
    }

    // Make EGL context current
    if (eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context) == EGL_FALSE) {
        fprintf(stderr, "Failed to make EGL context current\n");
        eglDestroySurface(egl_display, egl_surface);
        wl_egl_window_destroy(egl_window);
        eglDestroyContext(egl_display, egl_context);
        eglTerminate(egl_display);
        wl_display_disconnect(display);
        return 1;
    }

    // Clear screen to blue
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Swap buffers
    eglSwapBuffers(egl_display, egl_surface);

    // Wait for a few seconds
    sleep(5);

    // Cleanup
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_display, egl_surface);
    wl_egl_window_destroy(egl_window);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    wl_display_disconnect(display);

    return 0;
}
