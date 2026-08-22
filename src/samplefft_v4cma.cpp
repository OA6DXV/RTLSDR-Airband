/*
 * RTLSDR AM/NFM demodulator, mixer, streamer and recorder
 *
 * Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
 * Copyright (c) 2015-2021 Tomasz Lemiech <szpajder@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "rtl_airband.h"

#include <arm_neon.h>

extern "C" void samplefft(sample_fft_arg* a, unsigned char* buffer, float* window, float* levels) {
    const size_t sample_count = a->fft_size_by4 * 4;

    for (size_t i = 0; i < sample_count; i += 4, buffer += 8, window += 8) {
        const float32x4_t samples0 = {levels[buffer[0]], levels[buffer[1]], levels[buffer[2]], levels[buffer[3]]};
        const float32x4_t samples1 = {levels[buffer[4]], levels[buffer[5]], levels[buffer[6]], levels[buffer[7]]};
        const float32x4_t output0 = vmulq_f32(samples0, vld1q_f32(window));
        const float32x4_t output1 = vmulq_f32(samples1, vld1q_f32(window + 4));
        __builtin_memcpy(a->dest + i, &output0, sizeof(output0));
        __builtin_memcpy(a->dest + i + 2, &output1, sizeof(output1));
    }
}
