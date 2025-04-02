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
#include <linux/videodev2.h>  // For V4L2 support
#include <sys/mman.h>

// X11 includes
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/eglplatform.h>

// FFmpeg includes for MP4 support
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#define WINDOW_WIDTH  1920
#define WINDOW_HEIGHT 1080
#define BUFFER_COUNT 4  // Number of buffers for video processing

// Enum to track which display server we're using
typedef enum {
    DISPLAY_WAYLAND,
    DISPLAY_X11,
    DISPLAY_UNKNOWN
} DisplayServerType;

// Video source type
typedef enum {
    VIDEO_SOURCE_FILE,
    VIDEO_SOURCE_CAMERA,
    VIDEO_SOURCE_MP4,  // Added MP4 as a specific format
    VIDEO_SOURCE_NONE
} VideoSourceType;

// Globals
DisplayServerType display_server_type = DISPLAY_UNKNOWN;
VideoSourceType video_source_type = VIDEO_SOURCE_NONE;

// Video capture globals
int video_fd = -1;
struct v4l2_buffer v4l2_buffers[BUFFER_COUNT];
void* buffer_start[BUFFER_COUNT];
size_t buffer_length[BUFFER_COUNT];
int current_buffer = 0;
int frame_width = 0;
int frame_height = 0;
int video_format = 0;  // Will store V4L2 pixel format

// Video file globals
FILE* video_file = NULL;
unsigned char* frame_data = NULL;

// FFmpeg globals for MP4 handling
AVFormatContext* format_context = NULL;
AVCodecContext* codec_context = NULL;
AVFrame* av_frame = NULL;
AVFrame* rgba_frame = NULL;
AVPacket* packet = NULL;
struct SwsContext* sws_context = NULL;
int video_stream_index = -1;
uint8_t* rgb_buffer = NULL;
int rgb_buffer_size = 0;

// Wayland globals
struct wl_display *wl_display;
struct wl_compositor *compositor;
struct wl_surface *wl_surface;
struct wl_egl_window *wl_egl_window;
struct wl_shell *shell;
struct wl_shell_surface *shell_surface;

// X11 globals
Display *x_display = NULL;
Window x_window;
Colormap x_colormap;
XVisualInfo *x_visual_info;

// Common EGL globals
EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
EGLConfig egl_config;

// OpenGL variables
GLuint texture_id;
GLuint program;
GLuint vbo;
GLuint framebuffer;
GLuint output_texture;
int running = 1;  // Control flag for main loop

// Shader sources
const char *vertex_shader_source =
    "attribute vec3 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 1.0);\n"
    "  v_texcoord = texcoord;\n"
    "}\n";

const char *fragment_shader_source =
    "precision mediump float;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(texture, v_texcoord);\n"
    "}\n";

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

// Simple structure to hold image data
typedef struct {
    unsigned char* data;
    int width;
    int height;
    int channels;
} Image;

// Function to detect available display server
DisplayServerType detect_display_server() {
    // Try to connect to Wayland first
    struct wl_display *test_display = wl_display_connect(NULL);
    if (test_display) {
        printf("Wayland display server detected\n");
        wl_display_disconnect(test_display);
        return DISPLAY_WAYLAND;
    }
    
    // Try X11 next
    Display *test_x_display = XOpenDisplay(NULL);
    if (test_x_display) {
        printf("X11 display server detected\n");
        XCloseDisplay(test_x_display);
        return DISPLAY_X11;
    }
    
    printf("No supported display server detected\n");
    return DISPLAY_UNKNOWN;
}

// Convert RGB to RGBA (adding alpha channel)
unsigned char* convert_rgb_to_rgba(unsigned char* rgb_data, int width, int height) {
    // Allocate memory for RGBA data
    unsigned char* rgba = (unsigned char*)malloc(width * height * 4);
    if (!rgba) return NULL;
    
    // Convert RGB to RGBA (set alpha to 255)
    for (int i = 0; i < width * height; i++) {
        rgba[i*4]   = rgb_data[i*3];     // R
        rgba[i*4+1] = rgb_data[i*3+1];   // G
        rgba[i*4+2] = rgb_data[i*3+2];   // B
        rgba[i*4+3] = 255;               // A (fully opaque)
    }
    
    return rgba;
}

// Convert YUV (YUV420) to RGBA
unsigned char* convert_yuv_to_rgba(unsigned char* yuv_data, int width, int height) {
    unsigned char* rgba = (unsigned char*)malloc(width * height * 4);
    if (!rgba) return NULL;
    
    unsigned char* y_plane = yuv_data;
    unsigned char* u_plane = y_plane + width * height;
    unsigned char* v_plane = u_plane + (width * height / 4);
    
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            int y_index = j * width + i;
            int u_index = (j/2) * (width/2) + (i/2);
            int v_index = u_index;
            
            int y = y_plane[y_index];
            int u = u_plane[u_index] - 128;
            int v = v_plane[v_index] - 128;
            
            int r = y + 1.402 * v;
            int g = y - 0.344 * u - 0.714 * v;
            int b = y + 1.772 * u;
            
            // Clamp values
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            
            // Write to RGBA buffer
            rgba[(j * width + i) * 4] = r;
            rgba[(j * width + i) * 4 + 1] = g;
            rgba[(j * width + i) * 4 + 2] = b;
            rgba[(j * width + i) * 4 + 3] = 255;
        }
    }
    
    return rgba;
}

// Function to initialize V4L2 camera
int init_camera(const char* device) {
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    
    // Open the video device
    video_fd = open(device, O_RDWR);
    if (video_fd < 0) {
        perror("Failed to open video device");
        return -1;
    }
    
    // Check device capabilities
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
    
    // Set format
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WINDOW_WIDTH;
    fmt.fmt.pix.height = WINDOW_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;  // Try YUYV format first
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    
    if (ioctl(video_fd, VIDIOC_S_FMT, &fmt) < 0) {
        // Try fallback to common format
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        if (ioctl(video_fd, VIDIOC_S_FMT, &fmt) < 0) {
            perror("Failed to set format");
            close(video_fd);
            return -1;
        }
    }
    
    // Store the negotiated format
    frame_width = fmt.fmt.pix.width;
    frame_height = fmt.fmt.pix.height;
    video_format = fmt.fmt.pix.pixelformat;
    
    printf("Camera initialized with resolution %dx%d and format %c%c%c%c\n",
           frame_width, frame_height,
           (video_format & 0xff),
           ((video_format >> 8) & 0xff),
           ((video_format >> 16) & 0xff),
           ((video_format >> 24) & 0xff));
    
    // Request buffers
    memset(&req, 0, sizeof(req));
    req.count = BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(video_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Failed to request buffers");
        close(video_fd);
        return -1;
    }
    
    // Map buffers
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
        
        // Enqueue the buffer
        if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("Failed to queue buffer");
            close(video_fd);
            return -1;
        }
    }
    
    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to start streaming");
        close(video_fd);
        return -1;
    }
    
    return 0;
}

// Initialize FFmpeg for MP4 decoding
int init_mp4_file(const char* filename) {
    int ret;
    
    // Open video file
    ret = avformat_open_input(&format_context, filename, NULL, NULL);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Could not open source file: %s, %s\n", filename, errbuf);
        return -1;
    }
    
    // Retrieve stream information
    ret = avformat_find_stream_info(format_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        return -1;
    }
    
    // Find the first video stream
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
    
    // Get a pointer to the codec context for the video stream
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
    
    // Copy codec parameters from input stream to output codec context
    if (avcodec_parameters_to_context(codec_context, format_context->streams[video_stream_index]->codecpar) < 0) {
        fprintf(stderr, "Failed to copy codec parameters to decoder context\n");
        return -1;
    }
    
    // Open the codec
    ret = avcodec_open2(codec_context, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }
    
    // Allocate video frames
    av_frame = av_frame_alloc();
    rgba_frame = av_frame_alloc();
    if (!av_frame || !rgba_frame) {
        fprintf(stderr, "Could not allocate video frames\n");
        return -1;
    }
    
    // Store video dimensions
    frame_width = codec_context->width;
    frame_height = codec_context->height;
    
    // Determine required buffer size and allocate buffer
    rgb_buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, frame_width, frame_height, 1);
    rgb_buffer = (uint8_t*)av_malloc(rgb_buffer_size);
    if (!rgb_buffer) {
        fprintf(stderr, "Could not allocate destination image buffer\n");
        return -1;
    }
    
    // Assign appropriate parts of buffer to image planes in rgba_frame
    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, rgb_buffer,
                        AV_PIX_FMT_RGBA, frame_width, frame_height, 1);
    
    // Initialize SWS context for software scaling/conversion
    sws_context = sws_getContext(frame_width, frame_height, codec_context->pix_fmt,
                                frame_width, frame_height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_context) {
        fprintf(stderr, "Could not initialize the conversion context\n");
        return -1;
    }
    
    // Allocate packet
    packet = av_packet_alloc();
    if (!packet) {
        fprintf(stderr, "Could not allocate packet\n");
        return -1;
    }
    
    printf("MP4 file opened with resolution %dx%d\n", frame_width, frame_height);
    return 0;
}

// Function to open a video file (PPM format)
int open_video_file(const char* filename) {
    // Check file extension to determine format
    const char* ext = strrchr(filename, '.');
    if (ext && strcasecmp(ext, ".mp4") == 0) {
        // Handle MP4 files with FFmpeg
        video_source_type = VIDEO_SOURCE_MP4;
        return init_mp4_file(filename);
    }
    
    // Continue with existing PPM handling
    video_file = fopen(filename, "rb");
    if (!video_file) {
        fprintf(stderr, "Error opening video file: %s\n", filename);
        return -1;
    }
    
    // Read PPM header (P6 format)
    char header[3];
    fread(header, 1, 3, video_file);
    if (header[0] != 'P' || header[1] != '6') {
        fprintf(stderr, "Not a valid P6 PPM file\n");
        fclose(video_file);
        return -1;
    }
    
    // Skip comments
    int c = getc(video_file);
    while (c == '#') {
        while (getc(video_file) != '\n');
        c = getc(video_file);
    }
    ungetc(c, video_file);
    
    // Read width and height
    if (fscanf(video_file, "%d %d", &frame_width, &frame_height) != 2) {
        fprintf(stderr, "Invalid PPM file: could not read width/height\n");
        fclose(video_file);
        return -1;
    }
    
    // Read max color value
    int maxval;
    if (fscanf(video_file, "%d", &maxval) != 1) {
        fprintf(stderr, "Invalid PPM file: could not read max color value\n");
        fclose(video_file);
        return -1;
    }
    
    // Skip whitespace
    fgetc(video_file);
    
    // Allocate memory for a frame
    frame_data = (unsigned char*)malloc(frame_width * frame_height * 3);
    if (!frame_data) {
        fprintf(stderr, "Could not allocate memory for frame data\n");
        fclose(video_file);
        return -1;
    }
    
    printf("PPM file opened with resolution %dx%d\n", frame_width, frame_height);
    return 0;
}

// Function to get the next frame from camera
int get_next_camera_frame(unsigned char** frame_ptr) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    // Dequeue a buffer
    if (ioctl(video_fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("Failed to dequeue buffer");
        return -1;
    }
    
    // Store the buffer index
    current_buffer = buf.index;
    
    // Set the frame pointer to the buffer data
    *frame_ptr = buffer_start[current_buffer];
    
    return buffer_length[current_buffer];
}

// Function to release the current camera frame
void release_camera_frame() {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = current_buffer;
    
    // Enqueue the buffer back
    if (ioctl(video_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("Failed to requeue buffer");
    }
}

// Function to get the next frame from an MP4 file
int get_next_mp4_frame(unsigned char** frame_ptr) {
    int ret;
    int frame_finished = 0;
    
    // Read frames until we get a complete video frame
    while (!frame_finished) {
        ret = av_read_frame(format_context, packet);
        
        // If we've reached the end of the file, seek back to the beginning
        if (ret < 0) {
            av_seek_frame(format_context, video_stream_index, 0, AVSEEK_FLAG_BACKWARD);
            ret = av_read_frame(format_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error seeking to beginning of file\n");
                return -1;
            }
        }
        
        // Check if this packet is from the video stream
        if (packet->stream_index == video_stream_index) {
            // Decode video frame
            ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error sending packet for decoding\n");
                return -1;
            }
            
            ret = avcodec_receive_frame(codec_context, av_frame);
            if (ret == 0) {
                // Frame successfully decoded
                frame_finished = 1;
                
                // Convert the image from its native format to RGBA
                sws_scale(sws_context, (const uint8_t* const*)av_frame->data,
                         av_frame->linesize, 0, codec_context->height,
                         rgba_frame->data, rgba_frame->linesize);
                
                *frame_ptr = rgb_buffer;
                av_packet_unref(packet);
                return rgb_buffer_size;
            } else if (ret == AVERROR(EAGAIN)) {
                // Need more packets
                av_packet_unref(packet);
                continue;
            } else {
                fprintf(stderr, "Error receiving frame from decoder\n");
                av_packet_unref(packet);
                return -1;
            }
        }
        
        // Free the packet
        av_packet_unref(packet);
    }
    
    return -1; // Should never reach here
}

// Function to get the next frame from video file (PPM)
int get_next_video_file_frame(unsigned char** frame_ptr) {
    // For PPM files, read the RGB data directly
    if (fread(frame_data, 1, frame_width * frame_height * 3, video_file) != frame_width * frame_height * 3) {
        // End of file or error
        if (feof(video_file)) {
            // Rewind to the beginning of the file data (after the header)
            fseek(video_file, 0, SEEK_SET);
            
            // Skip the header again
            char line[100];
            fgets(line, sizeof(line), video_file); // P6
            
            // Skip comments
            int c = getc(video_file);
            while (c == '#') {
                while (getc(video_file) != '\n');
                c = getc(video_file);
            }
            ungetc(c, video_file);
            
            // Skip dimensions and max value
            fgets(line, sizeof(line), video_file);
            fgets(line, sizeof(line), video_file);
            
            // Read frame data again
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

// Generic function to get the next frame from any video source
int get_next_frame(unsigned char** frame_ptr) {
    switch (video_source_type) {
        case VIDEO_SOURCE_CAMERA:
            return get_next_camera_frame(frame_ptr);
        case VIDEO_SOURCE_FILE:
            return get_next_video_file_frame(frame_ptr);
        case VIDEO_SOURCE_MP4:
            return get_next_mp4_frame(frame_ptr);
        default:
            return -1;
    }
}

// Compile shader
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

// Initialize shaders
GLuint init_shaders() {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    
    GLuint program = glCreateProgram();
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
    
    return program;
}

// Initialize offscreen framebuffer
void init_framebuffer() {
    // Create a framebuffer
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    // Create a texture to render to
    glGenTextures(1, &output_texture);
    glBindTexture(GL_TEXTURE_2D, output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WINDOW_WIDTH, WINDOW_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Attach texture to framebuffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture, 0);
    
    // Check if framebuffer is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer is not complete!\n");
        exit(EXIT_FAILURE);
    }
    
    // Unbind framebuffer to return to default
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Initialize texture for video frames
void init_video_texture() {
    // Generate texture
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Allocate texture storage
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

void init_geometry() {
    // Define vertices for a fullscreen quad (two triangles)
    // Position (x,y,z) and texture coordinates (s,t)
    float vertices[] = {
        // Position      // Texcoords
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, // Bottom-left
        1.0f, -1.0f, 0.0f,   1.0f, 0.0f, // Bottom-right
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f, // Top-left
        1.0f,  1.0f, 0.0f,   1.0f, 1.0f  // Top-right
    };

    // Create and bind a buffer for the vertices
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
}

// Initialize Wayland display
int init_wayland() {
    // Connect to the display
    wl_display = wl_display_connect(NULL);
    if (!wl_display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return -1;
    }

    // Get the registry
    struct wl_registry *registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_dispatch(wl_display);
    wl_display_roundtrip(wl_display);

    if (!compositor || !shell) {
        fprintf(stderr, "Failed to get compositor or shell\n");
        return -1;
    }

    // Create surface
    wl_surface = wl_compositor_create_surface(compositor);
    if (!wl_surface) {
        fprintf(stderr, "Failed to create wayland surface\n");
        return -1;
    }

    // Create shell surface
    shell_surface = wl_shell_get_shell_surface(shell, wl_surface);
    if (!shell_surface) {
        fprintf(stderr, "Failed to get shell surface\n");
        return -1;
    }

    // Set the surface as top level
    wl_shell_surface_set_toplevel(shell_surface);

    // Create the EGL window
    wl_egl_window = wl_egl_window_create(wl_surface, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!wl_egl_window) {
        fprintf(stderr, "Failed to create EGL window\n");
        return -1;
    }

    return 0;
}

// Initialize X11 display
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

    // Find an appropriate visual
    XVisualInfo visTemplate;
    visTemplate.screen = screen;
    int num_visuals;
    x_visual_info = XGetVisualInfo(x_display, VisualScreenMask, &visTemplate, &num_visuals);

    if (!x_visual_info) {
        fprintf(stderr, "Failed to get X visual info\n");
        return -1;
    }

    // Create colormap
    x_colormap = XCreateColormap(x_display, root, x_visual_info->visual, AllocNone);

    // Set window attributes
    attr.colormap = x_colormap;
    attr.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;

    // Create window
    x_window = XCreateWindow(x_display, root,
            0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
            x_visual_info->depth, InputOutput,
            x_visual_info->visual,
            CWColormap | CWEventMask, &attr);

    // Set window title
    XStoreName(x_display, x_window, "Video Player");

    // Show the window
    XMapWindow(x_display, x_window);
    XFlush(x_display);

    // Wait for window to be mapped
    XGetWindowAttributes(x_display, x_window, &window_attributes);
    while (window_attributes.map_state != IsViewable) {
        XGetWindowAttributes(x_display, x_window, &window_attributes);
    }

    return 0;
}

// Initialize EGL
int init_egl() {
    EGLint major, minor, count, n, size;
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

    // Get EGL display
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

    // Initialize EGL
    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return -1;
    }

    printf("EGL version: %d.%d\n", major, minor);

    // Get number of configs
    if (!eglGetConfigs(egl_display, NULL, 0, &count)) {
        fprintf(stderr, "Failed to get EGL config count\n");
        return -1;
    }

    // Allocate memory for configs
    configs = malloc(count * sizeof(EGLConfig));

    // Get configs
    if (!eglChooseConfig(egl_display, config_attribs, configs, count, &n)) {
        fprintf(stderr, "Failed to get EGL configs\n");
        free(configs);
        return -1;
    }

    // Choose the first config
    if (n < 1) {
        fprintf(stderr, "No suitable EGL configs found\n");
        free(configs);
        return -1;
    }

    egl_config = configs[config_index];
    free(configs);

    // Create EGL context
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        return -1;
    }

    // Create EGL surface
    if (display_server_type == DISPLAY_WAYLAND) {
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)wl_egl_window, NULL);
    } else if (display_server_type == DISPLAY_X11) {
        egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)x_window, NULL);
    }

    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        return -1;
    }

    // Make current
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        return -1;
    }

    return 0;
}

// Clean up video sources
void cleanup_video_source() {
    switch (video_source_type) {
        case VIDEO_SOURCE_CAMERA:
            if (video_fd >= 0) {
                enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                // Stop streaming
                ioctl(video_fd, VIDIOC_STREAMOFF, &type);

                // Unmap buffers
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
            break;

        default:
            break;
    }
}

// Clean up display server
void cleanup_display() {
    // Clean up EGL
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

    // Clean up based on display server type
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

// Clean up OpenGL resources
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

// Main render loop
void render_loop() {
    unsigned char* frame;
    int frame_size;
    unsigned char* rgba_data = NULL;

    // Event handling for X11
    XEvent xev;

    struct timespec start_time, end_time;
    double elapsed;

    while (running) {
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        // Process X11 events if using X11
        if (display_server_type == DISPLAY_X11) {
            while (XPending(x_display)) {
                XNextEvent(x_display, &xev);

                // Handle key press events
                if (xev.type == KeyPress) {
                    running = 0;
                    break;
                }
            }
        }

        // Process Wayland events if using Wayland
        if (display_server_type == DISPLAY_WAYLAND) {
            wl_display_dispatch_pending(wl_display);
        }

        // Get next video frame
        frame_size = get_next_frame(&frame);
        if (frame_size < 0) {
            fprintf(stderr, "Failed to get next frame\n");
            break;
        }

        // Convert frame data to RGBA if needed
        if (video_source_type == VIDEO_SOURCE_CAMERA) {
            // Handle different pixel formats
            if (video_format == V4L2_PIX_FMT_YUYV) {
                // Convert YUYV to RGBA
                if (!rgba_data) {
                    rgba_data = (unsigned char*)malloc(frame_width * frame_height * 4);
                }

                // YUYV to RGBA conversion (simplified)
                for (int i = 0; i < frame_height; i++) {
                    for (int j = 0; j < frame_width; j += 2) {
                        int index = i * frame_width * 2 + j * 2;
                        int y0 = frame[index];
                        int u = frame[index + 1] - 128;
                        int y1 = frame[index + 2];
                        int v = frame[index + 3] - 128;

                        int r0 = y0 + 1.402 * v;
                        int g0 = y0 - 0.344 * u - 0.714 * v;
                        int b0 = y0 + 1.772 * u;

                        int r1 = y1 + 1.402 * v;
                        int g1 = y1 - 0.344 * u - 0.714 * v;
                        int b1 = y1 + 1.772 * u;

                        // Clamp values
                        r0 = r0 < 0 ? 0 : (r0 > 255 ? 255 : r0);
                        g0 = g0 < 0 ? 0 : (g0 > 255 ? 255 : g0);
                        b0 = b0 < 0 ? 0 : (b0 > 255 ? 255 : b0);

                        r1 = r1 < 0 ? 0 : (r1 > 255 ? 255 : r1);
                        g1 = g1 < 0 ? 0 : (g1 > 255 ? 255 : g1);
                        b1 = b1 < 0 ? 0 : (b1 > 255 ? 255 : b1);

                        // Set RGBA values for pixel 0
                        rgba_data[(i * frame_width + j) * 4] = r0;
                        rgba_data[(i * frame_width + j) * 4 + 1] = g0;
                        rgba_data[(i * frame_width + j) * 4 + 2] = b0;
                        rgba_data[(i * frame_width + j) * 4 + 3] = 255;

                        // Set RGBA values for pixel 1
                        rgba_data[(i * frame_width + j + 1) * 4] = r1;
                        rgba_data[(i * frame_width + j + 1) * 4 + 1] = g1;
                        rgba_data[(i * frame_width + j + 1) * 4 + 2] = b1;
                        rgba_data[(i * frame_width + j + 1) * 4 + 3] = 255;
                    }
                }

                frame = rgba_data;
            } else if (video_format == V4L2_PIX_FMT_MJPEG) {
                // MJPEG decoding would need an additional library like libjpeg
                // For simplicity, we'll skip this in this code
                fprintf(stderr, "MJPEG format not supported in this example\n");
                continue;
            }

            // Release camera frame
            release_camera_frame();
        } else if (video_source_type == VIDEO_SOURCE_FILE) {
            // Convert RGB to RGBA for PPM files
            if (!rgba_data) {
                rgba_data = (unsigned char*)malloc(frame_width * frame_height * 4);
            }

            // Simple RGB to RGBA conversion
            for (int i = 0; i < frame_width * frame_height; i++) {
                rgba_data[i*4]   = frame[i*3];     // R
                rgba_data[i*4+1] = frame[i*3+1];   // G
                rgba_data[i*4+2] = frame[i*3+2];   // B
                rgba_data[i*4+3] = 255;            // A (fully opaque)
            }

            frame = rgba_data;
        }
        // For VIDEO_SOURCE_MP4, the frame data is already in RGBA format

        // Update texture with new frame data
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame);

        // Clear the screen
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Use the shader program
        glUseProgram(program);

        // Set up the vertex attributes
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        // Position attribute
        GLint pos_attrib = glGetAttribLocation(program, "position");
        glEnableVertexAttribArray(pos_attrib);
        glVertexAttribPointer(pos_attrib, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);

        // Texture coordinate attribute
        GLint tex_attrib = glGetAttribLocation(program, "texcoord");
        glEnableVertexAttribArray(tex_attrib);
        glVertexAttribPointer(tex_attrib, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

        // Set texture uniform
        GLint tex_uniform = glGetUniformLocation(program, "texture");
        glUniform1i(tex_uniform, 0);  // GL_TEXTURE0

        // Draw two triangles (as triangle strip) to form a quad
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Disable vertex attributes
        glDisableVertexAttribArray(pos_attrib);
        glDisableVertexAttribArray(tex_attrib);

        // Swap buffers
        eglSwapBuffers(egl_display, egl_surface);

        // Calculate frame time and potentially sleep to maintain a target FPS
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        elapsed = (end_time.tv_sec - start_time.tv_sec) + 
            (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

        // Target ~30 FPS
        if (elapsed < 0.033) {
            usleep((0.033 - elapsed) * 1000000);
        }
    }

    // Free RGBA buffer if allocated
    if (rgba_data) {
        free(rgba_data);
    }
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <video_file.ppm|video_file.mp4|/dev/videoX>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* source_path = argv[1];

    // Detect if source is a camera device
    if (strncmp(source_path, "/dev/video", 10) == 0) {
        video_source_type = VIDEO_SOURCE_CAMERA;
        printf("Using camera source: %s\n", source_path);

        // Initialize camera
        if (init_camera(source_path) < 0) {
            fprintf(stderr, "Failed to initialize camera\n");
            return EXIT_FAILURE;
        }
    } else {
        // Check file extension to determine if it's MP4
        const char* ext = strrchr(source_path, '.');
        if (ext && strcasecmp(ext, ".mp4") == 0) {
            video_source_type = VIDEO_SOURCE_MP4;
        } else {
            video_source_type = VIDEO_SOURCE_FILE;
        }

        printf("Using video file source: %s\n", source_path);

        // Open video file
        if (open_video_file(source_path) < 0) {
            fprintf(stderr, "Failed to open video file\n");
            return EXIT_FAILURE;
        }
    }

    // Detect display server type
    display_server_type = detect_display_server();
    if (display_server_type == DISPLAY_UNKNOWN) {
        fprintf(stderr, "No supported display server detected\n");
        cleanup_video_source();
        return EXIT_FAILURE;
    }

    // Initialize display server specific components
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

    // Initialize EGL
    if (init_egl() < 0) {
        fprintf(stderr, "Failed to initialize EGL\n");
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    // Initialize shader program
    program = init_shaders();
    if (!program) {
        fprintf(stderr, "Failed to initialize shaders\n");
        cleanup_video_source();
        cleanup_display();
        return EXIT_FAILURE;
    }

    // Initialize geometry
    init_geometry();

    // Initialize framebuffer
    init_framebuffer();

    // Initialize video texture
    init_video_texture();

    // Main render loop
    render_loop();

    // Cleanup
    cleanup_gl();
    cleanup_video_source();
    cleanup_display();

    printf("Video player terminated\n");
    return EXIT_SUCCESS;
}
