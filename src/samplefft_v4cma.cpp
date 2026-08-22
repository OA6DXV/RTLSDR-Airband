#include "rtl_airband.h"

#include <arm_neon.h>

extern "C" void samplefft(sample_fft_arg* a, unsigned char* buffer, float* window, float* levels) {
    float* dest = reinterpret_cast<float*>(a->dest);
    const size_t sample_count = a->fft_size_by4 * 4;

    for (size_t i = 0; i < sample_count; i += 4, buffer += 8, dest += 8, window += 8) {
        const float32x4_t samples0 = {levels[buffer[0]], levels[buffer[1]], levels[buffer[2]], levels[buffer[3]]};
        const float32x4_t samples1 = {levels[buffer[4]], levels[buffer[5]], levels[buffer[6]], levels[buffer[7]]};
        vst1q_f32(dest, vmulq_f32(samples0, vld1q_f32(window)));
        vst1q_f32(dest + 4, vmulq_f32(samples1, vld1q_f32(window + 4)));
    }
}
