/*
 *
 * Copyright (C) 2022-2023 Intel Corporation
 *
 * Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM Exceptions.
 * See LICENSE.TXT
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 */
#include "ur_loader.hpp"
#include "ur_memory_provider.hpp"

namespace ur_loader {
///////////////////////////////////////////////////////////////////////////////
context_t *context;

///////////////////////////////////////////////////////////////////////////////
ur_result_t context_t::init() {
    for (const auto &adapterPaths : adapter_registry) {
        for (const auto &path : adapterPaths) {
            auto handle = LibLoader::loadAdapterLibrary(path.string().c_str());
            if (handle) {
                platforms.emplace_back(std::move(handle));
                break;
            }
        }
    }

    forceIntercept = getenv_tobool("UR_ENABLE_LOADER_INTERCEPT");

    if (forceIntercept || platforms.size() > 1) {
        intercept_enabled = true;
    }

    // create register UMF providers
    uint32_t adapterCount = 0;
    std::vector<ur_adapter_handle_t> adapters;
    urAdapterGet(0, nullptr, &adapterCount);
    adapters.resize(adapterCount);
    urAdapterGet(adapterCount, adapters.data(), nullptr);
    for (auto a : adapters) {

        struct umf_memory_provider_ops_t ops = {
            .version = UMF_VERSION_CURRENT,
            .initialize = ur_initialize,
            .finalize = ur_finalize,
            .alloc = ur_alloc,
            .free = ur_free,
            .get_last_native_error = ur_get_last_native_error,
            .get_min_page_size = ur_get_min_page_size,
            .get_name = ur_get_name,
            .supports_device = ur_supports_device,
        };

        umfMemoryProviderRegister(&ops);
    }

    return UR_RESULT_SUCCESS;
}

} // namespace ur_loader
