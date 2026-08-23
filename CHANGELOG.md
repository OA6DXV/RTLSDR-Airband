# Changelog

## main - OA6DXV GPU acceleration release

This release publishes the GPU acceleration work from `vulkan-dev` into `main`.
The reported software version now keeps the upstream `git describe` value and
adds the `-OA6DXV` suffix.

### Vulkan FFT backend

- Added `PLATFORM=vulkan`, a headless Vulkan Compute FFT backend intended for
  non-Raspberry-Pi GPUs as well as ARM boards with Vulkan-capable Mali, Mesa,
  Radeon, Intel, or similar drivers.
- Targets Vulkan 1.0 so the backend can run on older or embedded Vulkan stacks
  instead of requiring newer desktop-only API levels.
- Compiles `src/vulkan_fft.comp` to SPIR-V at build time with either `glslc` or
  `glslangValidator`, then embeds the shader into the executable so no runtime
  shader file is needed.
- Selects a non-CPU Vulkan physical device with a compute queue, preferring
  discrete GPUs first, then integrated GPUs, and favoring compute-only queues
  when available.
- Batches multiple FFT transforms per command buffer and keeps input,
  intermediate, twiddle, and output data in mapped Vulkan storage buffers during
  each batch.
- Uses a specialization constant to choose a power-of-two compute workgroup size
  from the selected device limits.
- Adds runtime device selection through `RTL_AIRBAND_VULKAN_DEVICE` and a forced
  CPU comparison path through `RTL_AIRBAND_VULKAN_DISABLE=1`.
- Falls back automatically to batched FFTW3F when Vulkan initialization, device
  selection, memory allocation, shader setup, or execution is unavailable.
- Logs the selected Vulkan device, workgroup size, FFT batch size, or the
  fallback reason at startup.

### VideoCore IV CMA backend

- Added `PLATFORM=v4cma` for 64-bit Raspberry Pi userspaces on VideoCore IV
  systems, covering the gap where the legacy `rpiv2` backend remains tied to
  32-bit ARM builds.
- Added CMA/DMA-BUF allocation support through `/dev/dma_heap/linux,cma` and
  VCSM-CMA import handling for QPU-visible memory.
- Added a mailbox-only QPU execution path while keeping the existing legacy
  `rpiv2` source path intact.
- Added AArch64 sample packing in `samplefft_v4cma.cpp`, replacing the ARM32
  NEON routine where `PLATFORM=v4cma` is selected.
- Added DMA-BUF synchronization and hardened cleanup ordering for mmap,
  imported VCSM descriptors, and allocation file descriptors.
- Enables coordinated multi-threaded demodulation with `v4cma` and propagates
  GPU initialization/execution errors back to the demodulator path.

### Build system and platform selection

- Extended CMake platform handling to recognize `rpiv2`, `v4cma`, `vulkan`,
  `native`, and `generic` as the valid platform choices.
- Added `src/CMakeModules/detect_acceleration.cmake` to provide advisory
  acceleration hints without changing the selected `PLATFORM` automatically.
- Detects VideoCore IV systems from the Linux device tree and suggests `rpiv2`
  for 32-bit userspace or `v4cma` for 64-bit userspace.
- Detects non-CPU Vulkan devices with `vulkaninfo --summary` when available and
  suggests `PLATFORM=vulkan` for native/generic builds.
- Keeps FFTW3F as the normal CPU FFT path when neither Broadcom VideoCore nor
  Vulkan is selected, and as the runtime fallback for Vulkan builds.
- Adds `WITH_VULKAN` and VideoCore IV CMA state to generated configuration and
  the CMake configuration summary.

### Documentation and CI

- Added `docs/VULKAN.md` with build requirements, runtime environment
  variables, fallback behavior, and the initial performance model.
- Added Vulkan CI coverage for native amd64 plus ARM userspace builds.
- Added GPU/platform CI coverage for the new `v4cma` path and inherited GPU
  build matrix work.
- Added a small Vulkan backend test that verifies software Vulkan devices such
  as llvmpipe are rejected and that the FFTW fallback path remains usable.

### Compatibility notes

- The legacy `rpiv2` VideoCore path remains the dedicated 32-bit Raspberry Pi
  backend.
- `PLATFORM=v4cma` requires an AArch64 compiler and `linux/dma-heap.h`.
- `PLATFORM=vulkan` requires Vulkan headers, the Vulkan loader library, a target
  GPU ICD, `glslc` or `glslangValidator`, and FFTW3F for fallback.
- `multiple_demod_threads` remains unsupported on the old non-CMA Broadcom
  VideoCore path, while the `v4cma` path is wired for coordinated multi-threaded
  demodulation.

## gpu-dev

- Added `PLATFORM=v4cma` for AArch64 Raspberry Pi systems using VideoCore IV FFT through CMA/DMA-BUF.
- Added a VCSM-CMA allocation backend and mailbox-only QPU execution path without changing the legacy `rpiv2` sources.
- Added AArch64 NEON sample packing in place of the ARM32-only routine.
- Fixed CMA allocation cleanup after testing exposed an invalid unmap/descriptor-close order.
- Added coordinated multi-threaded demodulation and GPU error propagation for `v4cma`.
