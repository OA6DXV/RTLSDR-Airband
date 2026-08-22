#ifndef RTL_AIRBAND_GPU_FFT_V4CMA_H
#define RTL_AIRBAND_GPU_FFT_V4CMA_H

#define GPU_FFT_QPUS 8
#define GPU_FFT_PI 3.14159265358979323846
#define GPU_FFT_FWD 0
#define GPU_FFT_REV 1
#define GPU_FFT_ERROR_SYNC_DEVICE 0xfffffffeU
#define GPU_FFT_ERROR_SYNC_CPU 0xfffffffdU

struct GPU_FFT_COMPLEX {
    float re, im;
};

struct GPU_FFT_PTR {
    unsigned vc;
    union {
        struct GPU_FFT_COMPLEX* cptr;
        void* vptr;
        char* bptr;
        float* fptr;
        unsigned* uptr;
    } arm;
};

struct GPU_FFT_BASE {
    int mb;
    unsigned handle, size, vc_msg, vc_code, vc_unifs[GPU_FFT_QPUS];
    volatile unsigned* peri;
    int dma_buf_fd;
    int vcsm_fd;
    int vcsm_import_fd;
    void* arm_map;
};

struct GPU_FFT {
    struct GPU_FFT_BASE base;
    struct GPU_FFT_COMPLEX *in, *out;
    int x, y, step;
};

int gpu_fft_prepare(int mb, int log2_N, int direction, int jobs, struct GPU_FFT** fft);
unsigned gpu_fft_execute(struct GPU_FFT* info);
void gpu_fft_release(struct GPU_FFT* info);

int gpu_fft_twiddle_size(int, int*, int*, int*);
void gpu_fft_twiddle_data(int, int, float*);
unsigned int gpu_fft_shader_size(int);
unsigned int* gpu_fft_shader_code(int);

unsigned gpu_fft_base_exec(struct GPU_FFT_BASE* base, unsigned num_qpus);
int gpu_fft_alloc(int mb, unsigned size, struct GPU_FFT_PTR* ptr);
void gpu_fft_base_release(struct GPU_FFT_BASE* base);
unsigned gpu_fft_ptr_inc(struct GPU_FFT_PTR* ptr, int bytes);

#endif
