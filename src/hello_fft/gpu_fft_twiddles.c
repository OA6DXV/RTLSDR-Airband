/*
BCM2835 "GPU_FFT" release 2.0
Copyright (c) 2014, Andrew Holme.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>

#ifdef HAVE_VC4_CMA
#include "gpu_fft_v4cma.h"
#else
#include "gpu_fft.h"
#endif

#define ALPHA(dx) (2 * pow(sin((dx) / 2), 2))
#define BETA(dx) (sin(dx))

static double k[16] = {0, 8, 4, 4, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1};
static double m[16] = {0, 0, 0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 6, 7};

/****************************************************************************/

static float* twiddles_base_16(double two_pi, float* out, double theta) {
    int i;
    for (i = 0; i < 16; i++) {
        *out++ = cos(two_pi / 16 * k[i] * m[i] + theta * k[i]);
        *out++ = sin(two_pi / 16 * k[i] * m[i] + theta * k[i]);
    }
    return out;
}

static float* twiddles_base_32(double two_pi, float* out, double theta) {
    int i;
    for (i = 0; i < 16; i++) {
        *out++ = cos(two_pi / 32 * i + theta);
        *out++ = sin(two_pi / 32 * i + theta);
    }
    return twiddles_base_16(two_pi, out, 2 * theta);
}

static float* twiddles_base_64(double two_pi, float* out) {
    int i;
    for (i = 0; i < 32; i++) {
        *out++ = cos(two_pi / 64 * i);
        *out++ = sin(two_pi / 64 * i);
    }
    return twiddles_base_32(two_pi, out, 0);
}

/****************************************************************************/

static float* twiddles_step_16(double /*two_pi*/, float* out, double theta) {
    int i;
    for (i = 0; i < 16; i++) {
        *out++ = ALPHA(theta * k[i]);
        *out++ = BETA(theta * k[i]);
    }
    return out;
}

static float* twiddles_step_32(double two_pi, float* out, double theta) {
    int i;
    for (i = 0; i < 16; i++) {
        *out++ = ALPHA(theta);
        *out++ = BETA(theta);
    }
    return twiddles_step_16(two_pi, out, 2 * theta);
}

/****************************************************************************/

static void twiddles_256(double two_pi, float* out) {
    double N = 256;
    int q;

    out = twiddles_base_16(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_512(double two_pi, float* out) {
    double N = 512;
    int q;

    out = twiddles_base_32(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_1k(double two_pi, float* out) {
    double N = 1024;
    int q;

    out = twiddles_base_32(two_pi, out, 0);
    out = twiddles_step_32(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_32(two_pi, out, two_pi / N * q);
}

static void twiddles_2k(double two_pi, float* out) {
    double N = 2048;
    int q;

    out = twiddles_base_64(two_pi, out);
    out = twiddles_step_32(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_32(two_pi, out, two_pi / N * q);
}

static void twiddles_4k(double two_pi, float* out) {
    double N = 4096;
    int q;

    out = twiddles_base_16(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_8k(double two_pi, float* out) {
    double N = 8192;
    int q;

    out = twiddles_base_32(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_16k(double two_pi, float* out) {
    double N = 16384;
    int q;

    out = twiddles_base_32(two_pi, out, 0);
    out = twiddles_step_32(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_32k(double two_pi, float* out) {
    double N = 32768;
    int q;

    out = twiddles_base_64(two_pi, out);
    out = twiddles_step_32(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_64k(double two_pi, float* out) {
    double N = 65536;
    int q;

    out = twiddles_base_16(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * 256);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_128k(double two_pi, float* out) {
    double N = 131072;
    int q;

    out = twiddles_base_32(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * 256);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_256k(double two_pi, float* out) {
    double N = 262144;
    int q;

    out = twiddles_base_32(two_pi, out, 0);
    out = twiddles_step_32(two_pi, out, two_pi / N * 256);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_512k(double two_pi, float* out) {
    double N = 524288;
    int q;

    out = twiddles_base_64(two_pi, out);
    out = twiddles_step_32(two_pi, out, two_pi / N * 256);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_1024k(double two_pi, float* out) {
    double N = 1048576;
    int q;

    out = twiddles_base_16(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * 4096);
    out = twiddles_step_16(two_pi, out, two_pi / N * 256);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

static void twiddles_2048k(double two_pi, float* out) {
    double N = 2097152;
    int q;

    out = twiddles_base_32(two_pi, out, 0);
    out = twiddles_step_16(two_pi, out, two_pi / N * 4096);
    out = twiddles_step_16(two_pi, out, two_pi / N * 256);
    out = twiddles_step_16(two_pi, out, two_pi / N * 16);
    out = twiddles_step_16(two_pi, out, two_pi / N * GPU_FFT_QPUS);

    for (q = 0; q < GPU_FFT_QPUS; q++)
        out = twiddles_base_16(two_pi, out, two_pi / N * q);
}

/****************************************************************************/

static void (*twiddles[])(double, float*) = {twiddles_256,  twiddles_512,  twiddles_1k,    twiddles_2k,    twiddles_4k,   twiddles_8k,   twiddles_16k,
                                            twiddles_32k,  twiddles_64k,  twiddles_128k,  twiddles_256k,  twiddles_512k, twiddles_1024k, twiddles_2048k};

void gpu_fft_twiddle_data(int log2_N, int direction, float* out) {
    double two_pi = 2 * GPU_FFT_PI;
    if (direction == GPU_FFT_FWD)
        two_pi = -two_pi;
    twiddles[log2_N - 8](two_pi, out);
}

int gpu_fft_twiddle_size(int log2_N, int* shared, int* unique, int* passes) {
    static int s[] = {1, 2, 3, 5, 6, 7, 9, 10, 11, 12, 14, 15, 16, 17};
    static int u[] = {1, 1, 2, 2, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1};
    static int p[] = {2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5};

    if (log2_N < 8 || log2_N > 21)
        return -1;
    *shared = s[log2_N - 8];
    *unique = u[log2_N - 8];
    *passes = p[log2_N - 8];
    return 0;
}
