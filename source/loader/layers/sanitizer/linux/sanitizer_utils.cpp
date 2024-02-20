//===----------------------------------------------------------------------===//
/*
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Part of the Unified-Runtime Project, under the Apache License v2.0 with LLVM Exceptions.
 * See LICENSE.TXT
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * @file sanitizer_utils.cpp
 *
 */

#include "common.hpp"
#include "ur_sanitizer_layer.hpp"

#include <asm/param.h>
#include <dlfcn.h>
#include <gnu/lib-names.h>
#include <sys/mman.h>

extern "C" __attribute__((weak)) void __asan_init(void);

namespace ur_sanitizer_layer {

bool IsInASanContext() { return (void *)__asan_init != nullptr; }

bool MmapFixedNoReserve(uptr Addr, uptr Size) {
    Size = RoundUpTo(Size, EXEC_PAGESIZE);
    Addr = RoundDownTo(Addr, EXEC_PAGESIZE);
    void *P =
        mmap((void *)Addr, Size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE | MAP_ANONYMOUS, -1, 0);
    return Addr == (uptr)P;
}

bool MmapFixedNoAccess(uptr Addr, uptr Size) {
    void *P =
        mmap((void *)Addr, Size, PROT_NONE,
             MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE | MAP_ANONYMOUS, -1, 0);
    return Addr == (uptr)P;
}

bool Munmap(uptr Addr, uptr Size) { return munmap((void *)Addr, Size) == 0; }

void *GetMemFunctionPointer(const char *FuncName) {
    void *handle = dlopen(LIBC_SO, RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) {
        context.logger.error("Failed to dlopen {}", LIBC_SO);
        return nullptr;
    }
    auto ptr = dlsym(handle, FuncName);
    if (!ptr) {
        context.logger.error("Failed to get '{}' from {}", FuncName, LIBC_SO);
    }
    return ptr;
}

} // namespace ur_sanitizer_layer
