#include "gpu_fft_v4cma.h"
#include "mailbox.h"
#include "vcsm_cma_ioctl.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr const char* kCmaHeap = "/dev/dma_heap/linux,cma";
constexpr const char* kVcsmCma = "/dev/vcsm-cma";

void close_fd(int* fd) {
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

int sync_dma_buf(int fd, __u64 flags) {
    struct dma_buf_sync sync;

    std::memset(&sync, 0, sizeof(sync));
    sync.flags = flags;
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

void release_allocation(struct GPU_FFT_BASE* base) {
    int vcsm_fd = base->vcsm_fd;
    int dma_buf_fd = base->dma_buf_fd;
    void* arm_map = base->arm_map;
    unsigned size = base->size;

    base->vcsm_fd = -1;
    base->dma_buf_fd = -1;
    base->arm_map = nullptr;
    if (arm_map != MAP_FAILED && arm_map != nullptr) {
        munmap(arm_map, size);
    }
    close_fd(&vcsm_fd);
    close_fd(&dma_buf_fd);
}

}

unsigned gpu_fft_base_exec(struct GPU_FFT_BASE* base, unsigned num_qpus) {
    sync_dma_buf(base->dma_buf_fd, DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END);
    unsigned ret = execute_qpu(base->mb, num_qpus, base->vc_msg, 1, 2000);
    sync_dma_buf(base->dma_buf_fd, DMA_BUF_SYNC_RW | DMA_BUF_SYNC_START);
    return ret;
}

int gpu_fft_alloc(int mb, unsigned size, struct GPU_FFT_PTR* ptr) {
    struct dma_heap_allocation_data allocation;
    struct vc_sm_cma_ioctl_import_dmabuf import;
    struct GPU_FFT_BASE allocation_base;
    void* arm_map;
    int heap_fd;
    int ret = -4;

    if (qpu_enable(mb, 1) != 0)
        return -1;

    heap_fd = open(kCmaHeap, O_RDONLY | O_CLOEXEC);
    if (heap_fd < 0)
        goto error_qpu;

    std::memset(&allocation, 0, sizeof(allocation));
    allocation.len = size;
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &allocation) < 0) {
        ret = -3;
        goto error_heap;
    }
    close(heap_fd);

    arm_map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, allocation.fd, 0);
    if (arm_map == MAP_FAILED) {
        ret = -3;
        goto error_dma_buf;
    }

    std::memset(&import, 0, sizeof(import));
    import.dmabuf_fd = allocation.fd;
    import.cached = VC_SM_CMA_CACHE_BOTH;
    std::memcpy(import.name, "rtl-airband-fft", sizeof("rtl-airband-fft"));

    std::memset(&allocation_base, 0, sizeof(allocation_base));
    allocation_base.dma_buf_fd = allocation.fd;
    allocation_base.vcsm_fd = open(kVcsmCma, O_RDWR | O_CLOEXEC);
    allocation_base.arm_map = arm_map;
    allocation_base.size = size;
    if (allocation_base.vcsm_fd < 0)
        goto error_map;
    if (ioctl(allocation_base.vcsm_fd, VC_SM_CMA_IOCTL_MEM_IMPORT_DMABUF, &import) < 0)
        goto error_vcsm;
    if (import.dma_addr > UINT32_MAX)
        goto error_vcsm;
    if (sync_dma_buf(allocation.fd, DMA_BUF_SYNC_RW | DMA_BUF_SYNC_START) < 0)
        goto error_vcsm;

    allocation_base.mb = mb;
    allocation_base.handle = static_cast<unsigned>(import.handle);
    *reinterpret_cast<struct GPU_FFT_BASE*>(arm_map) = allocation_base;
    ptr->vc = static_cast<unsigned>(import.dma_addr);
    ptr->arm.vptr = arm_map;
    return 0;

error_vcsm:
    release_allocation(&allocation_base);
    goto error_qpu;
error_map:
    munmap(arm_map, size);
error_dma_buf:
    close(allocation.fd);
    goto error_qpu;
error_heap:
    close(heap_fd);
error_qpu:
    qpu_enable(mb, 0);
    return ret;
}

void gpu_fft_base_release(struct GPU_FFT_BASE* base) {
    const int mb = base->mb;

    release_allocation(base);
    qpu_enable(mb, 0);
}

unsigned gpu_fft_ptr_inc(struct GPU_FFT_PTR* ptr, int bytes) {
    const unsigned vc = ptr->vc;

    ptr->vc += bytes;
    ptr->arm.bptr += bytes;
    return vc;
}
