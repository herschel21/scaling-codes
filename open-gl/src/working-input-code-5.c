#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#define TARGET_FPS 60
#define FRAME_DURATION (1.0 / TARGET_FPS) // 16.67ms
#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080
#define FRAME_BUFFER_SIZE 8

typedef enum { DISPLAY_WAYLAND, DISPLAY_X11, DISPLAY_UNKNOWN } DisplayServerType;

DisplayServerType display_server_type = DISPLAY_UNKNOWN;

// Video decoding globals
AVFormatContext* format_context = NULL;
AVCodecContext* codec_context = NULL;
AVFrame* av_frame = NULL;
AVFrame* rgba_frame = NULL;
AVPacket* packet = NULL;
struct SwsContext* sws_context = NULL;
int video_stream_index = -1;
uint8_t* rgb_buffer = NULL;
int rgb_buffer_size = 0;
int frame_width = 0, frame_height = 0;

// Display globals
struct wl_display *wl_display;
struct wl_compositor *compositor;
struct wl_surface *wl_surface;
struct wl_egl_window *wl_egl_window;
struct wl_shell *shell;
struct wl_shell_surface *shell_surface;
Display *x_display = NULL;
Window x_window;
Colormap x_colormap;
XVisualInfo *x_visual_info;

// EGL globals
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
EGLConfig egl_config;

// OpenGL globals
GLuint texture_id, program, vbo;
int running = 1, decoding_done = 0;

// Frame buffer
typedef struct {
    uint8_t* data;
    int size;
} FrameBuffer;

FrameBuffer frame_buffer[FRAME_BUFFER_SIZE];
int frame_buffer_head = 0, frame_buffer_tail = 0, frame_buffer_count = 0;
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_cond = PTHREAD_COND_INITIALIZER;
pthread_t decode_thread;

// FPS tracking
double last_fps_time = 0.0;
int frame_count = 0;
float current_fps = 0.0;
double total_time = 0.0;
int total_frames = 0;

// Shader sources
const char *vertex_shader_source =
    "attribute vec3 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 1.0);\n"
    "  v_texcoord = texcoord;\n"
    "}\n";

const char *fragment_shader_source_rgba =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(texture, v_texcoord);\n"
    "}\n";

// Wayland registry handlers
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    printf("DEBUG: Registry global - interface: %s\n", interface);
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    printf("DEBUG: Registry global remove - name: %u\n", name);
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

// Detect display server
DisplayServerType detect_display_server() {
    struct wl_display *test_display = wl_display_connect(NULL);
    if (test_display) {
        printf("DEBUG: Detected Wayland display server\n");
        wl_display_disconnect(test_display);
        return DISPLAY_WAYLAND;
    }
    Display *test_x_display = XOpenDisplay(NULL);
    if (test_x_display) {
        printf("DEBUG: Detected X11 display server\n");
        XCloseDisplay(test_x_display);
        return DISPLAY_X11;
    }
    printf("DEBUG: No supported display server detected\n");
    return DISPLAY_UNKNOWN;
}

// Initialize MP4
int init_mp4_file(const char* filename) {
    printf("DEBUG: Initializing MP4 file: %s\n", filename);
    int ret = avformat_open_input(&format_context, filename, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "DEBUG: Failed to open MP4 file\n");
        return -1;
    }

    avformat_find_stream_info(format_context, NULL);
    for (int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            printf("DEBUG: Video stream found at index: %d\n", i);
            break;
        }
    }
    if (video_stream_index == -1) return -1;

    const AVCodec* codec = avcodec_find_decoder(format_context->streams[video_stream_index]->codecpar->codec_id);
    codec_context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context, format_context->streams[video_stream_index]->codecpar);
    ret = avcodec_open2(codec_context, codec, NULL);
    if (ret < 0) return -1;

    av_frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    if (!av_frame || !rgba_frame) return -1;

    frame_width = codec_context->width;
    frame_height = codec_context->height;
    rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame_width, frame_height, 1);
    rgb_buffer = av_malloc(rgb_buffer_size);
    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgb_buffer, AV_PIX_FMT_RGBA, frame_width, frame_height, 1);

    sws_context = sws_getContext(frame_width, frame_height, codec_context->pix_fmt,
                                 frame_width, frame_height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_context) return -1;

    packet = av_packet_alloc();
    if (!packet) return -1;

    printf("DEBUG: MP4 initialized - %dx%d\n", frame_width, frame_height);
    return 0;
}

// Decoding thread
void* decode_thread_func(void* arg) {
    printf("DEBUG: Starting decode thread\n");
    while (running && !decoding_done) {
        pthread_mutex_lock(&buffer_mutex);
        while (frame_buffer_count >= FRAME_BUFFER_SIZE && running) {
            printf("DEBUG: Buffer full, waiting\n");
            pthread_cond_wait(&buffer_cond, &buffer_mutex);
        }
        pthread_mutex_unlock(&buffer_mutex);

        if (!running) break;

        int ret = av_read_frame(format_context, packet);
        if (ret < 0) {
            printf("DEBUG: End of video reached\n");
            decoding_done = 1;
            break;
        }

        if (packet->stream_index == video_stream_index) {
            avcodec_send_packet(codec_context, packet);
            if (avcodec_receive_frame(codec_context, av_frame) == 0) {
                sws_scale(sws_context, (const uint8_t* const*)av_frame->data, av_frame->linesize, 0,
                          codec_context->height, rgba_frame->data, rgba_frame->linesize);

                pthread_mutex_lock(&buffer_mutex);
                frame_buffer[frame_buffer_tail].data = malloc(rgb_buffer_size);
                memcpy(frame_buffer[frame_buffer_tail].data, rgb_buffer, rgb_buffer_size);
                frame_buffer[frame_buffer_tail].size = rgb_buffer_size;
                frame_buffer_tail = (frame_buffer_tail + 1) % FRAME_BUFFER_SIZE;
                frame_buffer_count++;
                printf("DEBUG: Frame decoded and buffered - count: %d\n", frame_buffer_count);
                pthread_cond_signal(&buffer_cond);
                pthread_mutex_unlock(&buffer_mutex);
            }
        }
        av_packet_unref(packet);
    }
    printf("DEBUG: Decode thread exiting\n");
    return NULL;
}

// Get next frame
int get_next_frame(uint8_t** frame_ptr) {
    pthread_mutex_lock(&buffer_mutex);
    while (frame_buffer_count == 0 && !decoding_done) {
        printf("DEBUG: Buffer empty, waiting\n");
        pthread_cond_wait(&buffer_cond, &buffer_mutex);
    }
    if (frame_buffer_count == 0 && decoding_done) {
        pthread_mutex_unlock(&buffer_mutex);
        printf("DEBUG: No more frames available\n");
        return -1;
    }

    *frame_ptr = frame_buffer[frame_buffer_head].data;
    int size = frame_buffer[frame_buffer_head].size;
    frame_buffer_head = (frame_buffer_head + 1) % FRAME_BUFFER_SIZE;
    frame_buffer_count--;
    printf("DEBUG: Frame retrieved - count: %d\n", frame_buffer_count);
    pthread_cond_signal(&buffer_cond);
    pthread_mutex_unlock(&buffer_mutex);
    return size;
}

// Compile shader
GLuint compile_shader(GLenum type, const char *source) {
    printf("DEBUG: Compiling %s shader\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment");
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        fprintf(stderr, "DEBUG: Shader compilation failed\n");
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Initialize shaders
GLuint init_shaders() {
    printf("DEBUG: Initializing shaders\n");
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source_rgba);
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        fprintf(stderr, "DEBUG: Shader linking failed\n");
        glDeleteProgram(program);
        return 0;
    }
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    printf("DEBUG: Shaders initialized\n");
    return program;
}

// Initialize geometry
void init_geometry() {
    printf("DEBUG: Initializing geometry\n");
    float vertices[] = {
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f
    };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
}

// Initialize video texture
void init_video_texture() {
    printf("DEBUG: Initializing video texture\n");
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

// Initialize Wayland
int init_wayland() {
    printf("DEBUG: Initializing Wayland\n");
    wl_display = wl_display_connect(NULL);
    if (!wl_display) return -1;

    struct wl_registry *registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_dispatch(wl_display);
    wl_display_roundtrip(wl_display);

    if (!compositor || !shell) return -1;

    wl_surface = wl_compositor_create_surface(compositor);
    if (!wl_surface) return -1;

    shell_surface = wl_shell_get_shell_surface(shell, wl_surface);
    if (!shell_surface) return -1;

    wl_shell_surface_set_toplevel(shell_surface);
    wl_egl_window = wl_egl_window_create(wl_surface, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!wl_egl_window) return -1;

    printf("DEBUG: Wayland initialized\n");
    return 0;
}

// Initialize X11
int init_x11() {
    printf("DEBUG: Initializing X11\n");
    x_display = XOpenDisplay(NULL);
    if (!x_display) return -1;

    int screen = DefaultScreen(x_display);
    Window root = RootWindow(x_display, screen);

    XVisualInfo visTemplate = {.screen = screen};
    int num_visuals;
    x_visual_info = XGetVisualInfo(x_display, VisualScreenMask, &visTemplate, &num_visuals);
    if (!x_visual_info) return -1;

    x_colormap = XCreateColormap(x_display, root, x_visual_info->visual, AllocNone);
    XSetWindowAttributes attr = {.colormap = x_colormap, .event_mask = ExposureMask | KeyPressMask | StructureNotifyMask};
    x_window = XCreateWindow(x_display, root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
                             x_visual_info->depth, InputOutput, x_visual_info->visual,
                             CWColormap | CWEventMask, &attr);

    XStoreName(x_display, x_window, "Video Player");
    XMapWindow(x_display, x_window);
    XFlush(x_display);

    XWindowAttributes window_attributes;
    do {
        XGetWindowAttributes(x_display, x_window, &window_attributes);
    } while (window_attributes.map_state != IsViewable);

    printf("DEBUG: X11 initialized\n");
    return 0;
}

// Initialize EGL
int init_egl() {
    printf("DEBUG: Initializing EGL\n");
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };
    EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    egl_display = (display_server_type == DISPLAY_WAYLAND) ?
        eglGetDisplay((EGLNativeDisplayType)wl_display) :
        eglGetDisplay((EGLNativeDisplayType)x_display);
    if (egl_display == EGL_NO_DISPLAY) return -1;

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) return -1;

    EGLint count;
    eglGetConfigs(egl_display, NULL, 0, &count);
    EGLConfig *configs = malloc(count * sizeof(EGLConfig));
    eglChooseConfig(egl_display, config_attribs, configs, count, &count);
    egl_config = configs[0];
    free(configs);

    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) return -1;

    egl_surface = (display_server_type == DISPLAY_WAYLAND) ?
        eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)wl_egl_window, NULL) :
        eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)x_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) return -1;

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) return -1;
    printf("DEBUG: EGL initialized\n");
    return 0;
}

// Render loop with FPS tracking
void render_loop() {
    printf("DEBUG: Starting render loop\n");
    struct timespec start_time, end_time, init_time;
    double elapsed, frame_time = FRAME_DURATION;
    uint8_t* frame;

    clock_gettime(CLOCK_MONOTONIC, &init_time);
    last_fps_time = init_time.tv_sec + init_time.tv_nsec / 1e9;

    glUseProgram(program);
    GLint pos_attrib = glGetAttribLocation(program, "position");
    GLint tex_attrib = glGetAttribLocation(program, "texcoord");
    GLint tex_uniform = glGetUniformLocation(program, "texture");
    glUniform1i(tex_uniform, 0);

    while (running) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        if (display_server_type == DISPLAY_X11) {
            while (XPending(x_display)) {
                XEvent xev;
                XNextEvent(x_display, &xev);
                if (xev.type == KeyPress) {
                    printf("DEBUG: Keypress detected, stopping\n");
                    running = 0;
                }
            }
        } else if (display_server_type == DISPLAY_WAYLAND) {
            wl_display_dispatch_pending(wl_display);
            printf("DEBUG: Wayland events dispatched\n");
        }

        int frame_size = get_next_frame(&frame);
        if (frame_size < 0) {
            running = 0;
            break;
        }

        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame_width, frame_height, GL_RGBA, GL_UNSIGNED_BYTE, frame);
        free(frame);
        printf("DEBUG: Texture updated\n");

        glClear(GL_COLOR_BUFFER_BIT);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(pos_attrib);
        glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
        glEnableVertexAttribArray(tex_attrib);
        glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(pos_attrib);
        glDisableVertexAttribArray(tex_attrib);
        printf("DEBUG: Frame rendered\n");

        eglSwapBuffers(egl_display, egl_surface);
        printf("DEBUG: Buffers swapped\n");

        frame_count++;
        total_frames++;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
        total_time += elapsed;

        double current_time = end_time.tv_sec + end_time.tv_nsec / 1e9;
        if (current_time - last_fps_time >= 1.0) {
            current_fps = frame_count / (current_time - last_fps_time);
            printf("DEBUG: Current FPS: %.1f\n", current_fps);
            frame_count = 0;
            last_fps_time = current_time;
        }

        if (elapsed < frame_time) {
            usleep((frame_time - elapsed) * 1e6);
            printf("DEBUG: Slept for %.3f ms\n", (frame_time - elapsed) * 1000);
        } else if (elapsed > frame_time * 2) {
            printf("DEBUG: Frame dropped, took %.3f ms\n", elapsed * 1000);
        }
    }

    double avg_fps = total_frames / total_time;
    printf("DEBUG: Render loop ended\n");
    printf("DEBUG: Total frames: %d, Total time: %.2f s, Average FPS: %.1f\n", total_frames, total_time, avg_fps);
}

// Cleanup functions
void cleanup_video_source() {
    printf("DEBUG: Cleaning up video source\n");
    if (packet) av_packet_free(&packet);
    if (rgba_frame) av_frame_free(&rgba_frame);
    if (av_frame) av_frame_free(&av_frame);
    if (codec_context) {
        avcodec_close(codec_context);
        avcodec_free_context(&codec_context);
    }
    if (format_context) avformat_close_input(&format_context);
    if (sws_context) sws_freeContext(sws_context);
    if (rgb_buffer) av_free(rgb_buffer);
    for (int i = 0; i < FRAME_BUFFER_SIZE; i++) {
        if (frame_buffer[i].data) free(frame_buffer[i].data);
    }
}

void cleanup_display() {
    printf("DEBUG: Cleaning up display\n");
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) eglDestroyContext(egl_display, egl_context);
        if (egl_surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
    }
    if (display_server_type == DISPLAY_WAYLAND) {
        if (wl_egl_window) wl_egl_window_destroy(wl_egl_window);
        if (shell_surface) wl_shell_surface_destroy(shell_surface);
        if (wl_surface) wl_surface_destroy(wl_surface);
        if (shell) wl_shell_destroy(shell);
        if (compositor) wl_compositor_destroy(compositor);
        if (wl_display) wl_display_disconnect(wl_display);
    } else if (display_server_type == DISPLAY_X11) {
        if (x_colormap) XFreeColormap(x_display, x_colormap);
        if (x_visual_info) XFree(x_visual_info);
        if (x_window) XDestroyWindow(x_display, x_window);
        if (x_display) XCloseDisplay(x_display);
    }
}

void cleanup_gl() {
    printf("DEBUG: Cleaning up GL\n");
    if (texture_id) glDeleteTextures(1, &texture_id);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (program) glDeleteProgram(program);
}

// Main function
int main(int argc, char *argv[]) {
    printf("DEBUG: Program started\n");
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <video_file.mp4>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (init_mp4_file(argv[1]) < 0) {
        fprintf(stderr, "DEBUG: Failed to open MP4 file\n");
        return EXIT_FAILURE;
    }

    display_server_type = detect_display_server();
    if (display_server_type == DISPLAY_UNKNOWN) {
        cleanup_video_source();
        return EXIT_FAILURE;
    }

    if (display_server_type == DISPLAY_WAYLAND) {
        if (init_wayland() < 0) {
            cleanup_video_source();
            return EXIT_FAILURE;
        }
    } else if (display_server_type == DISPLAY_X11) {
        if (init_x11() < 0) {
            cleanup_video_source();
            return EXIT_FAILURE;
        }
    }

    if (init_egl() < 0) {
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    program = init_shaders();
    if (!program) {
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    init_geometry();
    init_video_texture();

    pthread_create(&decode_thread, NULL, decode_thread_func, NULL);
    render_loop();

    running = 0;
    pthread_join(decode_thread, NULL);
    cleanup_gl();
    cleanup_video_source();
    cleanup_display();

    printf("DEBUG: Program terminated\n");
    return EXIT_SUCCESS;
}
