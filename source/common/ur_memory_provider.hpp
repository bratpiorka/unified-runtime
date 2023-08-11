/*
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM Exceptions.
 * See LICENSE.TXT
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <umf.h>
#include <umf/memory_provider.h>

#include "ur_api.h"

struct ur_provider_config_t {
    // NOTE: when the context is not NULL, create/get a provider instance
    // based on it
    ur_context_handle_t context;

    // when the context is NULL, create/get a provider instance based
    // on model, dev and PCI
    char *model_name;
    char *pci;

    // type of USM allocations
    ur_usm_type_t usm_type;
};

typedef struct ur_provider_priv_t {
    // TODO there will be always single dev per provider instance?
    ur_context_handle_t context;
    ur_device_handle_t device;
    ur_usm_type_t usm_type;
} ur_provider_priv_t;

enum umf_result_t ur_initialize(void *params, void **priv_ptr) {
    urInit(0, 0);

    if (priv_ptr == NULL) {
        return UMF_RESULT_ERROR_INVALID_ARGUMENT;
    }

    if (params == NULL) {
        return UMF_RESULT_ERROR_INVALID_ARGUMENT;
    }

    *(struct ur_provider_priv_t **)priv_ptr =
        (struct ur_provider_priv_t *)malloc(sizeof(struct ur_provider_priv_t));

    ur_provider_priv_t *priv = (struct ur_provider_priv_t *)*priv_ptr;
    if (priv == NULL) {
        return UMF_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    priv->usm_type = ((struct ur_provider_config_t *)params)->usm_type;

    if (((struct ur_provider_config_t *)params)->context) {
        priv->context = ((struct ur_provider_config_t *)params)->context;
        // get the devices list from the context
        size_t num_devices = 0;
        urContextGetInfo(priv->context, UR_CONTEXT_INFO_NUM_DEVICES,
                         sizeof(size_t), &num_devices, NULL);
        // assume there will be always single device per provider instance
        assert(num_devices == 1); // TODO report error
        urContextGetInfo(priv->context, UR_CONTEXT_INFO_DEVICES,
                         sizeof(ur_device_handle_t), &priv->device, NULL);
        assert(priv->device != NULL); // TODO report error

        return UMF_RESULT_SUCCESS;
    } else if (((struct ur_provider_config_t *)params)->pci) {
        // find a device that matches this name

        // NOTE: we browse all platforms here because right now
        // there is a single UR provider for all platforms

        uint32_t adapterCount = 0;
        urAdapterGet(0, NULL, &adapterCount);
        ur_adapter_handle_t *adapters = (ur_adapter_handle_t *)malloc(
            sizeof(ur_adapter_handle_t) * adapterCount);
        urAdapterGet(adapterCount, adapters, NULL);

        uint32_t platformCount = 0;
        urPlatformGet(adapters, adapterCount, 1, NULL, &platformCount);
        ur_platform_handle_t *platforms = (ur_platform_handle_t *)malloc(
            sizeof(ur_platform_handle_t) * platformCount);
        urPlatformGet(adapters, adapterCount, platformCount, platforms, NULL);

        for (size_t pid = 0; pid < platformCount; pid++) {
            uint32_t deviceCount = 0;
            urDeviceGet(platforms[pid], UR_DEVICE_TYPE_GPU, 0, NULL,
                        &deviceCount);
            ur_device_handle_t *devices = (ur_device_handle_t *)malloc(
                sizeof(ur_device_handle_t) * deviceCount);
            urDeviceGet(platforms[pid], UR_DEVICE_TYPE_GPU, deviceCount,
                        devices, NULL);

            for (size_t did = 0; did < deviceCount; did++) {
                static const size_t DEVICE_INFO_MAX_LEN = 1024;

                char *device_pci = (char *)malloc(DEVICE_INFO_MAX_LEN);
                memset(device_pci, 0, DEVICE_INFO_MAX_LEN);
                urDeviceGetInfo(devices[did], UR_DEVICE_INFO_PCI_ADDRESS,
                                DEVICE_INFO_MAX_LEN - 1, device_pci, NULL);

                if (strcmp(((struct ur_provider_config_t *)params)->pci,
                           device_pci) == 0) {
                    ur_context_handle_t ctx = NULL;
                    urContextCreate(1, &devices[did], NULL, &ctx);
                    priv->context = ctx;
                    priv->device = devices[did];

                    free(device_pci);
                    return UMF_RESULT_SUCCESS;
                }
            }
        }
    }

    // else
    return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;
}

void ur_finalize(void *provider) { free(provider); }

static enum umf_result_t ur_alloc(void *provider, size_t size, size_t alignment,
                                  void **resultPtr) {
    struct ur_provider_priv_t *config = (struct ur_provider_priv_t *)provider;
    enum umf_result_t result = UMF_RESULT_SUCCESS;

    // TODO check errors
    assert(config->context);

    switch (config->usm_type) {
    case UR_USM_TYPE_HOST:
        // TODO ur_usm_desc_t
        urUSMHostAlloc(config->context, NULL, NULL, size, resultPtr);
        break;
    case UR_USM_TYPE_DEVICE:
        urUSMDeviceAlloc(config->context, config->device, NULL, NULL, size,
                         resultPtr);
        break;
    case UR_USM_TYPE_SHARED:
        urUSMSharedAlloc(config->context, config->device, NULL, NULL, size,
                         resultPtr);
        break;
    default:
        assert(0);
    }

    return result;
}

static enum umf_result_t ur_free(void *provider, void *ptr, size_t size) {
    struct ur_provider_priv_t *config = (struct ur_provider_priv_t *)provider;

    // TODO check errors
    urUSMFree(config->context, ptr);

    // TODO - size?

    return UMF_RESULT_SUCCESS;
}

void ur_get_last_native_error(void *provider, const char **ppMessage,
                              int32_t *pError) {
    // TODO
}

enum umf_result_t ur_get_min_page_size(void *provider, void *ptr,
                                       size_t *pageSize) {
    *pageSize = 1024; // TODO call urVirtualMemGranularityGetInfo here
    return UMF_RESULT_SUCCESS;
}

const char *ur_get_name(void *provider) { return "USM"; }

bool ur_supports_device(const struct umf_memory_provider_config_t *config) {
    if (config->type != UMF_DEVICE_TYPE_GPU) {
        return false;
    }

    urInit(0, 0);

    uint32_t adapterCount = 0;
    urAdapterGet(0, NULL, &adapterCount);
    ur_adapter_handle_t *adapters = (ur_adapter_handle_t *)malloc(
        sizeof(ur_adapter_handle_t) * adapterCount);
    urAdapterGet(adapterCount, adapters, NULL);

    uint32_t platformCount = 0;
    urPlatformGet(adapters, adapterCount, 1, NULL, &platformCount);
    ur_platform_handle_t *platforms = (ur_platform_handle_t *)malloc(
        sizeof(ur_platform_handle_t) * platformCount);
    urPlatformGet(adapters, adapterCount, platformCount, platforms, NULL);

    for (uint32_t pid = 0; pid < platformCount; pid++) {
        uint32_t deviceCount = 0;
        urDeviceGet(platforms[pid], UR_DEVICE_TYPE_GPU, 0, NULL, &deviceCount);
        ur_device_handle_t *devices = (ur_device_handle_t *)malloc(
            sizeof(ur_device_handle_t) * deviceCount);
        urDeviceGet(platforms[pid], UR_DEVICE_TYPE_GPU, deviceCount, devices,
                    NULL);

        for (uint32_t did = 0; did < deviceCount; did++) {
            static const size_t DEVICE_INFO_MAX_LEN = 1024;

            char *device_name = (char *)malloc(DEVICE_INFO_MAX_LEN);
            memset(device_name, 0, DEVICE_INFO_MAX_LEN);
            urDeviceGetInfo(devices[did], UR_DEVICE_INFO_NAME,
                            DEVICE_INFO_MAX_LEN - 1, device_name, NULL);

            if (memcpy((void *)config->params.gpu.address, device_name,
                       sizeof(config->params.gpu.address)) == 0) {
                return true;
            }
        }
    }

    // no match
    return false;
}
