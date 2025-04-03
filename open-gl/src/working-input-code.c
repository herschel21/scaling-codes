#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/eglplatform.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#define WINDOW_WIDTH  640
#define WINDOW_HEIGHT 480
#define BUFFER_COUNT  4
#define FRAME_BUFFER_SIZE 100
#define STRINGIFY(x) #x

typedef enum {
    DISPLAY_WAYLAND,
    DISPLAY_X11,
    DISPLAY_UNKNOWN
} DisplayServerType;

typedef enum {
    VIDEO_SOURCE_FILE,
    VIDEO_SOURCE_CAMERA,
    VIDEO_SOURCE_MP4,
    VIDEO_SOURCE_NONE
} VideoSourceType;

DisplayServerType display_server_type = DISPLAY_UNKNOWN;
VideoSourceType video_source_type = VIDEO_SOURCE_NONE;

int video_fd = -1;
struct v4l2_buffer v4l2_buffers[BUFFER_COUNT];
void* buffer_start[BUFFER_COUNT];
size_t buffer_length[BUFFER_COUNT];
int current_buffer = 0;
int frame_width = 0;
int frame_height = 0;
int video_format = 0;
double frame_duration = 0.033;  // Default to 30 FPS, updated for MP4

FILE* video_file = NULL;
unsigned char* frame_data = NULL;

AVFormatContext* format_context = NULL;
AVCodecContext* codec_context = NULL;
AVFrame* av_frame = NULL;
AVFrame* rgba_frame = NULL;
AVPacket* packet = NULL;
struct SwsContext* sws_context = NULL;
int video_stream_index = -1;
uint8_t* rgb_buffer = NULL;
int rgb_buffer_size = 0;

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

EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
EGLConfig egl_config;

GLuint texture_id;
GLuint program;
GLuint vbo;
GLuint framebuffer;
GLuint output_texture;
int running = 1;

const char *vertex_shader_source =
    "attribute vec3 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 1.0);\n"
    "  v_texcoord = texcoord;\n"
    "}\n";

const char *fragment_shader_source_yuyv =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture;\n"
    "void main() {\n"
    "  float tex_x = v_texcoord.x * 0.5;\n"
    "  vec4 yuyv = texture2D(texture, vec2(tex_x, v_texcoord.y));\n"
    "  float y, u, v;\n"
    "  if (mod(floor(v_texcoord.x * 1920.0), 2.0) == 0.0) {\n"
    "    y = yuyv.r;\n"
    "    u = yuyv.g;\n"
    "    v = yuyv.a;\n"
    "  } else {\n"
    "    y = yuyv.b;\n"
    "    u = yuyv.g;\n"
    "    v = yuyv.a;\n"
    "  }\n"
    "  float r = y + 1.402 * (v - 0.5);\n"
    "  float g = y - 0.344 * (u - 0.5) - 0.714 * (v - 0.5);\n"
    "  float b = y + 1.772 * (u - 0.5);\n"
    "  r = clamp(r, 0.0, 1.0);\n"
    "  g = clamp(g, 0.0, 1.0);\n"
    "  b = clamp(b, 0.0, 1.0);\n"
    "  gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

const char *fragment_shader_source_rgba =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(texture, v_texcoord);\n"
    "}\n";

typedef struct {
    unsigned char* data;
    int size;
} FrameBuffer;

FrameBuffer frame_buffer[FRAME_BUFFER_SIZE];
int frame_buffer_head = 0;
int frame_buffer_tail = 0;
int frame_buffer_count = 0;

// Function prototypes
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);
DisplayServerType detect_display_server();
unsigned char* convert_rgb_to_rgba(unsigned char* rgb_data, int width, int height);
int init_camera(const char* device);
int init_mp4_file(const char* filename);
int open_video_file(const char* filename);
int get_next_camera_frame(unsigned char** frame_ptr);
void release_camera_frame();
int get_next_mp4_frame(unsigned char** frame_ptr);
int get_buffered_frame(unsigned char** frame_ptr);
void prefill_frame_buffer();
int get_next_video_file_frame(unsigned char** frame_ptr);
int get_next_frame(unsigned char** frame_ptr);
GLuint compile_shader(GLenum type, const char *source);
GLuint init_shaders();
void init_framebuffer();
void init_video_texture();
void init_geometry();
int init_wayland();
int init_x11();
int init_egl();
void cleanup_video_source();
void cleanup_display();
void cleanup_gl();
void render_loop();

// Registry handlers (unchanged)
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

// Detect display server (unchanged)
DisplayServerType detect_display_server() {
    struct wl_display *test_display = wl_display_connect(NULL);
    if (test_display) {
        printf("Wayland display server detected\n");
        wl_display_disconnect(test_display);
        return DISPLAY_WAYLAND;
    }
    
    Display *test_x_display = XOpenDisplay(NULL);
    if (test_x_display) {
        printf("X11 display server detected\n");
        XCloseDisplay(test_x_display);
        return DISPLAY_X11;
    }
    
    printf("No supported display server detected\n");
    return DISPLAY_UNKNOWN;
}

// RGB to RGBA conversion (unchanged)
unsigned char* convert_rgb_to_rgba(unsigned char* rgb_data, int width, int height) {
    unsigned char* rgba = (unsigned char*)malloc(width * height * 4);
    if (!rgba) return NULL;
    
    for (int i = 0; i < width * height; i++) {
        rgba[i*4]   = rgb_data[i*3];
        rgba[i*4+1] = rgb_data[i*3+1];
        rgba[i*4+2] = rgb_data[i*3+2];
        rgba[i*4+3] = 255;
    }
    
    return rgba;
}

// Camera initialization (unchanged)
int init_camera(const char* device) {
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    
    video_fd = open(device, O_RDWR);
    if (video_fd < 0) {
        perror("Failed to open video device");
        return -1;
    }
    
    if (ioctl(video_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("Failed to query capabilities");
        close(video_fd);
        return -1;
    }
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is not a video capture device\n", device);
        close(video_fd);
        return -1;
    }
    
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WINDOW_WIDTH;
    fmt.fmt.pix.height = WINDOW_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    
    if (ioctl(video_fd, VIDIOC_S_FMT, &fmt) < 0) {
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        if (ioctl(video_fd, VIDIOC_S_FMT, &fmt) < 0) {
            perror("Failed to set format");
            close(video_fd);
            return -1;
        }
    }
    
    frame_width = fmt.fmt.pix.width;
    frame_height = fmt.fmt.pix.height;
    video_format = fmt.fmt.pix.pixelformat;
    
    printf("Camera initialized with resolution %dx%d and format %c%c%c%c\n",
           frame_width, frame_height,
           (video_format & 0xff),
           ((video_format >> 8) & 0xff),
           ((video_format >> 16) & 0xff),
           ((video_format >> 24) & 0xff));
    
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to request buffers");
        close(video_fd);
        return -1;
    }
    
    for (int i = 0; i < BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(video_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("Failed to query buffer");
            close(video_fd);
            return -1;
        }
        
        buffer_start[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                              MAP_SHARED, video_fd, buf.m.offset);
        buffer_length[i] = buf.length;
        
        if (buffer_start[i] == MAP_FAILED) {
            perror("Failed to map buffer");
            close(video_fd);
            return -1;
        }
        
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("Failed to queue buffer");
            close(video_fd);
            return -1;
        }
    }
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start streaming");
        close(video_fd);
        return -1;
    }
    
    return 0;
}

// Modified MP4 initialization to extract frame rate
int init_mp4_file(const char* filename) {
    int ret;
    
    ret = avformat_open_input(&format_context, filename, NULL, NULL);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Could not open source file: %s, %s\n", filename, errbuf);
        return -1;
    }
    
    ret = avformat_find_stream_info(format_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        return -1;
    }
    
    for (int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    
    if (video_stream_index == -1) {
        fprintf(stderr, "Could not find video stream in the input file\n");
        return -1;
    }
    
    const AVCodec* codec = avcodec_find_decoder(format_context->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Unsupported codec\n");
        return -1;
    }
    
    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        fprintf(stderr, "Failed to allocate codec context\n");
        return -1;
    }
    
    if (avcodec_parameters_to_context(codec_context, format_context->streams[video_stream_index]->codecpar) < 0) {
        fprintf(stderr, "Failed to copy codec parameters to decoder context\n");
        return -1;
    }
    
    ret = avcodec_open2(codec_context, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }
    
    av_frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    if (!av_frame || !rgba_frame) {
        fprintf(stderr, "Could not allocate video frames\n");
        return -1;
    }
    
    frame_width = codec_context->width;
    frame_height = codec_context->height;
    
    rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame_width, frame_height, 1);
    rgb_buffer = (uint8_t*)av_malloc(rgb_buffer_size);
    if (!rgb_buffer) {
        fprintf(stderr, "Could not allocate destination image buffer\n");
        return -1;
    }
    
    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgb_buffer,
                        AV_PIX_FMT_RGBA, frame_width, frame_height, 1);
    
    sws_context = sws_getContext(frame_width, frame_height, codec_context->pix_fmt,
                                frame_width, frame_height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_context) {
        fprintf(stderr, "Could not initialize the conversion context\n");
        return -1;
    }
    
    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate packet\n");
        return -1;
    }
    
    // Calculate frame duration from avg_frame_rate
    AVRational frame_rate = format_context->streams[video_stream_index]->avg_frame_rate;
    if (frame_rate.num > 0 && frame_rate.den > 0) {
        frame_duration = (double)frame_rate.den / frame_rate.num;  // Seconds per frame
        printf("Detected frame rate: %.2f FPS (duration: %.3f s)\n", 1.0 / frame_duration, frame_duration);
    } else {
        frame_duration = 0.033;  // Fallback to 30 FPS
        printf("Could not determine frame rate, using default 30 FPS\n");
    }
    
    printf("MP4 file opened with resolution %dx%d\n", frame_width, frame_height);
    return 0;
}

// Open video file (unchanged)
int open_video_file(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (ext && strcasecmp(ext, ".mp4") == 0) {
        video_source_type = VIDEO_SOURCE_MP4;
        return init_mp4_file(filename);
    }
    
    video_file = fopen(filename, "rb");
    if (!video_file) {
        fprintf(stderr, "Error opening video file: %s\n", filename);
        return -1;
    }
    
    char header[3];
    fread(header, 1, 3, video_file);
    if (header[0] != 'P' || header[1] != '6') {
        fprintf(stderr, "Not a valid P6 PPM file\n");
        fclose(video_file);
        return -1;
    }
    
    int c = getc(video_file);
    while (c == '#') {
        while (getc(video_file) != '\n');
        c = getc(video_file);
    }
    ungetc(c, video_file);
    
    if (fscanf(video_file, "%d %d", &frame_width, &frame_height) != 2) {
        fprintf(stderr, "Invalid PPM file: could not read width/height\n");
        fclose(video_file);
        return -1;
    }
    
    int maxval;
    if (fscanf(video_file, "%d", &maxval) != 1) {
        fprintf(stderr, "Invalid PPM file: could not read max color value\n");
        fclose(video_file);
        return -1;
    }
    
    fgetc(video_file);
    
    frame_data = (unsigned char*)malloc(frame_width * frame_height * 3);
    if (!frame_data) {
        fprintf(stderr, "Could not allocate memory for frame data\n");
        fclose(video_file);
        return -1;
    }
    
    printf("PPM file opened with resolution %dx%d\n", frame_width, frame_height);
    return 0;
}

// Camera frame handling (unchanged)
int get_next_camera_frame(unsigned char** frame_ptr) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(video_fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("Failed to dequeue buffer");
        return -1;
    }
    
    current_buffer = buf.index;
    *frame_ptr = buffer_start[current_buffer];
    return buffer_length[current_buffer];
}

void release_camera_frame() {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = current_buffer;
    
    if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("Failed to requeue buffer");
    }
}

// MP4 frame handling (unchanged)
int get_next_mp4_frame(unsigned char** frame_ptr) {
    int ret;
    int frame_finished = 0;
    
    while (!frame_finished) {
        ret = av_read_frame(format_context, packet);
        
        if (ret < 0) {
            av_seek_frame(format_context, video_stream_index, 0, AVSEEK_FLAG_BACKWARD);
            ret = av_read_frame(format_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error seeking to beginning of file\n");
                return -1;
            }
        }
        
        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error sending packet for decoding\n");
                return -1;
            }
            
            ret = avcodec_receive_frame(codec_context, av_frame);
            if (ret == 0) {
                frame_finished = 1;
                sws_scale(sws_context, (const uint8_t* const*)av_frame->data,
                         av_frame->linesize, 0, codec_context->height,
                         rgba_frame->data, rgba_frame->linesize);
                *frame_ptr = rgb_buffer;
                av_packet_unref(packet);
                return rgb_buffer_size;
            } else if (ret == AVERROR(EAGAIN)) {
                av_packet_unref(packet);
                continue;
            } else {
                fprintf(stderr, "Error receiving frame from decoder\n");
                av_packet_unref(packet);
                return -1;
            }
        }
        
        av_packet_unref(packet);
    }
    
    return -1;
}

// Frame buffering (unchanged)
void prefill_frame_buffer() {
    while (frame_buffer_count < FRAME_BUFFER_SIZE && video_source_type == VIDEO_SOURCE_MP4) {
        unsigned char* frame;
        int size = get_next_mp4_frame(&frame);
        if (size < 0) break;
        frame_buffer[frame_buffer_tail].data = malloc(size);
        memcpy(frame_buffer[frame_buffer_tail].data, frame, size);
        frame_buffer[frame_buffer_tail].size = size;
        frame_buffer_tail = (frame_buffer_tail + 1) % FRAME_BUFFER_SIZE;
        frame_buffer_count++;
    }
}

int get_buffered_frame(unsigned char** frame_ptr) {
    if (frame_buffer_count == 0) return -1;
    *frame_ptr = frame_buffer[frame_buffer_head].data;
    int size = frame_buffer[frame_buffer_head].size;
    frame_buffer_head = (frame_buffer_head + 1) % FRAME_BUFFER_SIZE;
    frame_buffer_count--;
    return size;
}

// PPM frame handling (unchanged)
int get_next_video_file_frame(unsigned char** frame_ptr) {
    if (fread(frame_data, 1, frame_width * frame_height * 3, video_file) != frame_width * frame_height * 3) {
        if (feof(video_file)) {
            fseek(video_file, 0, SEEK_SET);
            char line[100];
            fgets(line, sizeof(line), video_file);
            int c = getc(video_file);
            while (c == '#') {
                while (getc(video_file) != '\n');
                c = getc(video_file);
            }
            ungetc(c, video_file);
            fgets(line, sizeof(line), video_file);
            fgets(line, sizeof(line), video_file);
            if (fread(frame_data, 1, frame_width * frame_height * 3, video_file) != frame_width * frame_height * 3) {
                fprintf(stderr, "Error reading video frame after rewind\n");
                return -1;
            }
        } else {
            fprintf(stderr, "Error reading video frame\n");
            return -1;
        }
    }
    
    *frame_ptr = frame_data;
    return frame_width * frame_height * 3;
}

// Unified frame getter (unchanged)
int get_next_frame(unsigned char** frame_ptr) {
    switch (video_source_type) {
        case VIDEO_SOURCE_CAMERA:
            return get_next_camera_frame(frame_ptr);
        case VIDEO_SOURCE_FILE:
            return get_next_video_file_frame(frame_ptr);
        case VIDEO_SOURCE_MP4:
            return get_buffered_frame(frame_ptr);
        default:
            return -1;
    }
}

// Shader compilation (unchanged)
GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);
            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            fprintf(stderr, "Error compiling shader: %s\n", info_log);
            free(info_log);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

// Shader initialization (unchanged)
GLuint init_shaders() {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader;
    
    if (video_source_type == VIDEO_SOURCE_MP4 || video_source_type == VIDEO_SOURCE_FILE) {
        fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source_rgba);
    } else {
        fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source_yuyv);
    }
    
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);
            glGetProgramInfoLog(program, info_len, NULL, info_log);
            fprintf(stderr, "Error linking program: %s\n", info_log);
            free(info_log);
        }
        glDeleteProgram(program);
        return 0;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program;
}

// Framebuffer initialization (unchanged)
void init_framebuffer() {
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    glGenTextures(1, &output_texture);
    glBindTexture(GL_TEXTURE_2D, output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer is not complete!\n");
        exit(EXIT_FAILURE);
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Video texture initialization (unchanged)
void init_video_texture() {
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    if (video_source_type == VIDEO_SOURCE_CAMERA && video_format == V4L2_PIX_FMT_YUYV) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width / 2, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
}

// Geometry initialization (unchanged)
void init_geometry() {
    float vertices[] = {
        // Position      // Texcoords
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f, // Bottom-left
        1.0f, -1.0f, 0.0f,   1.0f, 1.0f, // Bottom-right
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f, // Top-left
        1.0f,  1.0f, 0.0f,   1.0f, 0.0f  // Top-right
    };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
}

// Wayland initialization (unchanged)
int init_wayland() {
    wl_display = wl_display_connect(NULL);
    if (!wl_display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return -1;
    }

    struct wl_registry *registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_dispatch(wl_display);
    wl_display_roundtrip(wl_display);

    if (!compositor || !shell) {
        fprintf(stderr, "Failed to get compositor or shell\n");
        return -1;
    }

    wl_surface = wl_compositor_create_surface(compositor);
    if (!wl_surface) {
        fprintf(stderr, "Failed to create wayland surface\n");
        return -1;
    }

    shell_surface = wl_shell_get_shell_surface(shell, wl_surface);
    if (!shell_surface) {
        fprintf(stderr, "Failed to get shell surface\n");
        return -1;
    }

    wl_shell_surface_set_toplevel(shell_surface);

    wl_egl_window = wl_egl_window_create(wl_surface, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!wl_egl_window) {
        fprintf(stderr, "Failed to create EGL window\n");
        return -1;
    }

    return 0;
}

// X11 initialization (unchanged)
int init_x11() {
    XSetWindowAttributes attr;
    XWindowAttributes window_attributes;

    x_display = XOpenDisplay(NULL);
    if (!x_display) {
        fprintf(stderr, "Failed to open X display\n");
        return -1;
    }

    int screen = DefaultScreen(x_display);
    Window root = RootWindow(x_display, screen);

    XVisualInfo visTemplate;
    visTemplate.screen = screen;
    int num_visuals;
    x_visual_info = XGetVisualInfo(x_display, VisualScreenMask, &visTemplate, &num_visuals);

    if (!x_visual_info) {
        fprintf(stderr, "Failed to get X visual info\n");
        return -1;
    }

    x_colormap = XCreateColormap(x_display, root, x_visual_info->visual, AllocNone);

    attr.colormap = x_colormap;
    attr.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;

    x_window = XCreateWindow(x_display, root,
            0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
            x_visual_info->depth, InputOutput,
            x_visual_info->visual,
            CWColormap | CWEventMask, &attr);

    XStoreName(x_display, x_window, "Video Player");
    XMapWindow(x_display, x_window);
    XFlush(x_display);

    XGetWindowAttributes(x_display, x_window, &window_attributes);
    while (window_attributes.map_state != IsViewable) {
        XGetWindowAttributes(x_display, x_window, &window_attributes);
    }

    return 0;
}

// EGL initialization (unchanged)
int init_egl() {
    EGLint major, minor, count, n;
    EGLConfig *configs;
    int config_index = 0;

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    if (display_server_type == DISPLAY_WAYLAND) {
        egl_display = eglGetDisplay((EGLNativeDisplayType)wl_display);
    } else if (display_server_type == DISPLAY_X11) {
        egl_display = eglGetDisplay((EGLNativeDisplayType)x_display);
    } else {
        fprintf(stderr, "Unknown display server type\n");
        return -1;
    }

    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        return -1;
    }

    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return -1;
    }

    printf("EGL version: %d.%d\n", major, minor);

    if (!eglGetConfigs(egl_display, NULL, 0, &count)) {
        fprintf(stderr, "Failed to get EGL config count\n");
        return -1;
    }

    configs = malloc(count * sizeof(EGLConfig));
    if (!eglChooseConfig(egl_display, config_attribs, configs, count, &n)) {
        fprintf(stderr, "Failed to get EGL configs\n");
        free(configs);
        return -1;
    }

    if (n < 1) {
        fprintf(stderr, "No suitable EGL configs found\n");
        free(configs);
        return -1;
    }

    egl_config = configs[config_index];
    free(configs);

    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        return -1;
    }

    if (display_server_type == DISPLAY_WAYLAND) {
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)wl_egl_window, NULL);
    } else if (display_server_type == DISPLAY_X11) {
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)x_window, NULL);
    }

    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        return -1;
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        return -1;
    }

    return 0;
}

// Cleanup video source (unchanged)
void cleanup_video_source() {
    switch (video_source_type) {
        case VIDEO_SOURCE_CAMERA:
            if (video_fd >= 0) {
                enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                ioctl(video_fd, VIDIOC_STREAMOFF, &type);
                for (int i = 0; i < BUFFER_COUNT; i++) {
                    if (buffer_start[i]) {
                        munmap(buffer_start[i], buffer_length[i]);
                    }
                }
                close(video_fd);
                video_fd = -1;
            }
            break;

        case VIDEO_SOURCE_FILE:
            if (video_file) {
                fclose(video_file);
                video_file = NULL;
            }
            if (frame_data) {
                free(frame_data);
                frame_data = NULL;
            }
            break;

        case VIDEO_SOURCE_MP4:
            if (packet) {
                av_packet_free(&packet);
            }
            if (rgba_frame) {
                av_frame_free(&rgba_frame);
            }
            if (av_frame) {
                av_frame_free(&av_frame);
            }
            if (codec_context) {
                avcodec_close(codec_context);
                avcodec_free_context(&codec_context);
            }
            if (format_context) {
                avformat_close_input(&format_context);
            }
            if (sws_context) {
                sws_freeContext(sws_context);
            }
            if (rgb_buffer) {
                av_free(rgb_buffer);
            }
            for (int i = 0; i < FRAME_BUFFER_SIZE; i++) {
                if (frame_buffer[i].data) {
                    free(frame_buffer[i].data);
                    frame_buffer[i].data = NULL;
                }
            }
            break;

        default:
            break;
    }
}

// Display cleanup (unchanged)
void cleanup_display() {
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display, egl_context);
        }
        if (egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display, egl_surface);
        }
        eglTerminate(egl_display);
    }

    if (display_server_type == DISPLAY_WAYLAND) {
        if (wl_egl_window) {
            wl_egl_window_destroy(wl_egl_window);
        }
        if (shell_surface) {
            wl_shell_surface_destroy(shell_surface);
        }
        if (wl_surface) {
            wl_surface_destroy(wl_surface);
        }
        if (shell) {
            wl_shell_destroy(shell);
        }
        if (compositor) {
            wl_compositor_destroy(compositor);
        }
        if (wl_display) {
            wl_display_disconnect(wl_display);
        }
    } else if (display_server_type == DISPLAY_X11) {
        if (x_colormap) {
            XFreeColormap(x_display, x_colormap);
        }
        if (x_visual_info) {
            XFree(x_visual_info);
        }
        if (x_window) {
            XDestroyWindow(x_display, x_window);
        }
        if (x_display) {
            XCloseDisplay(x_display);
        }
    }
}

// GL cleanup (unchanged)
void cleanup_gl() {
    if (texture_id) {
        glDeleteTextures(1, &texture_id);
    }
    if (output_texture) {
        glDeleteTextures(1, &output_texture);
    }
    if (framebuffer) {
        glDeleteFramebuffers(1, &framebuffer);
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
    }
    if (program) {
        glDeleteProgram(program);
    }
}

// Modified render loop with dynamic frame timing
void render_loop() {
    unsigned char* frame;
    int frame_size;
    XEvent xev;
    struct timespec start_time, end_time;
    double elapsed;

    // Prefill buffer for MP4
    if (video_source_type == VIDEO_SOURCE_MP4) {
        prefill_frame_buffer();
    }

    while (running) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        if (display_server_type == DISPLAY_X11) {
            while (XPending(x_display)) {
                XNextEvent(x_display, &xev);
                if (xev.type == KeyPress) {
                    running = 0;
                    break;
                }
            }
        }

        if (display_server_type == DISPLAY_WAYLAND) {
            wl_display_dispatch_pending(wl_display);
        }

        frame_size = get_next_frame(&frame);
        if (frame_size < 0) {
            fprintf(stderr, "Failed to get next frame\n");
            break;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id);

        if (video_source_type == VIDEO_SOURCE_CAMERA) {
            if (video_format == V4L2_PIX_FMT_YUYV) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width / 2, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame);
            } else if (video_format == V4L2_PIX_FMT_MJPEG) {
                fprintf(stderr, "MJPEG format not supported in this example\n");
                release_camera_frame();
                continue;
            }
            release_camera_frame();
        } else if (video_source_type == VIDEO_SOURCE_FILE) {
            unsigned char* rgba_data = convert_rgb_to_rgba(frame, frame_width, frame_height);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
            free(rgba_data);
        } else if (video_source_type == VIDEO_SOURCE_MP4) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame);
            // Refill buffer if space available
            if (frame_buffer_count < FRAME_BUFFER_SIZE) {
                unsigned char* next_frame;
                int next_size = get_next_mp4_frame(&next_frame);
                if (next_size >= 0) {
                    frame_buffer[frame_buffer_tail].data = malloc(next_size);
                    memcpy(frame_buffer[frame_buffer_tail].data, next_frame, next_size);
                    frame_buffer[frame_buffer_tail].size = next_size;
                    frame_buffer_tail = (frame_buffer_tail + 1) % FRAME_BUFFER_SIZE;
                    frame_buffer_count++;
                }
            }
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        GLint pos_attrib = glGetAttribLocation(program, "position");
        glEnableVertexAttribArray(pos_attrib);
        glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);

        GLint tex_attrib = glGetAttribLocation(program, "texcoord");
        glEnableVertexAttribArray(tex_attrib);
        glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

        GLint tex_uniform = glGetUniformLocation(program, "texture");
        glUniform1i(tex_uniform, 0);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(pos_attrib);
        glDisableVertexAttribArray(tex_attrib);

        eglSwapBuffers(egl_display, egl_surface);

        clock_gettime(CLOCK_MONOTONIC, &end_time);
        elapsed = (end_time.tv_sec - start_time.tv_sec) + 
                  (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
        if (elapsed < frame_duration) {
            usleep((frame_duration - elapsed) * 1000000);  // Dynamic sleep based on frame rate
        }
    }
}

// Main function (unchanged)
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <video_file.ppm|video_file.mp4|/dev/videoX>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* source_path = argv[1];

    if (strncmp(source_path, "/dev/video", 10) == 0) {
        video_source_type = VIDEO_SOURCE_CAMERA;
        printf("Using camera source: %s\n", source_path);
        if (init_camera(source_path) < 0) {
            fprintf(stderr, "Failed to initialize camera\n");
            return EXIT_FAILURE;
        }
    } else {
        const char* ext = strrchr(source_path, '.');
        if (ext && strcasecmp(ext, ".mp4") == 0) {
            video_source_type = VIDEO_SOURCE_MP4;
        } else {
            video_source_type = VIDEO_SOURCE_FILE;
        }
        printf("Using video file source: %s\n", source_path);
        if (open_video_file(source_path) < 0) {
            fprintf(stderr, "Failed to open video file\n");
            return EXIT_FAILURE;
        }
    }

    display_server_type = detect_display_server();
    if (display_server_type == DISPLAY_UNKNOWN) {
        fprintf(stderr, "No supported display server detected\n");
        cleanup_video_source();
        return EXIT_FAILURE;
    }

    if (display_server_type == DISPLAY_WAYLAND) {
        if (init_wayland() < 0) {
            fprintf(stderr, "Failed to initialize Wayland\n");
            cleanup_video_source();
            return EXIT_FAILURE;
        }
    } else if (display_server_type == DISPLAY_X11) {
        if (init_x11() < 0) {
            fprintf(stderr, "Failed to initialize X11\n");
            cleanup_video_source();
            return EXIT_FAILURE;
        }
    }

    if (init_egl() < 0) {
        fprintf(stderr, "Failed to initialize EGL\n");
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    program = init_shaders();
    if (!program) {
        fprintf(stderr, "Failed to initialize shaders\n");
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    init_geometry();
    init_framebuffer();
    init_video_texture();

    render_loop();

    cleanup_gl();
    cleanup_video_source();
    cleanup_display();

    printf("Video player terminated\n");
    return EXIT_SUCCESS;
}
