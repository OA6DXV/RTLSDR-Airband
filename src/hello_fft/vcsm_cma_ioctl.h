/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright 2019 Raspberry Pi (Trading) Ltd.  All rights reserved.
 *
 * Based on vmcs_sm_ioctl.h Copyright Broadcom Corporation.
 */

#ifndef RTL_AIRBAND_VCSM_CMA_IOCTL_H
#define RTL_AIRBAND_VCSM_CMA_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define VC_SM_CMA_RESOURCE_NAME 32
#define VC_SM_CMA_MAGIC_TYPE 'J'

enum vc_sm_cma_cmd_e {
    VC_SM_CMA_CMD_ALLOC = 0x5A,
    VC_SM_CMA_CMD_IMPORT_DMABUF,
};

enum vc_sm_cma_cache_e {
    VC_SM_CMA_CACHE_NONE,
    VC_SM_CMA_CACHE_HOST,
    VC_SM_CMA_CACHE_VC,
    VC_SM_CMA_CACHE_BOTH,
};

struct vc_sm_cma_ioctl_import_dmabuf {
    __s32 dmabuf_fd;
    __u32 cached;
    __u8 name[VC_SM_CMA_RESOURCE_NAME];
    __s32 handle;
    __u32 vc_handle;
    __u32 size;
    __u32 pad;
    __u64 dma_addr;
};

#define VC_SM_CMA_IOCTL_MEM_IMPORT_DMABUF _IOR(VC_SM_CMA_MAGIC_TYPE, VC_SM_CMA_CMD_IMPORT_DMABUF, struct vc_sm_cma_ioctl_import_dmabuf)

#endif
