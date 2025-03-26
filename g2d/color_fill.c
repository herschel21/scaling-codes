#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

Display *x_display;
Window x_window;
EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;

void initX11Window(int width, int height) {
    x_display = XOpenDisplay(NULL);
    if (!x_display) {
        printf("Failed to open X display\n");
        exit(1);
    }

    int screen = DefaultScreen(x_display);
    x_window = XCreateSimpleWindow(x_display, RootWindow(x_display, screen), 10, 10, width, height, 1,
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

void render() {
    glClearColor(0.0, 0.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(egl_display, egl_surface);
}

int main() {
    initX11Window(800, 600);  // Create X11 window
    initEGL();                // Initialize EGL

    while (1) {
        render();  // Render frame
        usleep(16000);  // ~60 FPS
    }

    return 0;
}

