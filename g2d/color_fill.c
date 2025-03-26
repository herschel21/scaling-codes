#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;

void initEGL() {
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl_display, NULL, NULL);

    EGLConfig config;
    EGLint numConfigs;
    EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,  // Using PBuffer for offscreen rendering
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    eglChooseConfig(egl_display, configAttribs, &config, 1, &numConfigs);

    EGLint pbufferAttribs[] = { EGL_WIDTH, 800, EGL_HEIGHT, 600, EGL_NONE };
    egl_surface = eglCreatePbufferSurface(egl_display, config, pbufferAttribs);

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
    initEGL();  // Initialize EGL

    for (int i = 0; i < 300; ++i) {  // Run for 5 seconds (~60 FPS)
        render();  
        usleep(16000);  
    }

    eglTerminate(egl_display);
    return 0;
}

