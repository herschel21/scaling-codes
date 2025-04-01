#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>

// Simple vertex shader
const char* vertexShaderSource =
    "attribute vec4 aPosition;\n"
    "void main() {\n"
    "  gl_Position = aPosition;\n"
    "}\n";

// Simple fragment shader
const char* fragmentShaderSource =
    "precision mediump float;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"  // Red color
    "}\n";

// Compile shader and check for errors
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    // Check compilation status
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = (char*)malloc(sizeof(char) * infoLen);
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
            printf("Error compiling shader: %s\n", infoLog);
            free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

int main(int argc, char** argv) {
    // Get default EGL display
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        printf("Failed to get EGL display\n");
        return -1;
    }
    
    // Initialize EGL
    EGLint majorVersion, minorVersion;
    if (!eglInitialize(display, &majorVersion, &minorVersion)) {
        printf("Failed to initialize EGL\n");
        return -1;
    }
    
    printf("EGL Version: %d.%d\n", majorVersion, minorVersion);
    
    // Configure EGL
    EGLConfig config;
    EGLint numConfigs;
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) || numConfigs <= 0) {
        printf("Failed to choose EGL config\n");
        eglTerminate(display);
        return -1;
    }
    
    // Create a small pbuffer surface for rendering
    EGLint pbufferAttribs[] = {
        EGL_WIDTH, 800,
        EGL_HEIGHT, 480,
        EGL_NONE
    };
    
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        printf("Failed to create EGL surface\n");
        eglTerminate(display);
        return -1;
    }
    
    // Create OpenGL ES 2.0 context
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        printf("Failed to create EGL context\n");
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return -1;
    }
    
    // Make the context current
    if (!eglMakeCurrent(display, surface, surface, context)) {
        printf("Failed to make context current\n");
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return -1;
    }
    
    // Print OpenGL ES info
    printf("OpenGL ES Vendor: %s\n", glGetString(GL_VENDOR));
    printf("OpenGL ES Renderer: %s\n", glGetString(GL_RENDERER));
    printf("OpenGL ES Version: %s\n", glGetString(GL_VERSION));
    
    // Compile shaders
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
    if (!vertexShader || !fragmentShader) {
        printf("Failed to compile shaders\n");
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return -1;
    }
    
    // Create program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    // Check link status
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = (char*)malloc(sizeof(char) * infoLen);
            glGetProgramInfoLog(program, infoLen, NULL, infoLog);
            printf("Error linking program: %s\n", infoLog);
            free(infoLog);
        }
        glDeleteProgram(program);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        return -1;
    }
    
    // Define triangle vertices
    GLfloat vertices[] = {
        0.0f,  0.5f, 0.0f,  // top
       -0.5f, -0.5f, 0.0f,  // bottom left
        0.5f, -0.5f, 0.0f   // bottom right
    };
    
    // Set clear color and viewport
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, 800, 480);
    
    // Use the program
    glUseProgram(program);
    
    // Get position attribute location
    GLint positionLoc = glGetAttribLocation(program, "aPosition");
    glEnableVertexAttribArray(positionLoc);
    glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    
    // Clear and draw
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    // Present the rendered image
    eglSwapBuffers(display, surface);
    
    // Read a pixel to verify rendering (optional)
    GLubyte pixel[4];
    glReadPixels(400, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("Center pixel color: R=%d, G=%d, B=%d, A=%d\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    
    // Sleep to allow time for rendering
    printf("Rendering complete\n");
    
    // Cleanup
    glDeleteProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    
    return 0;
}
