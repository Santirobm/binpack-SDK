/**
 * @file binpack_cpu.c
 * @brief BinPack Enterprise SDK - Hardware Capability & OS Feature Detection
 *
 * Implements thread-safe, immutable runtime dispatch for SIMD extensions (AVX2).
 * Verifies CPUID capabilities, OSXSAVE, and OS-managed extended context (XGETBV).
 */

#include "binpack_internal.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(_MSC_VER)
        #include <intrin.h>
        static inline void bp_cpuid_ex(int info[4], int func, int subfunc) {
            __cpuidex(info, func, subfunc);
        }
        static inline uint64_t bp_xgetbv_val(uint32_t xcr) {
            return _xgetbv(xcr);
        }
    #elif defined(__GNUC__) || defined(__clang__)
        #include <cpuid.h>
        static inline void bp_cpuid_ex(int info[4], int func, int subfunc) {
            __cpuid_count(func, subfunc, info[0], info[1], info[2], info[3]);
        }
        static inline uint64_t bp_xgetbv_val(uint32_t xcr) {
            uint32_t eax, edx;
            __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr));
            return ((uint64_t)edx << 32) | eax;
        }
    #endif
#endif

/* Global immutable dispatch table initialized by default with portable scalar kernels */
binpack_dispatch_table_t g_bp_dispatch = {
    bp_bcj_aarch64_filter_scalar,
    bp_bcj_aarch64_unfilter_scalar,
    bp_bcj_riscv_filter_scalar,
    bp_bcj_riscv_unfilter_scalar,
    bp_bitshuffle_filter_scalar,
    bp_bitshuffle_unfilter_scalar,
    BINPACK_BACKEND_SCALAR
};

#if defined(_WIN32)
static INIT_ONCE s_dispatch_once = INIT_ONCE_STATIC_INIT;
#else
static pthread_once_t s_dispatch_once = PTHREAD_ONCE_INIT;
#endif

static int bp_detect_avx2_hardware_and_os(void) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    int info[4] = {0};

    /* 1. Verify max basic CPUID level supports Leaf 7 */
    bp_cpuid_ex(info, 0, 0);
    if (info[0] < 7) {
        return 0;
    }

    /* 2. CPUID Leaf 1: OSXSAVE (bit 27) and AVX (bit 28) */
    bp_cpuid_ex(info, 1, 0);
    int osxsave_mask = (1 << 27);
    int avx_mask     = (1 << 28);
    if ((info[2] & (osxsave_mask | avx_mask)) != (osxsave_mask | avx_mask)) {
        return 0;
    }

    /* 3. XGETBV(0): Verify operating system has enabled XMM (bit 1) and YMM (bit 2) state */
    uint64_t xcr0 = bp_xgetbv_val(0);
    if ((xcr0 & 0x06) != 0x06) {
        return 0;
    }

    /* 4. CPUID Leaf 7, Subleaf 0: EBX bit 5 (AVX2) */
    bp_cpuid_ex(info, 7, 0);
    if (!(info[1] & (1 << 5))) {
        return 0;
    }

    return 1;
#else
    /* Non-x86 architectures (e.g. ARM64, RISC-V) do not execute x86 AVX2 kernels */
    return 0;
#endif
}

static void bp_init_dispatch_internal(void) {
    /* 1. Check environment overrides for testing / fallback enforcement */
    const char *force_scalar = getenv("BINPACK_FORCE_SCALAR");
    if (!force_scalar) {
        force_scalar = getenv("BINPACK_DISABLE_AVX2");
    }
    if (force_scalar != NULL && strcmp(force_scalar, "0") != 0) {
        /* User or test explicitly requested scalar fallback */
        g_bp_dispatch.backend_id = BINPACK_BACKEND_SCALAR;
        return;
    }

    /* 2. Detect CPU and OS capabilities */
    if (bp_detect_avx2_hardware_and_os()) {
#if defined(__x86_64__) || defined(_M_X64)
        g_bp_dispatch.bcj_aarch64_filter   = bp_bcj_aarch64_filter_avx2;
        g_bp_dispatch.bcj_aarch64_unfilter = bp_bcj_aarch64_unfilter_avx2;
        g_bp_dispatch.bcj_riscv_filter     = bp_bcj_riscv_filter_avx2;
        g_bp_dispatch.bcj_riscv_unfilter   = bp_bcj_riscv_unfilter_avx2;
        g_bp_dispatch.bitshuffle_filter    = bp_bitshuffle_filter_avx2;
        g_bp_dispatch.bitshuffle_unfilter  = bp_bitshuffle_unfilter_avx2;
        g_bp_dispatch.backend_id           = BINPACK_BACKEND_AVX2;
#endif
    }
}

#if defined(_WIN32)
static BOOL CALLBACK bp_dispatch_once_callback(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
    (void)InitOnce;
    (void)Parameter;
    (void)Context;
    bp_init_dispatch_internal();
    return TRUE;
}
#endif

void bp_ensure_cpu_dispatch_initialized(void) {
#if defined(_WIN32)
    InitOnceExecuteOnce(&s_dispatch_once, bp_dispatch_once_callback, NULL, NULL);
#else
    pthread_once(&s_dispatch_once, bp_init_dispatch_internal);
#endif
}

int bp_cpu_dispatch_get_active_backend(void) {
    bp_ensure_cpu_dispatch_initialized();
    return g_bp_dispatch.backend_id;
}
