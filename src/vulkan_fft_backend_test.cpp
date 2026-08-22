#include "vulkan_fft.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int verify_cpu_vulkan_is_rejected(const char* requested_device) {
    if (requested_device != NULL)
        setenv("RTL_AIRBAND_VULKAN_DEVICE", requested_device, 1);
    else
        unsetenv("RTL_AIRBAND_VULKAN_DEVICE");

    unsetenv("RTL_AIRBAND_VULKAN_DISABLE");

    VulkanFFT fft(256, 2);
    if (!fft.initialize()) {
        std::cerr << "FFT initialization failed: " << fft.last_error() << '\n';
        return 1;
    }

    if (fft.using_vulkan()) {
        std::cerr << "Software Vulkan device was incorrectly selected: "
                  << fft.device_name() << '\n';
        return 2;
    }

    if (fft.device_name() != "FFTW3F CPU fallback") {
        std::cerr << "Expected FFTW fallback, got: " << fft.device_name() << '\n';
        return 3;
    }

    if (fft.fallback_reason().empty()) {
        std::cerr << "Vulkan fallback reason was not recorded\n";
        return 4;
    }

    std::cout << "PASS: Vulkan software device rejected; backend="
              << fft.device_name() << "; reason=" << fft.fallback_reason() << '\n';
    return 0;
}

}  // namespace

int main() {
    int result = verify_cpu_vulkan_is_rejected(NULL);
    if (result != 0)
        return result;

    result = verify_cpu_vulkan_is_rejected("llvmpipe");
    if (result != 0)
        return 10 + result;

    return 0;
}
