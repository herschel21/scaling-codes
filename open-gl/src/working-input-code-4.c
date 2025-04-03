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

#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080
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
double frame_duration = 0.0167;  // Default to 30 FPS, updated for MP4
double total_rendered_time = 0.0; // Total time of rendered frames in seconds

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

// FPS overlay variables
GLuint fps_texture;
GLuint fps_program;
GLuint fps_vbo;
double last_fps_time = 0.0;
int frame_count = 0;
float current_fps = 0.0;

// Simple 5x7 bitmap font (0-9, .)
static const unsigned char font_data[] = {
    0x3E, 0x45, 0x49, 0x51, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x62, 0x51, 0x49, 0x45, 0x42, // 2
    0x22, 0x41, 0x49, 0x51, 0x22, // 3
    0x0C, 0x0A, 0x09, 0x7F, 0x08, // 4
    0x2F, 0x49, 0x49, 0x49, 0x31, // 5
    0x3E, 0x49, 0x49, 0x49, 0x32, // 6
    0x03, 0x01, 0x71, 0x09, 0x07, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x26, 0x49, 0x49, 0x49, 0x3E, // 9
    0x00, 0x60, 0x60, 0x00, 0x00  // .
};

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

const char *fps_fragment_shader_source =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture;\n"
    "void main() {\n"
    "  float alpha = texture2D(texture, v_texcoord).a;\n"
    "  gl_FragColor = vec4(1.0, 1.0, 1.0, alpha);\n" // White text, alpha from texture
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
void init_fps_overlay();
int init_wayland();
int init_x11();
int init_egl();
void render_fps(float fps);
void cleanup_video_source();
void cleanup_display();
void cleanup_gl();
void render_loop();

// Registry handlers
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    printf("DEBUG: Registry global - name: %u, interface: %s, version: %u\n", name, interface, version);
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
        printf("DEBUG: Bound wl_compositor\n");
    } else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
        printf("DEBUG: Bound wl_shell\n");
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
        printf("DEBUG: Wayland display server detected\n");
        wl_display_disconnect(test_display);
        return DISPLAY_WAYLAND;
    }
    
    Display *test_x_display = XOpenDisplay(NULL);
    if (test_x_display) {
        printf("DEBUG: X11 display server detected\n");
        XCloseDisplay(test_x_display);
        return DISPLAY_X11;
    }
    
    printf("DEBUG: No supported display server detected\n");
    return DISPLAY_UNKNOWN;
}

// RGB to RGBA conversion
unsigned char* convert_rgb_to_rgba(unsigned char* rgb_data, int width, int height) {
    printf("DEBUG: Converting RGB to RGBA - width: %d, height: %d\n", width, height);
    unsigned char* rgba = (unsigned char*)malloc(width * height * 4);
    if (!rgba) {
        fprintf(stderr, "DEBUG: Failed to allocate memory for RGBA conversion\n");
        return NULL;
    }
    
    for (int i = 0; i < width * height; i++) {
        rgba[i*4]   = rgb_data[i*3];
        rgba[i*4+1] = rgb_data[i*3+1];
        rgba[i*4+2] = rgb_data[i*3+2];
        rgba[i*4+3] = 255;
    }
    
    printf("DEBUG: RGB to RGBA conversion completed\n");
    return rgba;
}

// Camera initialization
int init_camera(const char* device) {
    printf("DEBUG: Initializing camera - device: %s\n", device);
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    
    video_fd = open(device, O_RDWR);
    if (video_fd < 0) {
        perror("DEBUG: Failed to open video device");
        return -1;
    }
    
    if (ioctl(video_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("DEBUG: Failed to query capabilities");
        close(video_fd);
        return -1;
    }
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "DEBUG: %s is not a video capture device\n", device);
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
            perror("DEBUG: Failed to set format");
            close(video_fd);
            return -1;
        }
    }
    
    frame_width = fmt.fmt.pix.width;
    frame_height = fmt.fmt.pix.height;
    video_format = fmt.fmt.pix.pixelformat;
    
    printf("DEBUG: Camera initialized - resolution: %dx%d, format: %c%c%c%c\n",
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
        perror("DEBUG: Failed to request buffers");
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
            perror("DEBUG: Failed to query buffer");
            close(video_fd);
            return -1;
        }
        
        buffer_start[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                              MAP_SHARED, video_fd, buf.m.offset);
        buffer_length[i] = buf.length;
        
        if (buffer_start[i] == MAP_FAILED) {
            perror("DEBUG: Failed to map buffer");
            close(video_fd);
            return -1;
        }
        
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("DEBUG: Failed to queue buffer");
            close(video_fd);
            return -1;
        }
        printf("DEBUG: Buffer %d mapped and queued - length: %zu\n", i, buffer_length[i]);
    }
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("DEBUG: Failed to start streaming");
        close(video_fd);
        return -1;
    }
    
    printf("DEBUG: Camera streaming started\n");
    return 0;
}

// MP4 initialization
int init_mp4_file(const char* filename) {
    printf("DEBUG: Initializing MP4 file - filename: %s\n", filename);
    int ret;
    
    ret = avformat_open_input(&format_context, filename, NULL, NULL);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "DEBUG: Could not open source file: %s, %s\n", filename, errbuf);
        return -1;
    }
    
    ret = avformat_find_stream_info(format_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "DEBUG: Could not find stream information\n");
        return -1;
    }
    
    for (int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            printf("DEBUG: Video stream found at index: %d\n", i);
            break;
        }
    }
    
    if (video_stream_index == -1) {
        fprintf(stderr, "DEBUG: Could not find video stream in the input file\n");
        return -1;
    }
    
    const AVCodec* codec = avcodec_find_decoder(format_context->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "DEBUG: Unsupported codec\n");
        return -1;
    }
    printf("DEBUG: Codec found - name: %s\n", codec->name);
    
    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        fprintf(stderr, "DEBUG: Failed to allocate codec context\n");
        return -1;
    }
    
    if (avcodec_parameters_to_context(codec_context, format_context->streams[video_stream_index]->codecpar) < 0) {
        fprintf(stderr, "DEBUG: Failed to copy codec parameters to decoder context\n");
        return -1;
    }
    
    ret = avcodec_open2(codec_context, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "DEBUG: Could not open codec\n");
        return -1;
    }
    
    av_frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    if (!av_frame || !rgba_frame) {
        fprintf(stderr, "DEBUG: Could not allocate video frames\n");
        return -1;
    }
    
    frame_width = codec_context->width;
    frame_height = codec_context->height;
    
    rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame_width, frame_height, 1);
    rgb_buffer = (uint8_t*)av_malloc(rgb_buffer_size);
    if (!rgb_buffer) {
        fprintf(stderr, "DEBUG: Could not allocate destination image buffer\n");
        return -1;
    }
    
    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgb_buffer,
                        AV_PIX_FMT_RGBA, frame_width, frame_height, 1);
    
    sws_context = sws_getContext(frame_width, frame_height, codec_context->pix_fmt,
                                frame_width, frame_height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_context) {
        fprintf(stderr, "DEBUG: Could not initialize the conversion context\n");
        return -1;
    }
    
    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "DEBUG: Could not allocate packet\n");
        return -1;
    }
    
    AVRational frame_rate = format_context->streams[video_stream_index]->avg_frame_rate;
    if (frame_rate.num > 0 && frame_rate.den > 0) {
        frame_duration = (double)frame_rate.den / frame_rate.num;
        printf("DEBUG: Detected frame rate: %.2f FPS (duration: %.3f s)\n", 1.0 / frame_duration, frame_duration);
    } else {
        frame_duration = 0.033;
        printf("DEBUG: Could not determine frame rate, using default 30 FPS\n");
    }
    
    printf("DEBUG: MP4 file opened - resolution: %dx%d\n", frame_width, frame_height);
    return 0;
}

// Open video file
int open_video_file(const char* filename) {
    printf("DEBUG: Opening video file - filename: %s\n", filename);
    const char* ext = strrchr(filename, '.');
    if (ext && strcasecmp(ext, ".mp4") == 0) {
        video_source_type = VIDEO_SOURCE_MP4;
        return init_mp4_file(filename);
    }
    
    video_file = fopen(filename, "rb");
    if (!video_file) {
        fprintf(stderr, "DEBUG: Error opening video file: %s\n", filename);
        return -1;
    }
    
    char header[3];
    fread(header, 1, 3, video_file);
    if (header[0] != 'P' || header[1] != '6') {
        fprintf(stderr, "DEBUG: Not a valid P6 PPM file\n");
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
        fprintf(stderr, "DEBUG: Invalid PPM file: could not read width/height\n");
        fclose(video_file);
        return -1;
    }
    
    int maxval;
    if (fscanf(video_file, "%d", &maxval) != 1) {
        fprintf(stderr, "DEBUG: Invalid PPM file: could not read max color value\n");
        fclose(video_file);
        return -1;
    }
    
    fgetc(video_file);
    
    frame_data = (unsigned char*)malloc(frame_width * frame_height * 3);
    if (!frame_data) {
        fprintf(stderr, "DEBUG: Could not allocate memory for frame data\n");
        fclose(video_file);
        return -1;
    }
    
    printf("DEBUG: PPM file opened - resolution: %dx%d\n", frame_width, frame_height);
    return 0;
}

// Camera frame handling
int get_next_camera_frame(unsigned char** frame_ptr) {
    printf("DEBUG: Getting next camera frame\n");
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(video_fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("DEBUG: Failed to dequeue buffer");
        return -1;
    }
    
    current_buffer = buf.index;
    *frame_ptr = buffer_start[current_buffer];
    printf("DEBUG: Camera frame dequeued - index: %d, size: %zu\n", current_buffer, buffer_length[current_buffer]);
    return buffer_length[current_buffer];
}

void release_camera_frame() {
    printf("DEBUG: Releasing camera frame - index: %d\n", current_buffer);
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = current_buffer;
    
    if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("DEBUG: Failed to requeue buffer");
    }
    printf("DEBUG: Camera frame requeued\n");
}

// MP4 frame handling
int get_next_mp4_frame(unsigned char** frame_ptr) {
    printf("DEBUG: Getting next MP4 frame\n");
    int ret;
    int frame_finished = 0;
    
    while (!frame_finished) {
        ret = av_read_frame(format_context, packet);
        
        if (ret < 0) {
            avcodec_flush_buffers(codec_context);
            printf("DEBUG: End of MP4 file reached\n");
            return -1;
        }
        
        if (packet->stream_index == video_stream_index) {
            printf("DEBUG: Processing video packet - size: %d\n", packet->size);
            ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "DEBUG: Error sending packet for decoding\n");
                return -1;
            }
            
            ret = avcodec_receive_frame(codec_context, av_frame);
            if (ret == 0) {
                frame_finished = 1;
                sws_scale(sws_context, (const uint8_t* const*)av_frame->data,
                         av_frame->linesize, 0, codec_context->height,
                         rgba_frame->data, rgba_frame->linesize);
                *frame_ptr = rgb_buffer;
                printf("DEBUG: MP4 frame decoded - size: %d\n", rgb_buffer_size);
                av_packet_unref(packet);
                return rgb_buffer_size;
            } else if (ret == AVERROR(EAGAIN)) {
                printf("DEBUG: Need more data for frame\n");
                av_packet_unref(packet);
                continue;
            } else {
                fprintf(stderr, "DEBUG: Error receiving frame from decoder\n");
                av_packet_unref(packet);
                return -1;
            }
        }
        
        av_packet_unref(packet);
    }
    
    return -1;
}

// Frame buffering
int get_buffered_frame(unsigned char** frame_ptr) {
    printf("DEBUG: Getting buffered frame - count: %d\n", frame_buffer_count);
    if (frame_buffer_count == 0) {
        printf("DEBUG: No frames in buffer\n");
        return -1;
    }
    
    *frame_ptr = malloc(frame_buffer[frame_buffer_head].size);
    if (!*frame_ptr) {
        fprintf(stderr, "DEBUG: Failed to allocate memory for frame\n");
        return -1;
    }
    
    memcpy(*frame_ptr, frame_buffer[frame_buffer_head].data, frame_buffer[frame_buffer_head].size);
    int size = frame_buffer[frame_buffer_head].size;
    
    free(frame_buffer[frame_buffer_head].data);
    frame_buffer[frame_buffer_head].data = NULL;
    frame_buffer[frame_buffer_head].size = 0;
    
    frame_buffer_head = (frame_buffer_head + 1) % FRAME_BUFFER_SIZE;
    frame_buffer_count--;
    
    printf("DEBUG: Buffered frame retrieved - size: %d, new count: %d\n", size, frame_buffer_count);
    return size;
}

void prefill_frame_buffer() {
    printf("DEBUG: Prefilling frame buffer - current count: %d\n", frame_buffer_count);
    while (frame_buffer_count < FRAME_BUFFER_SIZE && video_source_type == VIDEO_SOURCE_MP4) {
        unsigned char* frame;
        int size = get_next_mp4_frame(&frame);
        if (size < 0) {
            printf("DEBUG: No more frames to prefill\n");
            break;
        }
        frame_buffer[frame_buffer_tail].data = malloc(size);
        memcpy(frame_buffer[frame_buffer_tail].data, frame, size);
        frame_buffer[frame_buffer_tail].size = size;
        frame_buffer_tail = (frame_buffer_tail + 1) % FRAME_BUFFER_SIZE;
        frame_buffer_count++;
        printf("DEBUG: Frame added to buffer - tail: %d, count: %d\n", frame_buffer_tail, frame_buffer_count);
    }
}

// PPM frame handling
int get_next_video_file_frame(unsigned char** frame_ptr) {
    printf("DEBUG: Getting next PPM frame\n");
    if (fread(frame_data, 1, frame_width * frame_height * 3, video_file) != frame_width * frame_height * 3) {
        if (feof(video_file)) {
            printf("DEBUG: End of PPM file reached\n");
            return -1;
        } else {
            fprintf(stderr, "DEBUG: Error reading video frame\n");
            return -1;
        }
    }
    
    *frame_ptr = frame_data;
    printf("DEBUG: PPM frame read - size: %d\n", frame_width * frame_height * 3);
    return frame_width * frame_height * 3;
}

// Unified frame getter
int get_next_frame(unsigned char** frame_ptr) {
    printf("DEBUG: Getting next frame - source: %d\n", video_source_type);
    switch (video_source_type) {
        case VIDEO_SOURCE_CAMERA:
            return get_next_camera_frame(frame_ptr);
        case VIDEO_SOURCE_FILE:
            return get_next_video_file_frame(frame_ptr);
        case VIDEO_SOURCE_MP4:
            return get_buffered_frame(frame_ptr);
        default:
            printf("DEBUG: Unknown video source type\n");
            return -1;
    }
}

// Shader compilation
GLuint compile_shader(GLenum type, const char *source) {
    printf("DEBUG: Compiling shader - type: %s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment");
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
            fprintf(stderr, "DEBUG: Error compiling shader: %s\n", info_log);
            free(info_log);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    printf("DEBUG: Shader compiled successfully\n");
    return shader;
}

// Shader initialization
GLuint init_shaders() {
    printf("DEBUG: Initializing shaders\n");
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
            fprintf(stderr, "DEBUG: Error linking program: %s\n", info_log);
            free(info_log);
        }
        glDeleteProgram(program);
        return 0;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    printf("DEBUG: Shaders initialized successfully\n");
    return program;
}

// Framebuffer initialization
void init_framebuffer() {
    printf("DEBUG: Initializing framebuffer\n");
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
        fprintf(stderr, "DEBUG: Framebuffer is not complete!\n");
        exit(EXIT_FAILURE);
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    printf("DEBUG: Framebuffer initialized\n");
}

// Video texture initialization
void init_video_texture() {
    printf("DEBUG: Initializing video texture\n");
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    if (video_source_type == VIDEO_SOURCE_CAMERA && video_format == V4L2_PIX_FMT_YUYV) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width / 2, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        printf("DEBUG: Texture initialized for YUYV - size: %dx%d\n", frame_width / 2, frame_height);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        printf("DEBUG: Texture initialized for RGBA - size: %dx%d\n", frame_width, frame_height);
    }
}

// Geometry initialization
void init_geometry() {
    printf("DEBUG: Initializing geometry\n");
    float vertices[] = {
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
        1.0f, -1.0f, 0.0f,   1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
        1.0f,  1.0f, 0.0f,   1.0f, 0.0f
    };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    printf("DEBUG: Geometry initialized\n");
}

// FPS overlay initialization
void init_fps_overlay() {
    printf("DEBUG: Initializing FPS overlay\n");
    // Create font texture (11 characters: 0-9 and .) using GL_ALPHA
    glGenTextures(1, &fps_texture);
    glBindTexture(GL_TEXTURE_2D, fps_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 5 * 11, 7, 0, GL_ALPHA, GL_UNSIGNED_BYTE, font_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Compile FPS shader
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fps_fragment_shader_source);
    fps_program = glCreateProgram();
    glAttachShader(fps_program, vertex_shader);
    glAttachShader(fps_program, fragment_shader);
    glLinkProgram(fps_program);

    GLint linked;
    glGetProgramiv(fps_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(fps_program, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);
            glGetProgramInfoLog(fps_program, info_len, NULL, info_log);
            fprintf(stderr, "DEBUG: Error linking FPS program: %s\n", info_log);
            free(info_log);
        }
        glDeleteProgram(fps_program);
        fps_program = 0;
        return;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    // Create VBO for FPS text (quad for up to 6 characters: "XX.XX")
    float fps_vertices[6 * 5 * 6]; // 6 characters, 5 floats per vertex, 6 vertices per quad
    glGenBuffers(1, &fps_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, fps_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fps_vertices), fps_vertices, GL_DYNAMIC_DRAW);

    printf("DEBUG: FPS overlay initialized\n");
}

// Wayland initialization
int init_wayland() {
    printf("DEBUG: Initializing Wayland\n");
    wl_display = wl_display_connect(NULL);
    if (!wl_display) {
        fprintf(stderr, "DEBUG: Failed to connect to Wayland display\n");
        return -1;
    }

    struct wl_registry *registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_dispatch(wl_display);
    wl_display_roundtrip(wl_display);

    if (!compositor || !shell) {
        fprintf(stderr, "DEBUG: Failed to get compositor or shell\n");
        return -1;
    }

    wl_surface = wl_compositor_create_surface(compositor);
    if (!wl_surface) {
        fprintf(stderr, "DEBUG: Failed to create Wayland surface\n");
        return -1;
    }

    shell_surface = wl_shell_get_shell_surface(shell, wl_surface);
    if (!shell_surface) {
        fprintf(stderr, "DEBUG: Failed to get shell surface\n");
        return -1;
    }

    wl_shell_surface_set_toplevel(shell_surface);

    wl_egl_window = wl_egl_window_create(wl_surface, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!wl_egl_window) {
        fprintf(stderr, "DEBUG: Failed to create EGL window\n");
        return -1;
    }

    printf("DEBUG: Wayland initialized\n");
    return 0;
}

// X11 initialization
int init_x11() {
    printf("DEBUG: Initializing X11\n");
    XSetWindowAttributes attr;
    XWindowAttributes window_attributes;

    x_display = XOpenDisplay(NULL);
    if (!x_display) {
        fprintf(stderr, "DEBUG: Failed to open X display\n");
        return -1;
    }

    int screen = DefaultScreen(x_display);
    Window root = RootWindow(x_display, screen);

    XVisualInfo visTemplate;
    visTemplate.screen = screen;
    int num_visuals;
    x_visual_info = XGetVisualInfo(x_display, VisualScreenMask, &visTemplate, &num_visuals);

    if (!x_visual_info) {
        fprintf(stderr, "DEBUG: Failed to get X visual info\n");
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

    printf("DEBUG: X11 initialized\n");
    return 0;
}

// EGL initialization
int init_egl() {
    printf("DEBUG: Initializing EGL\n");
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
        fprintf(stderr, "DEBUG: Unknown display server type\n");
        return -1;
    }

    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "DEBUG: Failed to get EGL display\n");
        return -1;
    }

    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "DEBUG: Failed to initialize EGL\n");
        return -1;
    }

    printf("DEBUG: EGL version: %d.%d\n", major, minor);

    if (!eglGetConfigs(egl_display, NULL, 0, &count)) {
        fprintf(stderr, "DEBUG: Failed to get EGL config count\n");
        return -1;
    }

    configs = malloc(count * sizeof(EGLConfig));
    if (!eglChooseConfig(egl_display, config_attribs, configs, count, &n)) {
        fprintf(stderr, "DEBUG: Failed to get EGL configs\n");
        free(configs);
        return -1;
    }

    if (n < 1) {
        fprintf(stderr, "DEBUG: No suitable EGL configs found\n");
        free(configs);
        return -1;
    }

    egl_config = configs[config_index];
    free(configs);

    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "DEBUG: Failed to create EGL context\n");
        return -1;
    }

    if (display_server_type == DISPLAY_WAYLAND) {
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)wl_egl_window, NULL);
    } else if (display_server_type == DISPLAY_X11) {
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)x_window, NULL);
    }

    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "DEBUG: Failed to create EGL surface\n");
        return -1;
    }

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "DEBUG: Failed to make EGL context current\n");
        return -1;
    }

    printf("DEBUG: EGL initialized\n");
    return 0;
}

// Render FPS overlay
void render_fps(float fps) {
    char fps_str[6];
    snprintf(fps_str, 6, "%.1f", fps); // Format as "XX.X"
    int len = strlen(fps_str);

    // Increase size of characters (e.g., 20x28 pixels instead of 10x14)
    float char_width = 20.0f / WINDOW_WIDTH;  // Larger width
    float char_height = 28.0f / WINDOW_HEIGHT; // Larger height
    float start_x = -1.0f + 10.0f / WINDOW_WIDTH; // 10 pixel offset from left
    float start_y = 1.0f - 10.0f / WINDOW_HEIGHT - char_height; // 10 pixel offset from top

    float vertices[6 * 5 * len];
    for (int i = 0; i < len; i++) {
        int char_idx = (fps_str[i] == '.' ? 10 : fps_str[i] - '0');
        float tex_x = (float)(char_idx * 5) / (5 * 11);
        float tex_x_end = tex_x + 5.0f / (5 * 11);
        float x = start_x + i * char_width;
        float x_end = x + char_width;

        // Define quad vertices (triangle strip: bottom-left, bottom-right, top-left, top-right)
        float* v = &vertices[i * 6 * 5];
        // Bottom-left
        v[0] = x; v[1] = start_y; v[2] = 0.0f; v[3] = tex_x; v[4] = 1.0f;
        // Bottom-right
        v[5] = x_end; v[6] = start_y; v[7] = 0.0f; v[8] = tex_x_end; v[9] = 1.0f;
        // Top-left
        v[10] = x; v[11] = start_y + char_height; v[12] = 0.0f; v[13] = tex_x; v[14] = 0.0f;
        // Top-left (degenerate)
        v[15] = x; v[16] = start_y + char_height; v[17] = 0.0f; v[18] = tex_x; v[19] = 0.0f;
        // Bottom-right (degenerate)
        v[20] = x_end; v[21] = start_y; v[22] = 0.0f; v[23] = tex_x_end; v[24] = 1.0f;
        // Top-right
        v[25] = x_end; v[26] = start_y + char_height; v[27] = 0.0f; v[28] = tex_x_end; v[29] = 0.0f;
    }

    glUseProgram(fps_program);
    glBindBuffer(GL_ARRAY_BUFFER, fps_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    printf("DEBUG: FPS vertices updated - len: %d\n", len);

    GLint pos_attrib = glGetAttribLocation(fps_program, "position");
    glEnableVertexAttribArray(pos_attrib);
    glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);

    GLint tex_attrib = glGetAttribLocation(fps_program, "texcoord");
    glEnableVertexAttribArray(tex_attrib);
    glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fps_texture);
    GLint tex_uniform = glGetUniformLocation(fps_program, "texture");
    glUniform1i(tex_uniform, 0);
    printf("DEBUG: FPS texture bound\n");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 6 * len);
    printf("DEBUG: FPS rendered - FPS: %.1f\n", fps);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(pos_attrib);
    glDisableVertexAttribArray(tex_attrib);
}

// Cleanup video source
void cleanup_video_source() {
    printf("DEBUG: Cleaning up video source\n");
    switch (video_source_type) {
        case VIDEO_SOURCE_CAMERA:
            if (video_fd >= 0) {
                enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                ioctl(video_fd, VIDIOC_STREAMOFF, &type);
                for (int i = 0; i < BUFFER_COUNT; i++) {
                    if (buffer_start[i]) {
                        munmap(buffer_start[i], buffer_length[i]);
                        printf("DEBUG: Unmapped camera buffer %d\n", i);
                    }
                }
                close(video_fd);
                video_fd = -1;
                printf("DEBUG: Camera closed\n");
            }
            break;

        case VIDEO_SOURCE_FILE:
            if (video_file) {
                fclose(video_file);
                video_file = NULL;
                printf("DEBUG: PPM file closed\n");
            }
            if (frame_data) {
                free(frame_data);
                frame_data = NULL;
                printf("DEBUG: Frame data freed\n");
            }
            break;

        case VIDEO_SOURCE_MP4:
            if (packet) {
                av_packet_free(&packet);
                printf("DEBUG: Packet freed\n");
            }
            if (rgba_frame) {
                av_frame_free(&rgba_frame);
                printf("DEBUG: RGBA frame freed\n");
            }
            if (av_frame) {
                av_frame_free(&av_frame);
                printf("DEBUG: AV frame freed\n");
            }
            if (codec_context) {
                avcodec_close(codec_context);
                avcodec_free_context(&codec_context);
                printf("DEBUG: Codec context freed\n");
            }
            if (format_context) {
                avformat_close_input(&format_context);
                printf("DEBUG: Format context closed\n");
            }
            if (sws_context) {
                sws_freeContext(sws_context);
                printf("DEBUG: SWS context freed\n");
            }
            if (rgb_buffer) {
                av_free(rgb_buffer);
                printf("DEBUG: RGB buffer freed\n");
            }
            for (int i = 0; i < FRAME_BUFFER_SIZE; i++) {
                if (frame_buffer[i].data) {
                    free(frame_buffer[i].data);
                    frame_buffer[i].data = NULL;
                    printf("DEBUG: Frame buffer %d freed\n", i);
                }
            }
            break;

        default:
            break;
    }
    printf("DEBUG: Video source cleanup completed\n");
}

// Display cleanup
void cleanup_display() {
    printf("DEBUG: Cleaning up display\n");
    if (egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display, egl_context);
            printf("DEBUG: EGL context destroyed\n");
        }
        if (egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display, egl_surface);
            printf("DEBUG: EGL surface destroyed\n");
        }
        eglTerminate(egl_display);
        printf("DEBUG: EGL terminated\n");
    }

    if (display_server_type == DISPLAY_WAYLAND) {
        if (wl_egl_window) {
            wl_egl_window_destroy(wl_egl_window);
            printf("DEBUG: Wayland EGL window destroyed\n");
        }
        if (shell_surface) {
            wl_shell_surface_destroy(shell_surface);
            printf("DEBUG: Shell surface destroyed\n");
        }
        if (wl_surface) {
            wl_surface_destroy(wl_surface);
            printf("DEBUG: Wayland surface destroyed\n");
        }
        if (shell) {
            wl_shell_destroy(shell);
            printf("DEBUG: Shell destroyed\n");
        }
        if (compositor) {
            wl_compositor_destroy(compositor);
            printf("DEBUG: Compositor destroyed\n");
        }
        if (wl_display) {
            wl_display_disconnect(wl_display);
            printf("DEBUG: Wayland display disconnected\n");
        }
    } else if (display_server_type == DISPLAY_X11) {
        if (x_colormap) {
            XFreeColormap(x_display, x_colormap);
            printf("DEBUG: X11 colormap freed\n");
        }
        if (x_visual_info) {
            XFree(x_visual_info);
            printf("DEBUG: X11 visual info freed\n");
        }
        if (x_window) {
            XDestroyWindow(x_display, x_window);
            printf("DEBUG: X11 window destroyed\n");
        }
        if (x_display) {
            XCloseDisplay(x_display);
            printf("DEBUG: X11 display closed\n");
        }
    }
    printf("DEBUG: Display cleanup completed\n");
}

// GL cleanup
void cleanup_gl() {
    printf("DEBUG: Cleaning up GL\n");
    if (texture_id) {
        glDeleteTextures(1, &texture_id);
        printf("DEBUG: Texture deleted\n");
    }
    if (output_texture) {
        glDeleteTextures(1, &output_texture);
        printf("DEBUG: Output texture deleted\n");
    }
    if (framebuffer) {
        glDeleteFramebuffers(1, &framebuffer);
        printf("DEBUG: Framebuffer deleted\n");
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        printf("DEBUG: VBO deleted\n");
    }
    if (program) {
        glDeleteProgram(program);
        printf("DEBUG: Program deleted\n");
    }
    if (fps_texture) {
        glDeleteTextures(1, &fps_texture);
        printf("DEBUG: FPS texture deleted\n");
    }
    if (fps_program) {
        glDeleteProgram(fps_program);
        printf("DEBUG: FPS program deleted\n");
    }
    if (fps_vbo) {
        glDeleteBuffers(1, &fps_vbo);
        printf("DEBUG: FPS VBO deleted\n");
    }
    printf("DEBUG: GL cleanup completed\n");
}

// Render loop
void render_loop() {
    printf("DEBUG: Starting render loop\n");
    unsigned char* frame;
    int frame_size;
    XEvent xev;
    struct timespec start_time, end_time;
    double elapsed;

    if (video_source_type == VIDEO_SOURCE_MP4) {
        prefill_frame_buffer();
    }

    while (running) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        printf("DEBUG: Frame loop iteration - running: %d\n", running);

        if (display_server_type == DISPLAY_X11) {
            while (XPending(x_display)) {
                XNextEvent(x_display, &xev);
                if (xev.type == KeyPress) {
                    running = 0;
                    printf("DEBUG: Keypress detected, stopping\n");
                    break;
                }
            }
        }

        if (display_server_type == DISPLAY_WAYLAND) {
            wl_display_dispatch_pending(wl_display);
            printf("DEBUG: Wayland events dispatched\n");
        }

        frame_size = get_next_frame(&frame);
        if (frame_size < 0) {
            if (video_source_type == VIDEO_SOURCE_CAMERA) {
                fprintf(stderr, "DEBUG: Failed to get next camera frame\n");
            } else {
                printf("DEBUG: Video playback completed\n");
                running = 0;
            }
            break;
        }
        printf("DEBUG: Frame retrieved - size: %d\n", frame_size);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id);

        if (video_source_type == VIDEO_SOURCE_CAMERA) {
            if (video_format == V4L2_PIX_FMT_YUYV) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width / 2, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame);
                printf("DEBUG: Uploaded YUYV frame\n");
            } else if (video_format == V4L2_PIX_FMT_MJPEG) {
                fprintf(stderr, "DEBUG: MJPEG format not supported in this example\n");
                release_camera_frame();
                continue;
            }
            release_camera_frame();
        } else if (video_source_type == VIDEO_SOURCE_FILE) {
            unsigned char* rgba_data = convert_rgb_to_rgba(frame, frame_width, frame_height);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
            free(rgba_data);
            printf("DEBUG: Uploaded PPM frame\n");
        } else if (video_source_type == VIDEO_SOURCE_MP4) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame);
            free(frame);
            printf("DEBUG: Uploaded MP4 frame\n");
            if (frame_buffer_count < FRAME_BUFFER_SIZE) {
                unsigned char* next_frame;
                int next_size = get_next_mp4_frame(&next_frame);
                if (next_size >= 0) {
                    frame_buffer[frame_buffer_tail].data = malloc(next_size);
                    memcpy(frame_buffer[frame_buffer_tail].data, next_frame, next_size);
                    frame_buffer[frame_buffer_tail].size = next_size;
                    frame_buffer_tail = (frame_buffer_tail + 1) % FRAME_BUFFER_SIZE;
                    frame_buffer_count++;
                    printf("DEBUG: Refilled buffer - count: %d\n", frame_buffer_count);
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
        printf("DEBUG: Frame rendered\n");

        glDisableVertexAttribArray(pos_attrib);
        glDisableVertexAttribArray(tex_attrib);

        // Calculate FPS
        frame_count++;
        total_rendered_time += frame_duration;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
        double current_time = end_time.tv_sec + end_time.tv_nsec / 1000000000.0;
        if (current_time - last_fps_time >= 1.0) {
            current_fps = frame_count / (current_time - last_fps_time);
            frame_count = 0;
            last_fps_time = current_time;
            printf("DEBUG: Current FPS: %.1f\n", current_fps);
        }

        // Render FPS overlay (ensure it's called after video frame)
        render_fps(current_fps);

        eglSwapBuffers(egl_display, egl_surface);
        printf("DEBUG: Buffers swapped\n");

        printf("DEBUG: Frame time: %.3f s\n", elapsed);
        if (elapsed < frame_duration) {
            usleep((frame_duration - elapsed) * 1000000);
            printf("DEBUG: Slept for %.3f s\n", (frame_duration - elapsed));
        }
    }
    printf("DEBUG: Render loop ended\n");
    printf("DEBUG: Total rendered time: %.2f seconds\n", total_rendered_time);
}

// Main function
int main(int argc, char *argv[]) {
    printf("DEBUG: Program started\n");
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <video_file.ppm|video_file.mp4|/dev/videoX>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* source_path = argv[1];

    if (strncmp(source_path, "/dev/video", 10) == 0) {
        video_source_type = VIDEO_SOURCE_CAMERA;
        printf("DEBUG: Using camera source: %s\n", source_path);
        if (init_camera(source_path) < 0) {
            fprintf(stderr, "DEBUG: Failed to initialize camera\n");
            return EXIT_FAILURE;
        }
    } else {
        const char* ext = strrchr(source_path, '.');
        if (ext && strcasecmp(ext, ".mp4") == 0) {
            video_source_type = VIDEO_SOURCE_MP4;
        } else {
            video_source_type = VIDEO_SOURCE_FILE;
        }
        printf("DEBUG: Using video file source: %s\n", source_path);
        if (open_video_file(source_path) < 0) {
            fprintf(stderr, "DEBUG: Failed to open video file\n");
            return EXIT_FAILURE;
        }
    }

    display_server_type = detect_display_server();
    if (display_server_type == DISPLAY_UNKNOWN) {
        fprintf(stderr, "DEBUG: No supported display server detected\n");
        cleanup_video_source();
        return EXIT_FAILURE;
    }

    if (display_server_type == DISPLAY_WAYLAND) {
        if (init_wayland() < 0) {
            fprintf(stderr, "DEBUG: Failed to initialize Wayland\n");
            cleanup_video_source();
            return EXIT_FAILURE;
        }
    } else if (display_server_type == DISPLAY_X11) {
        if (init_x11() < 0) {
            fprintf(stderr, "DEBUG: Failed to initialize X11\n");
            cleanup_video_source();
            return EXIT_FAILURE;
        }
    }

    if (init_egl() < 0) {
        fprintf(stderr, "DEBUG: Failed to initialize EGL\n");
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    program = init_shaders();
    if (!program) {
        fprintf(stderr, "DEBUG: Failed to initialize shaders\n");
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    init_geometry();
    init_framebuffer();
    init_video_texture();
    init_fps_overlay();
    if (!fps_program) {
        fprintf(stderr, "DEBUG: Failed to initialize FPS overlay\n");
        cleanup_gl();
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    // Initialize FPS timing
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    last_fps_time = ts.tv_sec + ts.tv_nsec / 1000000000.0;

    render_loop();

    cleanup_gl();
    cleanup_video_source();
    cleanup_display();

    printf("DEBUG: Video player terminated\n");
    return EXIT_SUCCESS;
}
