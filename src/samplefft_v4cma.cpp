#include "rtl_airband.h"

extern "C" void samplefft(sample_fft_arg* a, unsigned char* buffer, float* window, float* levels) {
    GPU_FFT_COMPLEX* dest = a->dest;
    const size_t sample_count = a->fft_size_by4 * 4;

    for (size_t i = 0; i < sample_count; i++, buffer += 2) {
        dest[i].re = levels[buffer[0]] * window[i * 2];
        dest[i].im = levels[buffer[1]] * window[i * 2 + 1];
    }
}
