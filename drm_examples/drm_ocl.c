#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <CL/cl.h>

#define MAX_ITERATIONS 100
#define SRC_WIDTH 640
#define SRC_HEIGHT 480
#define DST_WIDTH 1024
#define DST_HEIGHT 768
#define PIXEL_SIZE 4  // RGBA, 4 bytes per pixel

typedef struct {
    int width;
    int height;
    unsigned char* data;
} Resolution;

// OpenCL kernel for nearest-neighbor scaling
const char* kernelSource = "__kernel void scaleImage(__global const uchar* src, __global uchar* dst, "
                           "int srcWidth, int srcHeight, int dstWidth, int dstHeight) {"
                           "    int x = get_global_id(0);"
                           "    int y = get_global_id(1);"
                           "    if (x >= dstWidth || y >= dstHeight) return;"
                           "    float x_ratio = (float)srcWidth / dstWidth;"
                           "    float y_ratio = (float)srcHeight / dstHeight;"
                           "    int srcX = (int)(x * x_ratio);"
                           "    int srcY = (int)(y * y_ratio);"
                           "    int srcIndex = (srcY * srcWidth + srcX) * 4;"
                           "    int dstIndex = (y * dstWidth + x) * 4;"
                           "    dst[dstIndex] = src[srcIndex];"
                           "    dst[dstIndex + 1] = src[srcIndex + 1];"
                           "    dst[dstIndex + 2] = src[srcIndex + 2];"
                           "    dst[dstIndex + 3] = src[srcIndex + 3];"
                           "}";

// Initialize source resolution
Resolution initResolution() {
    Resolution res;
    res.width = SRC_WIDTH;
    res.height = SRC_HEIGHT;
    size_t dataSize = res.width * res.height * PIXEL_SIZE;
    res.data = (unsigned char*)malloc(dataSize);
    if (!res.data) {
        printf("Memory allocation failed\n");
        exit(1);
    }
    for (size_t i = 0; i < dataSize; i++) {
        res.data[i] = rand() % 256;
    }
    printf("Initialized resolution: %dx%d\n", res.width, res.height);
    return res;
}

// Write resolution data to memory
void writeResolution(Resolution* res, unsigned char* destMemory) {
    size_t dataSize = res.width * res.height * PIXEL_SIZE;
    memcpy(destMemory, res.data, dataSize);
}

int main() {
    // Initialize source resolution
    Resolution srcRes = initResolution();

    // Allocate destination resolution
    Resolution dstRes;
    dstRes.width = DST_WIDTH;
    dstRes.height = DST_HEIGHT;
    size_t dstSize = DST_WIDTH * DST_HEIGHT * PIXEL_SIZE;
    dstRes.data = (unsigned char*)malloc(dstSize);
    if (!dstRes.data) {
        printf("Memory allocation failed for scaled resolution\n");
        free(srcRes.data);
        return 1;
    }

    unsigned char* destMemory = (unsigned char*)malloc(dstSize);
    if (!destMemory) {
        printf("Destination memory allocation failed\n");
        free(srcRes.data);
        free(dstRes.data);
        return 1;
    }

    // OpenCL setup
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem srcBuffer, dstBuffer;
    cl_int err;

    // Get platform and device
    clGetPlatformIDs(1, &platform, NULL);
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);

    // Create context and command queue
    context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    queue = clCreateCommandQueue(context, device, 0, &err);

    // Create program from kernel source
    program = clCreateProgramWithSource(context, 1, &kernelSource, NULL, &err);
    clBuildProgram(program, 1, &device, NULL, NULL, NULL);

    // Create kernel
    kernel = clCreateKernel(program, "scaleImage", &err);

    // Create buffers
    srcBuffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                               SRC_WIDTH * SRC_HEIGHT * PIXEL_SIZE, srcRes.data, &err);
    dstBuffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
                               DST_WIDTH * DST_HEIGHT * PIXEL_SIZE, NULL, &err);

    // Set kernel arguments
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &srcBuffer);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &dstBuffer);
    clSetKernelArg(kernel, 2, sizeof(int), &srcRes.width);
    clSetKernelArg(kernel, 3, sizeof(int), &srcRes.height);
    clSetKernelArg(kernel, 4, sizeof(int), &dstRes.width);
    clSetKernelArg(kernel, 5, sizeof(int), &dstRes.height);

    // Timing start
    clock_t start_time = clock();

    // Perform scaling iterations
    for (int i = 0; i < MAX_ITERATIONS; i++) {
        printf("Iteration %d\n", i + 1);

        // Execute kernel
        size_t globalWorkSize[2] = {DST_WIDTH, DST_HEIGHT};
        clEnqueueNDRangeKernel(queue, kernel, 2, NULL, globalWorkSize, NULL, 0, NULL, NULL);

        // Read result back to host
        clEnqueueReadBuffer(queue, dstBuffer, CL_TRUE, 0, dstSize, dstRes.data, 0, NULL, NULL);

        // Write to memory
        writeResolution(&dstRes, destMemory);
    }

    // Timing end
    clock_t end_time = clock();
    double total_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    // Cleanup OpenCL resources
    clReleaseMemObject(srcBuffer);
    clReleaseMemObject(dstBuffer);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    // Free host memory
    free(srcRes.data);
    free(dstRes.data);
    free(destMemory);

    printf("Completed %d read/scale/write operations in %.6f seconds\n", MAX_ITERATIONS, total_time);
    return 0;
}
