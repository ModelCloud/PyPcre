// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#ifndef ATOMIC_COMPAT_H
#define ATOMIC_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================== */
/*                           MSVC IMPLEMENTATION                         */
/* ===================================================================== */
#if defined(_MSC_VER)

#if defined(__has_include)
#  if !defined(__STDC_NO_ATOMICS__) && __has_include(<stdatomic.h>)
#    include <stdatomic.h>
#    define ATOMIC_COMPAT_USE_NATIVE_ATOMICS 1
#  endif
#endif

#if defined(ATOMIC_COMPAT_USE_NATIVE_ATOMICS)

#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) _Atomic(type)
#ifndef ATOMIC_VAR_INIT
#  define ATOMIC_VAR_INIT(value) (value)
#endif

#else /* !ATOMIC_COMPAT_USE_NATIVE_ATOMICS */

#include <windows.h>
#include <intrin.h>

/* Feature flag & storage qualifier (GNU-style on MSVC). */
#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) volatile type
#ifndef ATOMIC_VAR_INIT
#  define ATOMIC_VAR_INIT(value) (value)
#endif

/* --------------------------------------------------------------------- */
/* 1) Nuke vcruntime <stdatomic.h> macros if they were included already. */
/*    This prevents expansions to __c11_atomic_* builtins on MSVC.       */
/* --------------------------------------------------------------------- */

#if defined(atomic_init)
#  undef atomic_init
#endif

#if defined(atomic_thread_fence)
#  undef atomic_thread_fence
#endif
#if defined(atomic_signal_fence)
#  undef atomic_signal_fence
#endif

#if defined(atomic_load)
#  undef atomic_load
#endif
#if defined(atomic_load_explicit)
#  undef atomic_load_explicit
#endif

#if defined(atomic_store)
#  undef atomic_store
#endif
#if defined(atomic_store_explicit)
#  undef atomic_store_explicit
#endif

#if defined(atomic_exchange)
#  undef atomic_exchange
#endif
#if defined(atomic_exchange_explicit)
#  undef atomic_exchange_explicit
#endif

#if defined(atomic_fetch_add)
#  undef atomic_fetch_add
#endif
#if defined(atomic_fetch_add_explicit)
#  undef atomic_fetch_add_explicit
#endif

#if defined(atomic_fetch_sub)
#  undef atomic_fetch_sub
#endif
#if defined(atomic_fetch_sub_explicit)
#  undef atomic_fetch_sub_explicit
#endif

#if defined(atomic_compare_exchange_strong)
#  undef atomic_compare_exchange_strong
#endif
#if defined(atomic_compare_exchange_strong_explicit)
#  undef atomic_compare_exchange_strong_explicit
#endif
#if defined(atomic_compare_exchange_weak)
#  undef atomic_compare_exchange_weak
#endif
#if defined(atomic_compare_exchange_weak_explicit)
#  undef atomic_compare_exchange_weak_explicit
#endif

#if defined(atomic_flag_test_and_set)
#  undef atomic_flag_test_and_set
#endif
#if defined(atomic_flag_test_and_set_explicit)
#  undef atomic_flag_test_and_set_explicit
#endif
#if defined(atomic_flag_clear)
#  undef atomic_flag_clear
#endif
#if defined(atomic_flag_clear_explicit)
#  undef atomic_flag_clear_explicit
#endif

/* --------------------------------------------------------------------- */
/* 2) C11 memory orders (accepted, enforced >= seq_cst where relevant).  */
/* --------------------------------------------------------------------- */
typedef enum {
    memory_order_relaxed = 0,
    memory_order_consume = 1, /* treat as acquire */
    memory_order_acquire = 2,
    memory_order_release = 3,
    memory_order_acq_rel = 4,
    memory_order_seq_cst = 5
} memory_order;

static __forceinline void atomic_thread_fence(memory_order order) {
    (void)order;
    /* Full fence (at-least seq_cst). */
    MemoryBarrier();
}
static __forceinline void atomic_signal_fence(memory_order order) {
    (void)order;
    /* Compiler barrier only. */
    _ReadWriteBarrier();
}

/* --------------------------------------------------------------------- */
/* 3) Low-level typed helpers backed by Interlocked primitives.          */
/*    (All provide at-least seq_cst.)                                    */
/* --------------------------------------------------------------------- */

/* 32-bit */
static __forceinline int32_t  ac_load_i32 (volatile int32_t *p)  { return (int32_t)InterlockedCompareExchange((volatile LONG*)p, 0, 0); }
static __forceinline uint32_t ac_load_u32 (volatile uint32_t *p) { return (uint32_t)InterlockedCompareExchange((volatile LONG*)p, 0, 0); }
static __forceinline void     ac_store_i32(volatile int32_t *p,  int32_t v)  { InterlockedExchange((volatile LONG*)p, (LONG)v); }
static __forceinline void     ac_store_u32(volatile uint32_t *p, uint32_t v) { InterlockedExchange((volatile LONG*)p, (LONG)v); }
static __forceinline int32_t  ac_xchg_i32 (volatile int32_t *p,  int32_t v)  { return (int32_t)InterlockedExchange((volatile LONG*)p, (LONG)v); }
static __forceinline uint32_t ac_xchg_u32 (volatile uint32_t *p, uint32_t v) { return (uint32_t)InterlockedExchange((volatile LONG*)p, (LONG)v); }
static __forceinline int32_t  ac_fadd_i32 (volatile int32_t *p,  int32_t v)  { return (int32_t)InterlockedExchangeAdd((volatile LONG*)p, (LONG)v); }
static __forceinline uint32_t ac_fadd_u32 (volatile uint32_t *p, uint32_t v) { return (uint32_t)InterlockedExchangeAdd((volatile LONG*)p, (LONG)v); }
static __forceinline int      ac_cas_i32  (volatile int32_t *p,  int32_t *e, int32_t d) {
    LONG orig = InterlockedCompareExchange((volatile LONG*)p, (LONG)d, (LONG)*e);
    if ((int32_t)orig == *e) return 1;
    *e = (int32_t)orig; return 0;
}
static __forceinline int      ac_cas_u32  (volatile uint32_t *p, uint32_t *e, uint32_t d) {
    LONG orig = InterlockedCompareExchange((volatile LONG*)p, (LONG)d, (LONG)*e);
    if ((uint32_t)orig == *e) return 1;
    *e = (uint32_t)orig; return 0;
}

/* 64-bit */
static __forceinline int64_t  ac_load_i64 (volatile int64_t *p)  { return (int64_t)InterlockedCompareExchange64((volatile LONGLONG*)p, 0, 0); }
static __forceinline uint64_t ac_load_u64 (volatile uint64_t *p) { return (uint64_t)InterlockedCompareExchange64((volatile LONGLONG*)p, 0, 0); }
static __forceinline void     ac_store_i64(volatile int64_t *p,  int64_t v)  { InterlockedExchange64((volatile LONGLONG*)p, (LONGLONG)v); }
static __forceinline void     ac_store_u64(volatile uint64_t *p, uint64_t v) { InterlockedExchange64((volatile LONGLONG*)p, (LONGLONG)v); }
static __forceinline int64_t  ac_xchg_i64 (volatile int64_t *p,  int64_t v)  { return (int64_t)InterlockedExchange64((volatile LONGLONG*)p, (LONGLONG)v); }
static __forceinline uint64_t ac_xchg_u64 (volatile uint64_t *p, uint64_t v) { return (uint64_t)InterlockedExchange64((volatile LONGLONG*)p, (LONGLONG)v); }
static __forceinline int64_t  ac_fadd_i64 (volatile int64_t *p,  int64_t v)  { return (int64_t)InterlockedExchangeAdd64((volatile LONGLONG*)p, (LONGLONG)v); }
static __forceinline uint64_t ac_fadd_u64 (volatile uint64_t *p, uint64_t v) { return (uint64_t)InterlockedExchangeAdd64((volatile LONGLONG*)p, (LONGLONG)v); }
static __forceinline int      ac_cas_i64  (volatile int64_t *p,  int64_t *e, int64_t d) {
    LONGLONG orig = InterlockedCompareExchange64((volatile LONGLONG*)p, (LONGLONG)d, (LONGLONG)*e);
    if ((int64_t)orig == *e) return 1;
    *e = (int64_t)orig; return 0;
}
static __forceinline int      ac_cas_u64  (volatile uint64_t *p, uint64_t *e, uint64_t d) {
    LONGLONG orig = InterlockedCompareExchange64((volatile LONGLONG*)p, (LONGLONG)d, (LONGLONG)*e);
    if ((uint64_t)orig == *e) return 1;
    *e = (uint64_t)orig; return 0;
}

/* size_t */
static __forceinline size_t ac_load_size (volatile size_t *p) {
#if defined(_WIN64)
    return (size_t)InterlockedCompareExchange64((volatile LONGLONG*)p, 0, 0);
#else
    return (size_t)InterlockedCompareExchange((volatile LONG*)p, 0, 0);
#endif
}
static __forceinline void   ac_store_size(volatile size_t *p, size_t v) {
#if defined(_WIN64)
    InterlockedExchange64((volatile LONGLONG*)p, (LONGLONG)v);
#else
    InterlockedExchange((volatile LONG*)p, (LONG)v);
#endif
}

/* pointers */
static __forceinline void *ac_load_ptr (void * volatile *p)       { return InterlockedCompareExchangePointer(p, NULL, NULL); }
static __forceinline void  ac_store_ptr(void * volatile *p, void *v) { InterlockedExchangePointer(p, v); }
static __forceinline void *ac_xchg_ptr (void * volatile *p, void *v) { return InterlockedExchangePointer(p, v); }
static __forceinline int   ac_cas_ptr  (void * volatile *p, void **e, void *d) {
    void *orig = InterlockedCompareExchangePointer(p, d, *e);
    if (orig == *e) return 1;
    *e = orig; return 0;
}

/* --------------------------------------------------------------------- */
/* 4) GNU-style, type-directed macros via _Generic (C mode on MSVC 2022).*/
/*    These work on *plain* volatile T* (no _Atomic required).           */
/* --------------------------------------------------------------------- */

/* Load */
#define atomic_compat_load(ptr) \
    _Generic((ptr), \
        /* signed */   volatile int32_t *:  ac_load_i32((volatile int32_t*)(ptr)), \
                      int32_t *:            ac_load_i32((volatile int32_t*)(ptr)), \
        /* unsigned */ volatile uint32_t *: ac_load_u32((volatile uint32_t*)(ptr)), \
                      uint32_t *:           ac_load_u32((volatile uint32_t*)(ptr)), \
        /* size_t  */ volatile size_t *:    ac_load_size((volatile size_t*)(ptr)), \
                      size_t *:              ac_load_size((volatile size_t*)(ptr)), \
        /* pointer */ default:               ac_load_ptr((void * volatile *)(ptr)) \
    )

/* Store */
#define atomic_compat_store(ptr, value) \
    _Generic((ptr), \
        /* signed */   volatile int32_t *:  ac_store_i32((volatile int32_t*)(ptr),  (int32_t)(value)), \
                      int32_t *:            ac_store_i32((volatile int32_t*)(ptr),  (int32_t)(value)), \
        /* unsigned */ volatile uint32_t *: ac_store_u32((volatile uint32_t*)(ptr), (uint32_t)(value)), \
                      uint32_t *:           ac_store_u32((volatile uint32_t*)(ptr), (uint32_t)(value)), \
        /* size_t  */ volatile size_t *:    ac_store_size((volatile size_t*)(ptr),  (size_t)(value)), \
                      size_t *:              ac_store_size((volatile size_t*)(ptr),  (size_t)(value)), \
        /* pointer */ default:               ac_store_ptr((void * volatile *)(ptr), (void*)(value)) \
    )

/* Exchange */
#define atomic_compat_exchange(ptr, value) \
    _Generic((ptr), \
        /* signed */   volatile int32_t *:  (int32_t) ac_xchg_i32((volatile int32_t*)(ptr),  (int32_t)(value)), \
                      int32_t *:            (int32_t) ac_xchg_i32((volatile int32_t*)(ptr),  (int32_t)(value)), \
        /* unsigned */ volatile uint32_t *: (uint32_t)ac_xchg_u32((volatile uint32_t*)(ptr), (uint32_t)(value)), \
                      uint32_t *:           (uint32_t)ac_xchg_u32((volatile uint32_t*)(ptr), (uint32_t)(value)), \
        /* pointer */ default:               ac_xchg_ptr((void * volatile *)(ptr), (void*)(value)) \
    )

/* Fetch add / sub (integers only) */
#define atomic_compat_fetch_add(ptr, value) \
    _Generic((ptr), \
        volatile int32_t *:  (int32_t) ac_fadd_i32((volatile int32_t*)(ptr),  (int32_t)(value)), \
                 int32_t *:  (int32_t) ac_fadd_i32((volatile int32_t*)(ptr),  (int32_t)(value)), \
        volatile uint32_t *: (uint32_t)ac_fadd_u32((volatile uint32_t*)(ptr), (uint32_t)(value)), \
                 uint32_t *: (uint32_t)ac_fadd_u32((volatile uint32_t*)(ptr), (uint32_t)(value)) \
    )

#define atomic_compat_fetch_sub(ptr, value) \
    _Generic((ptr), \
        volatile int32_t *:  (int32_t) ac_fadd_i32((volatile int32_t*)(ptr),  (int32_t)-(value)), \
                 int32_t *:  (int32_t) ac_fadd_i32((volatile int32_t*)(ptr),  (int32_t)-(value)), \
        volatile uint32_t *: (uint32_t)ac_fadd_u32((volatile uint32_t*)(ptr), (uint32_t)-(value)), \
                 uint32_t *: (uint32_t)ac_fadd_u32((volatile uint32_t*)(ptr), (uint32_t)-(value)) \
    )

/* Compare-exchange (C11 rules: update *expected on failure) */
#define atomic_compat_compare_exchange(ptr, expected, desired) \
    _Generic((ptr), \
        volatile int32_t *:  ac_cas_i32((volatile int32_t*)(ptr),  (int32_t*)(expected),  (int32_t)(desired)), \
                 int32_t *:  ac_cas_i32((volatile int32_t*)(ptr),  (int32_t*)(expected),  (int32_t)(desired)), \
        volatile uint32_t *: ac_cas_u32((volatile uint32_t*)(ptr), (uint32_t*)(expected), (uint32_t)(desired)), \
                 uint32_t *: ac_cas_u32((volatile uint32_t*)(ptr), (uint32_t*)(expected), (uint32_t)(desired)), \
        default:              ac_cas_ptr((void * volatile *)(ptr), (void**)(expected),    (void*)(desired)) \
    )

/* --------------------------------------------------------------------- */
/* 5) Public C11-like API (explicit + default seq_cst variants).         */
/*    These *do not* require _Atomic-qualified objects on MSVC.          */
/* --------------------------------------------------------------------- */

#define atomic_init(obj, value) \
    atomic_compat_store((obj), (value))

#define atomic_load_explicit(ptr, order) \
    ( (void)(order), atomic_compat_load(ptr) )

#define atomic_store_explicit(ptr, value, order) \
    do { (void)(order); atomic_compat_store((ptr), (value)); } while (0)

#define atomic_exchange_explicit(ptr, value, order) \
    ( (void)(order), atomic_compat_exchange((ptr), (value)) )

#define atomic_fetch_add_explicit(ptr, value, order) \
    ( (void)(order), atomic_compat_fetch_add((ptr), (value)) )

#define atomic_fetch_sub_explicit(ptr, value, order) \
    ( (void)(order), atomic_compat_fetch_sub((ptr), (value)) )

#define atomic_compare_exchange_strong_explicit(ptr, expected, desired, success_order, failure_order) \
    ( (void)(success_order), (void)(failure_order), atomic_compat_compare_exchange((ptr), (expected), (desired)) )

#define atomic_compare_exchange_weak_explicit(ptr, expected, desired, success_order, failure_order) \
    atomic_compare_exchange_strong_explicit(ptr, expected, desired, success_order, failure_order)

/* seq_cst defaults per C11 */
#define atomic_load(ptr)                              atomic_load_explicit((ptr), memory_order_seq_cst)
#define atomic_store(ptr, value)                      atomic_store_explicit((ptr), (value), memory_order_seq_cst)
#define atomic_exchange(ptr, value)                   atomic_exchange_explicit((ptr), (value), memory_order_seq_cst)
#define atomic_fetch_add(ptr, value)                  atomic_fetch_add_explicit((ptr), (value), memory_order_seq_cst)
#define atomic_fetch_sub(ptr, value)                  atomic_fetch_sub_explicit((ptr), (value), memory_order_seq_cst)
#define atomic_compare_exchange_strong(ptr,e,d)       atomic_compare_exchange_strong_explicit((ptr),(e),(d), memory_order_acq_rel, memory_order_acquire)
#define atomic_compare_exchange_weak(ptr,e,d)         atomic_compare_exchange_weak_explicit((ptr),(e),(d), memory_order_acq_rel, memory_order_acquire)

/* atomic_flag support:
   - If vcruntime already typedefs atomic_flag, we only provide the API macros.
   - If not, we provide a minimal struct + API. */
#ifndef ATOMIC_FLAG_INIT
#  define ATOMIC_FLAG_INIT { 0 }
#endif

/* Minimal atomic_flag replacement for the compatibility path. */
typedef struct {
    volatile LONG _v;
} atomic_flag;

static __forceinline int atomic_flag_test_and_set_explicit(atomic_flag *obj, memory_order order) {
    (void)order;
    /* Returns previous value (true if already set). */
    LONG prev = InterlockedExchange(&obj->_v, 1);
    return prev != 0;
}
static __forceinline void atomic_flag_clear_explicit(atomic_flag *obj, memory_order order) {
    (void)order;
    InterlockedExchange(&obj->_v, 0);
}
#define atomic_flag_test_and_set(obj) atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)
#define atomic_flag_clear(obj)        atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#endif /* ATOMIC_COMPAT_USE_NATIVE_ATOMICS */

/* ===================================================================== */
/*                         NON-MSVC (use real C11)                        */
/* ===================================================================== */
#else /* !_MSC_VER */

#  include <stdatomic.h>
#  define ATOMIC_COMPAT_HAVE_ATOMICS 1
#  define ATOMIC_VAR(type) _Atomic(type)

#endif /* _MSC_VER */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ATOMIC_COMPAT_H */
