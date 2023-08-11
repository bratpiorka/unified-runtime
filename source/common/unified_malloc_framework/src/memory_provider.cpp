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
#include <stdlib.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "umf/memory_provider.h"

#include "memory_provider_internal.h"

struct umf_memory_provider_t {
    struct umf_memory_provider_ops_t ops;
    void *provider_priv;
};

// TODO here I use the ptr to vector because the system calls
// globalProviders destructor twice - why?
typedef std::pair<umf_device_type_t, struct umf_memory_provider_ops_t>
    umf_provider_desc_pair_t;

std::vector<umf_provider_desc_pair_t> *globalProviders;

enum umf_result_t
umfMemoryProviderCreate(const struct umf_memory_provider_ops_t *ops,
                        void *params, umf_memory_provider_handle_t *hProvider) {
    umf_memory_provider_handle_t provider =
        (umf_memory_provider_t *)malloc(sizeof(struct umf_memory_provider_t));
    if (!provider) {
        return UMF_RESULT_ERROR_OUT_OF_HOST_MEMORY;
    }

    assert(ops->version == UMF_VERSION_CURRENT);

    provider->ops = *ops;

    void *provider_priv;
    enum umf_result_t ret = ops->initialize(params, &provider_priv);
    if (ret != UMF_RESULT_SUCCESS) {
        free(provider);
        return ret;
    }

    provider->provider_priv = provider_priv;

    *hProvider = provider;

    return UMF_RESULT_SUCCESS;
}

enum umf_result_t umfMemoryProviderRegister(umf_device_type_t type,
                                            umf_memory_provider_ops_t *ops) {

    if (globalProviders == NULL) {
        // TODO this is never freed
        globalProviders = new std::vector<umf_provider_desc_pair_t>;
    }

    // TODO check if this provider isn't already registered
    globalProviders->push_back(std::make_pair(type, *ops));

    return UMF_RESULT_SUCCESS;
}

enum umf_result_t umfMemoryProvidersCreateFromConfig(
    const umf_memory_provider_config_t *config,
    umf_memory_provider_handle_t *hProvider /* out */) {
    if (globalProviders == NULL) {
        return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;
    }

    if (hProvider != NULL) {
        return UMF_RESULT_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < globalProviders->size(); i++) {
        if (globalProviders->at(i).first == config->type &&
            globalProviders->at(i).second.supports_device(config)) {
            enum umf_result_t ret = umfMemoryProviderCreate(
                &globalProviders->at(i).second, (void *)config, hProvider);
            if (ret == UMF_RESULT_SUCCESS) {
                return ret;
            }
        }
    }

    return UMF_RESULT_ERROR_MEMORY_PROVIDER_SPECIFIC;
}

void umfMemoryProviderDestroy(umf_memory_provider_handle_t hProvider) {
    hProvider->ops.finalize(hProvider->provider_priv);
    free(hProvider);
}

static void
checkErrorAndSetLastProvider(enum umf_result_t result,
                             umf_memory_provider_handle_t hProvider) {
    if (result != UMF_RESULT_SUCCESS) {
        *umfGetLastFailedMemoryProviderPtr() = hProvider;
    }
}

enum umf_result_t umfMemoryProviderAlloc(umf_memory_provider_handle_t hProvider,
                                         size_t size, size_t alignment,
                                         void **ptr) {
    enum umf_result_t res =
        hProvider->ops.alloc(hProvider->provider_priv, size, alignment, ptr);
    checkErrorAndSetLastProvider(res, hProvider);
    return res;
}

enum umf_result_t umfMemoryProviderFree(umf_memory_provider_handle_t hProvider,
                                        void *ptr, size_t size) {
    enum umf_result_t res =
        hProvider->ops.free(hProvider->provider_priv, ptr, size);
    checkErrorAndSetLastProvider(res, hProvider);
    return res;
}

void umfMemoryProviderGetLastNativeError(umf_memory_provider_handle_t hProvider,
                                         const char **ppMessage,
                                         int32_t *pError) {
    hProvider->ops.get_last_native_error(hProvider->provider_priv, ppMessage,
                                         pError);
}

void *umfMemoryProviderGetPriv(umf_memory_provider_handle_t hProvider) {
    return hProvider->provider_priv;
}

enum umf_result_t
umfMemoryProviderGetRecommendedPageSize(umf_memory_provider_handle_t hProvider,
                                        size_t size, size_t *pageSize) {
    enum umf_result_t res = hProvider->ops.get_recommended_page_size(
        hProvider->provider_priv, size, pageSize);
    checkErrorAndSetLastProvider(res, hProvider);
    return res;
}

enum umf_result_t
umfMemoryProviderGetMinPageSize(umf_memory_provider_handle_t hProvider,
                                void *ptr, size_t *pageSize) {
    enum umf_result_t res = hProvider->ops.get_min_page_size(
        hProvider->provider_priv, ptr, pageSize);
    checkErrorAndSetLastProvider(res, hProvider);
    return res;
}

enum umf_result_t
umfMemoryProviderPurgeLazy(umf_memory_provider_handle_t hProvider, void *ptr,
                           size_t size) {
    enum umf_result_t res =
        hProvider->ops.purge_lazy(hProvider->provider_priv, ptr, size);
    checkErrorAndSetLastProvider(res, hProvider);
    return res;
}

enum umf_result_t
umfMemoryProviderPurgeForce(umf_memory_provider_handle_t hProvider, void *ptr,
                            size_t size) {
    enum umf_result_t res =
        hProvider->ops.purge_force(hProvider->provider_priv, ptr, size);
    checkErrorAndSetLastProvider(res, hProvider);
    return res;
}

const char *umfMemoryProviderGetName(umf_memory_provider_handle_t hProvider) {
    return hProvider->ops.get_name(hProvider->provider_priv);
}

umf_memory_provider_handle_t umfGetLastFailedMemoryProvider(void) {
    return *umfGetLastFailedMemoryProviderPtr();
}
