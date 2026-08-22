#include <string.h>

#include "gpu_fft_v4cma.h"

typedef struct GPU_FFT_COMPLEX COMPLEX;

int gpu_fft_prepare(int mb, int log2_N, int direction, int jobs, struct GPU_FFT** fft) {
    unsigned info_bytes, twid_bytes, data_bytes, code_bytes, unif_bytes, mail_bytes;
    unsigned size, *uptr, vc_tw, vc_data;
    int i, q, shared, unique, passes, ret;
    struct GPU_FFT_BASE* base;
    struct GPU_FFT_PTR ptr;
    struct GPU_FFT* info;

    *fft = NULL;
    if (gpu_fft_twiddle_size(log2_N, &shared, &unique, &passes))
        return -2;

    info_bytes = 4096;
    data_bytes = 1 + ((sizeof(COMPLEX) << log2_N) | 4095);
    code_bytes = gpu_fft_shader_size(log2_N);
    twid_bytes = sizeof(COMPLEX) * 16 * (shared + GPU_FFT_QPUS * unique);
    unif_bytes = sizeof(int) * GPU_FFT_QPUS * (5 + jobs * 2);
    mail_bytes = sizeof(int) * GPU_FFT_QPUS * 2;
    size = info_bytes + data_bytes * jobs * 2 + code_bytes + twid_bytes + unif_bytes + mail_bytes;

    ret = gpu_fft_alloc(mb, size, &ptr);
    if (ret)
        return ret;

    info = (struct GPU_FFT*)ptr.arm.vptr;
    base = (struct GPU_FFT_BASE*)info;
    gpu_fft_ptr_inc(&ptr, info_bytes);
    info->x = 1 << log2_N;
    info->y = jobs;
    info->in = info->out = ptr.arm.cptr;
    info->step = data_bytes / sizeof(COMPLEX);
    if (passes & 1)
        info->out += info->step * jobs;
    vc_data = gpu_fft_ptr_inc(&ptr, data_bytes * jobs * 2);

    memcpy(ptr.arm.vptr, gpu_fft_shader_code(log2_N), code_bytes);
    base->vc_code = gpu_fft_ptr_inc(&ptr, code_bytes);
    gpu_fft_twiddle_data(log2_N, direction, ptr.arm.fptr);
    vc_tw = gpu_fft_ptr_inc(&ptr, twid_bytes);
    uptr = ptr.arm.uptr;

    for (q = 0; q < GPU_FFT_QPUS; q++) {
        *uptr++ = vc_tw;
        *uptr++ = vc_tw + sizeof(COMPLEX) * 16 * (shared + q * unique);
        *uptr++ = q;
        for (i = 0; i < jobs; i++) {
            *uptr++ = vc_data + data_bytes * i;
            *uptr++ = vc_data + data_bytes * i + data_bytes * jobs;
        }
        *uptr++ = 0;
        *uptr++ = (q == 0);
        base->vc_unifs[q] = gpu_fft_ptr_inc(&ptr, sizeof(int) * (5 + jobs * 2));
    }

    for (q = 0; q < GPU_FFT_QPUS; q++) {
        *uptr++ = base->vc_unifs[q];
        *uptr++ = base->vc_code;
    }
    base->vc_msg = ptr.vc;
    *fft = info;
    return 0;
}

unsigned gpu_fft_execute(struct GPU_FFT* info) {
    return gpu_fft_base_exec(&info->base, GPU_FFT_QPUS);
}

void gpu_fft_release(struct GPU_FFT* info) {
    gpu_fft_base_release(&info->base);
}
