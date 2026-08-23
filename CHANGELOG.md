# Changelog

## vulkan-dev

- Extended the Vulkan FFT backend to Vulkan 1.0 devices.
- Serialized FFTW fallback planning for multi-threaded demodulation.

## gpu-dev

- Added `PLATFORM=v4cma` for AArch64 Raspberry Pi systems using VideoCore IV FFT through CMA/DMA-BUF.
- Added a VCSM-CMA allocation backend and mailbox-only QPU execution path without changing the legacy `rpiv2` sources.
- Added AArch64 NEON sample packing in place of the ARM32-only routine.
- Fixed CMA allocation cleanup after testing exposed an invalid unmap/descriptor-close order.
- Added coordinated multi-threaded demodulation and GPU error propagation for `v4cma`.
