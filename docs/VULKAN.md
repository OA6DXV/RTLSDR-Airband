# Vulkan FFT backend

`PLATFORM=vulkan` builds RTLSDR-Airband with a headless Vulkan Compute FFT backend. The existing `rpiv2`, `v4cma`, `native`, and `generic` platform paths are unchanged.

## Design

The Vulkan backend:

- uses no window-system integration, surface, swapchain, X11, or Wayland;
- requires Vulkan 1.0 or newer;
- enumerates Vulkan physical devices at runtime and selects a non-CPU device with a compute queue;
- prefers discrete GPUs, then integrated GPUs, while preferring compute-only queues when available;
- batches FFT transforms in a single command buffer;
- keeps input, intermediate data, and FFT output in mapped Vulkan storage buffers during a batch;
- selects a power-of-two workgroup size from the selected device limits through a shader specialization constant;
- uses the same SPIR-V compute shader on all vendors;
- falls back to batched FFTW3F automatically if Vulkan initialization or device selection fails.

The fallback can be forced for comparison tests:

```sh
RTL_AIRBAND_VULKAN_DISABLE=1 rtl_airband -f
```

To select a particular Vulkan GPU, set a case-insensitive substring of its device name:

```sh
RTL_AIRBAND_VULKAN_DEVICE=Mali-G31 rtl_airband -f
RTL_AIRBAND_VULKAN_DEVICE=Radeon rtl_airband -f
```

Without that variable the device is selected automatically.

## Build requirements

In addition to the normal RTLSDR-Airband dependencies, the Vulkan build needs:

- Vulkan 1.0+ headers and loader;
- a Vulkan 1.0+ ICD for the target GPU;
- either `glslc` or `glslangValidator` at build time;
- FFTW3F for the automatic CPU fallback.

Typical Debian/Ubuntu development packages are:

```sh
sudo apt install libvulkan-dev glslc libfftw3-dev
```

`glslang-tools` may be used instead of `glslc`.

Configure with:

```sh
cmake -B build-vulkan -DCMAKE_BUILD_TYPE=Release -DPLATFORM=vulkan -S .
cmake --build build-vulkan -j$(nproc)
```

The compute shader is compiled to SPIR-V during the build and embedded in the executable, so no shader file is required at runtime.

## Runtime output

When Vulkan is selected, startup logging reports the detected device, workgroup size, and FFT batch size. If Vulkan is unavailable, startup logging reports the reason and the backend continues with FFTW3F.

## Initial performance model

The first implementation keeps the existing RTLSDR-Airband DSP behavior and only replaces the FFT engine. It batches multiple overlapping FFT windows per Vulkan submission to reduce command submission and synchronization overhead. Further optimization can move additional channel extraction or windowing work into compute shaders without changing the platform interface.
