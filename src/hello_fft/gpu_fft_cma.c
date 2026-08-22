#include "gpu_fft_v4cma.h"
#include "mailbox.h"
#include "vcsm_cma_ioctl.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr const char* kCmaHeap = "/dev/dma_heap/linux,cma";
constexpr const char* kVcsmCma = "/dev/vcsm-cma";
pthread_mutex_t qpu_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned qpu_users = 0;

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

int acquire_qpu(int mb) {
    int ret = 0;

    pthread_mutex_lock(&qpu_mutex);
    if (qpu_users == 0 && qpu_enable(mb, 1) != 0) {
        ret = -1;
    } else {
        qpu_users++;
    }
    pthread_mutex_unlock(&qpu_mutex);
    return ret;
}

void release_qpu(int mb) {
    pthread_mutex_lock(&qpu_mutex);
    if (qpu_users > 0 && --qpu_users == 0) {
        qpu_enable(mb, 0);
    }
    pthread_mutex_unlock(&qpu_mutex);
}

void release_allocation(struct GPU_FFT_BASE* base) {
    int vcsm_import_fd = base->vcsm_import_fd;
    int vcsm_fd = base->vcsm_fd;
    int dma_buf_fd = base->dma_buf_fd;
    void* arm_map = base->arm_map;
    unsigned size = base->size;

    base->vcsm_import_fd = -1;
    base->vcsm_fd = -1;
    base->dma_buf_fd = -1;
    base->arm_map = nullptr;

    // The VCSM-imported dma-buf owns the VPU mapping. Release it before the
    // userspace mapping and the original dma-heap fd are dropped.
    close_fd(&vcsm_import_fd);
    if (arm_map != MAP_FAILED && arm_map != nullptr) {
        munmap(arm_map, size);
    }
    close_fd(&vcsm_fd);
    close_fd(&dma_buf_fd);
}

}

unsigned gpu_fft_base_exec(struct GPU_FFT_BASE* base, unsigned num_qpus) {
    unsigned ret;

    pthread_mutex_lock(&qpu_mutex);
    if (sync_dma_buf(base->dma_buf_fd, DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END) < 0) {
        ret = GPU_FFT_ERROR_SYNC_DEVICE;
    } else {
        ret = execute_qpu(base->mb, num_qpus, base->vc_msg, 1, 2000);
        if (sync_dma_buf(base->dma_buf_fd, DMA_BUF_SYNC_RW | DMA_BUF_SYNC_START) < 0 && ret == 0) {
            ret = GPU_FFT_ERROR_SYNC_CPU;
        }
    }
    pthread_mutex_unlock(&qpu_mutex);
    return ret;
}

int gpu_fft_alloc(int mb, unsigned size, struct GPU_FFT_PTR* ptr) {
    struct dma_heap_allocation_data allocation;
    struct vc_sm_cma_ioctl_import_dmabuf import;
    struct GPU_FFT_BASE allocation_base;
    void* arm_map;
    int heap_fd;
    int ret = -4;

    if (acquire_qpu(mb) != 0)
        return -1;

    heap_fd = open(kCmaHeap, O_RDWR | O_CLOEXEC);
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
    allocation_base.vcsm_import_fd = -1;
    allocation_base.arm_map = arm_map;
    allocation_base.size = size;
    if (allocation_base.vcsm_fd < 0)
        goto error_map;
    if (ioctl(allocation_base.vcsm_fd, VC_SM_CMA_IOCTL_MEM_IMPORT_DMABUF, &import) < 0)
        goto error_vcsm;

    allocation_base.vcsm_import_fd = import.handle;
    allocation_base.handle = import.vc_handle;
    if (allocation_base.vcsm_import_fd < 0 || import.dma_addr > UINT32_MAX || import.size < size)
        goto error_vcsm;
    if (sync_dma_buf(allocation.fd, DMA_BUF_SYNC_RW | DMA_BUF_SYNC_START) < 0)
        goto error_vcsm;

    uint32_t vc_addr = static_cast<uint32_t>(import.dma_addr);
    const uint32_t cache_alias = vc_addr & 0xC0000000U;
    if (cache_alias != 0xC0000000U && cache_alias != 0x80000000U)
        vc_addr |= 0xC0000000U;

    allocation_base.mb = mb;
    *reinterpret_cast<struct GPU_FFT_BASE*>(arm_map) = allocation_base;
    ptr->vc = vc_addr;
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
    release_qpu(mb);
    return ret;
}

void gpu_fft_base_release(struct GPU_FFT_BASE* base) {
    const int mb = base->mb;

    release_allocation(base);
    release_qpu(mb);
}

unsigned gpu_fft_ptr_inc(struct GPU_FFT_PTR* ptr, int bytes) {
    const unsigned vc = ptr->vc;

    ptr->vc += bytes;
    ptr->arm.bptr += bytes;
    return vc;
}
