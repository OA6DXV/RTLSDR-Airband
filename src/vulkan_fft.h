#ifndef RTL_AIRBAND_VULKAN_FFT_H
#define RTL_AIRBAND_VULKAN_FFT_H

#include <cstddef>
#include <memory>
#include <string>

struct VulkanComplex {
    float re;
    float im;
};

class VulkanFFT {
   public:
    VulkanFFT(size_t fft_size, size_t batch_size);
    ~VulkanFFT();

    bool initialize();
    bool execute();

    VulkanComplex* input();
    const VulkanComplex* output() const;

    bool using_vulkan() const;
    const std::string& device_name() const;
    const std::string& fallback_reason() const;
    const std::string& last_error() const;
    size_t workgroup_size() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    VulkanFFT(const VulkanFFT&);
    VulkanFFT& operator=(const VulkanFFT&);
};

#endif /* RTL_AIRBAND_VULKAN_FFT_H */
