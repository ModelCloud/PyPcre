// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium
//
// atomic_compat.h — GNU C / C11 atomics compatibility for MSVC.
//
// This header provides a subset of C11 atomics for MSVC using Windows
// Interlocked APIs with at-least-as-strong memory ordering (seq_cst).
// On non-MSVC compilers it defers to <stdatomic.h>.

#ifndef ATOMIC_COMPAT_H
#define ATOMIC_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)

/* ============================= MSVC PATH ============================= */

#ifndef _WIN32_WINNT
// Interlocked*Acquire/Release are available broadly; base target is fine.
// Define minimally if needed; using full-fence Interlocked is conservative.
#define _WIN32_WINNT 0x0601 /* Windows 7 */
#endif

#include <windows.h>
#include <intrin.h>

/* ---- Feature flag and storage qualifier ---- */
#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) volatile type

#ifndef ATOMIC_VAR_INIT
#  define ATOMIC_VAR_INIT(value) (value)
#endif

/* ---- C11 memory order enum (MSVC doesn't ship <stdatomic.h>) ---- */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    memory_order_relaxed = 0,
    memory_order_consume = 1, /* treated as acquire */
    memory_order_acquire = 2,
    memory_order_release = 3,
    memory_order_acq_rel = 4,
    memory_order_seq_cst = 5
} memory_order;

/* ---- Fences ----
 * We conservatively implement thread fences as full fences and signal fences
 * as compiler barriers, which are at least as strong as required.
 */
#ifdef atomic_thread_fence
#  undef atomic_thread_fence
#endif
static __forceinline void atomic_thread_fence(memory_order order) {
    (void)order;
    /* Full fence (sequential consistency) */
    MemoryBarrier();
}

#ifdef atomic_signal_fence
#  undef atomic_signal_fence
#endif
static __forceinline void atomic_signal_fence(memory_order order) {
    (void)order;
    /* Compiler-only barrier */
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    /* Fallback no-op */
    (void)0;
#endif
}

/* ========== Low-level, typed C helpers (useful for C compilation) ========== */
/* 32-bit scalars */
static __forceinline int32_t  atomic_compat_load_i32 (volatile int32_t *p)  { return (int32_t)InterlockedCompareExchange((volatile LONG *)p, 0, 0); }
static __forceinline uint32_t atomic_compat_load_u32 (volatile uint32_t *p) { return (uint32_t)InterlockedCompareExchange((volatile LONG *)p, 0, 0); }
static __forceinline void     atomic_compat_store_i32(volatile int32_t *p,  int32_t  v)  { InterlockedExchange((volatile LONG *)p, (LONG)v); }
static __forceinline void     atomic_compat_store_u32(volatile uint32_t *p, uint32_t v)  { InterlockedExchange((volatile LONG *)p, (LONG)v); }
static __forceinline int32_t  atomic_compat_xchg_i32 (volatile int32_t *p,  int32_t  v)  { return (int32_t)InterlockedExchange((volatile LONG *)p, (LONG)v); }
static __forceinline uint32_t atomic_compat_xchg_u32 (volatile uint32_t *p, uint32_t v)  { return (uint32_t)InterlockedExchange((volatile LONG *)p, (LONG)v); }
static __forceinline int32_t  atomic_compat_fadd_i32(volatile int32_t *p,  int32_t  v)   { return (int32_t)InterlockedExchangeAdd((volatile LONG *)p, (LONG)v); }
static __forceinline uint32_t atomic_compat_fadd_u32(volatile uint32_t *p, uint32_t v)   { return (uint32_t)InterlockedExchangeAdd((volatile LONG *)p, (LONG)v); }
static __forceinline int      atomic_compat_cas_i32 (volatile int32_t *p,  int32_t *e, int32_t d) {
    LONG orig = InterlockedCompareExchange((volatile LONG *)p, (LONG)d, (LONG)(*e));
    if ((int32_t)orig == *e) return 1;
    *e = (int32_t)orig; return 0;
}
static __forceinline int      atomic_compat_cas_u32 (volatile uint32_t *p, uint32_t *e, uint32_t d) {
    LONG orig = InterlockedCompareExchange((volatile LONG *)p, (LONG)d, (LONG)(*e));
    if ((uint32_t)orig == *e) return 1;
    *e = (uint32_t)orig; return 0;
}

/* 64-bit scalars */
static __forceinline int64_t  atomic_compat_load_i64 (volatile int64_t *p)  { return (int64_t)InterlockedCompareExchange64((volatile LONGLONG *)p, 0, 0); }
static __forceinline uint64_t atomic_compat_load_u64 (volatile uint64_t *p) { return (uint64_t)InterlockedCompareExchange64((volatile LONGLONG *)p, 0, 0); }
static __forceinline void     atomic_compat_store_i64(volatile int64_t *p,  int64_t  v)  { InterlockedExchange64((volatile LONGLONG *)p, (LONGLONG)v); }
static __forceinline void     atomic_compat_store_u64(volatile uint64_t *p, uint64_t v)  { InterlockedExchange64((volatile LONGLONG *)p, (LONGLONG)v); }
static __forceinline int64_t  atomic_compat_xchg_i64 (volatile int64_t *p,  int64_t  v)  { return (int64_t)InterlockedExchange64((volatile LONGLONG *)p, (LONGLONG)v); }
static __forceinline uint64_t atomic_compat_xchg_u64 (volatile uint64_t *p, uint64_t v)  { return (uint64_t)InterlockedExchange64((volatile LONGLONG *)p, (LONGLONG)v); }
static __forceinline int64_t  atomic_compat_fadd_i64(volatile int64_t *p,  int64_t  v)   { return (int64_t)InterlockedExchangeAdd64((volatile LONGLONG *)p, (LONGLONG)v); }
static __forceinline uint64_t atomic_compat_fadd_u64(volatile uint64_t *p, uint64_t v)   { return (uint64_t)InterlockedExchangeAdd64((volatile LONGLONG *)p, (LONGLONG)v); }
static __forceinline int      atomic_compat_cas_i64 (volatile int64_t *p,  int64_t *e, int64_t d) {
    LONGLONG orig = InterlockedCompareExchange64((volatile LONGLONG *)p, (LONGLONG)d, (LONGLONG)(*e));
    if ((int64_t)orig == *e) return 1;
    *e = (int64_t)orig; return 0;
}
static __forceinline int      atomic_compat_cas_u64 (volatile uint64_t *p, uint64_t *e, uint64_t d) {
    LONGLONG orig = InterlockedCompareExchange64((volatile LONGLONG *)p, (LONGLONG)d, (LONGLONG)(*e));
    if ((uint64_t)orig == *e) return 1;
    *e = (uint64_t)orig; return 0;
}

/* size_t (width-dependent) */
static __forceinline size_t atomic_compat_load_size (volatile size_t *p) {
#if defined(_WIN64)
    return (size_t)InterlockedCompareExchange64((volatile LONGLONG *)p, 0, 0);
#else
    return (size_t)InterlockedCompareExchange((volatile LONG *)p, 0, 0);
#endif
}
static __forceinline void   atomic_compat_store_size(volatile size_t *p, size_t v) {
#if defined(_WIN64)
    InterlockedExchange64((volatile LONGLONG *)p, (LONGLONG)v);
#else
    InterlockedExchange((volatile LONG *)p, (LONG)v);
#endif
}

/* pointers */
static __forceinline void * atomic_compat_load_ptr (void * volatile *p)                 { return InterlockedCompareExchangePointer(p, NULL, NULL); }
static __forceinline void   atomic_compat_store_ptr(void * volatile *p, void *v)        { InterlockedExchangePointer(p, v); }
static __forceinline void * atomic_compat_xchg_ptr (void * volatile *p, void *v)        { return InterlockedExchangePointer(p, v); }
static __forceinline int    atomic_compat_cas_ptr  (void * volatile *p, void **e, void *d) {
    void *orig = InterlockedCompareExchangePointer(p, d, *e);
    if (orig == *e) return 1;
    *e = orig; return 0;
}

/* atomic_flag */
typedef struct { volatile LONG _v; } atomic_flag;
#define ATOMIC_FLAG_INIT { 0 }

static __forceinline int atomic_flag_test_and_set_explicit(atomic_flag *obj, memory_order order) {
    (void)order;
    LONG prev = InterlockedExchange(&obj->_v, 1);
    return prev != 0; /* returns previous value */
}
static __forceinline void atomic_flag_clear_explicit(atomic_flag *obj, memory_order order) {
    (void)order;
    InterlockedExchange(&obj->_v, 0);
}

/* ---- atomic_init (C11) ---- */
#define atomic_init(obj, value) do { atomic_store_explicit((obj), (value), memory_order_relaxed); } while (0)

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ======================= C++ GENERIC FRONT-END ======================= */
/* For MSVC, compile this header as C++ to get type-generic APIs. */
#if defined(__cplusplus)

/* We use overloads + SFINAE to emulate C11 generic functions. */
#include <type_traits>

/* ======== atomic_load_explicit ======== */
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==4, int>::type = 0>
static __forceinline T atomic_load_explicit(volatile T *p, memory_order order) {
    (void)order; return (T)atomic_compat_load_u32((volatile uint32_t*)p);
}
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==8, int>::type = 0>
static __forceinline T atomic_load_explicit(volatile T *p, memory_order order) {
    (void)order; return (T)atomic_compat_load_u64((volatile uint64_t*)p);
}
template <class P, typename std::enable_if<std::is_pointer<P>::value, int>::type = 0>
static __forceinline P atomic_load_explicit(P volatile *p, memory_order order) {
    (void)order; return (P)atomic_compat_load_ptr((void * volatile *)p);
}

/* ======== atomic_store_explicit ======== */
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==4, int>::type = 0>
static __forceinline void atomic_store_explicit(volatile T *p, T v, memory_order order) {
    (void)order; atomic_compat_store_u32((volatile uint32_t*)p, (uint32_t)v);
}
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==8, int>::type = 0>
static __forceinline void atomic_store_explicit(volatile T *p, T v, memory_order order) {
    (void)order; atomic_compat_store_u64((volatile uint64_t*)p, (uint64_t)v);
}
template <class P, typename std::enable_if<std::is_pointer<P>::value, int>::type = 0>
static __forceinline void atomic_store_explicit(P volatile *p, P v, memory_order order) {
    (void)order; atomic_compat_store_ptr((void * volatile *)p, (void*)v);
}

/* ======== atomic_exchange_explicit ======== */
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==4, int>::type = 0>
static __forceinline T atomic_exchange_explicit(volatile T *p, T v, memory_order order) {
    (void)order; return (T)atomic_compat_xchg_u32((volatile uint32_t*)p, (uint32_t)v);
}
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==8, int>::type = 0>
static __forceinline T atomic_exchange_explicit(volatile T *p, T v, memory_order order) {
    (void)order; return (T)atomic_compat_xchg_u64((volatile uint64_t*)p, (uint64_t)v);
}
template <class P, typename std::enable_if<std::is_pointer<P>::value, int>::type = 0>
static __forceinline P atomic_exchange_explicit(P volatile *p, P v, memory_order order) {
    (void)order; return (P)atomic_compat_xchg_ptr((void * volatile *)p, (void*)v);
}

/* ======== atomic_fetch_add_explicit / fetch_sub ======== */
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==4, int>::type = 0>
static __forceinline T atomic_fetch_add_explicit(volatile T *p, T v, memory_order order) {
    (void)order; return (T)atomic_compat_fadd_u32((volatile uint32_t*)p, (uint32_t)v);
}
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==8, int>::type = 0>
static __forceinline T atomic_fetch_add_explicit(volatile T *p, T v, memory_order order) {
    (void)order; return (T)atomic_compat_fadd_u64((volatile uint64_t*)p, (uint64_t)v);
}
template <class T, typename std::enable_if<std::is_integral<T>::value && (sizeof(T)==4 || sizeof(T)==8), int>::type = 0>
static __forceinline T atomic_fetch_sub_explicit(volatile T *p, T v, memory_order order) {
    (void)order; return atomic_fetch_add_explicit(p, (T)-(v), memory_order_acq_rel);
}

/* ======== atomic_compare_exchange_*_explicit ========
 * C11 requires that *expected is updated with the current value on failure.
 * We implement both strong and weak; weak == strong on this platform.
 */
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==4, int>::type = 0>
static __forceinline int atomic_compare_exchange_strong_explicit(volatile T *p, T *expected, T desired,
                                                                 memory_order success, memory_order failure) {
    (void)success; (void)failure;
    return atomic_compat_cas_u32((volatile uint32_t*)p, (uint32_t*)expected, (uint32_t)desired);
}
template <class T, typename std::enable_if<std::is_integral<T>::value && sizeof(T)==8, int>::type = 0>
static __forceinline int atomic_compare_exchange_strong_explicit(volatile T *p, T *expected, T desired,
                                                                 memory_order success, memory_order failure) {
    (void)success; (void)failure;
    return atomic_compat_cas_u64((volatile uint64_t*)p, (uint64_t*)expected, (uint64_t)desired);
}
template <class P, typename std::enable_if<std::is_pointer<P>::value, int>::type = 0>
static __forceinline int atomic_compare_exchange_strong_explicit(P volatile *p, P *expected, P desired,
                                                                 memory_order success, memory_order failure) {
    (void)success; (void)failure;
    return atomic_compat_cas_ptr((void * volatile *)p, (void**)expected, (void*)desired);
}

/* weak == strong on this implementation */
template <class T>
static __forceinline int atomic_compare_exchange_weak_explicit(volatile T *p, T *expected, T desired,
                                                               memory_order success, memory_order failure) {
    return atomic_compare_exchange_strong_explicit(p, expected, desired, success, failure);
}

/* ======== Convenience non-explicit forms (seq_cst by default) ======== */
template <class T> static __forceinline T    atomic_load (volatile T *p)                { return atomic_load_explicit(p, memory_order_seq_cst); }
template <class T> static __forceinline void atomic_store(volatile T *p, T v)           { atomic_store_explicit(p, v, memory_order_seq_cst); }
template <class T> static __forceinline T    atomic_exchange(volatile T *p, T v)        { return atomic_exchange_explicit(p, v, memory_order_seq_cst); }
template <class T> static __forceinline T    atomic_fetch_add(volatile T *p, T v)       { return atomic_fetch_add_explicit(p, v, memory_order_seq_cst); }
template <class T> static __forceinline T    atomic_fetch_sub(volatile T *p, T v)       { return atomic_fetch_sub_explicit(p, v, memory_order_seq_cst); }
template <class T> static __forceinline int  atomic_compare_exchange_strong(volatile T *p, T *e, T d) {
    return atomic_compare_exchange_strong_explicit(p, e, d, memory_order_acq_rel, memory_order_acquire);
}
template <class T> static __forceinline int  atomic_compare_exchange_weak(volatile T *p, T *e, T d) {
    return atomic_compare_exchange_weak_explicit(p, e, d, memory_order_acq_rel, memory_order_acquire);
}

#endif /* __cplusplus */

/* ==================== GNU __atomic builtins (MSVC shim) ==================== */
/* Provide GCC-style __atomic_* for MSVC (not clang-cl, which already has them). */
#if defined(_MSC_VER) && !defined(__clang__)

/* GCC order constants */
#ifndef __ATOMIC_RELAXED
#  define __ATOMIC_RELAXED 0
#  define __ATOMIC_CONSUME 1
#  define __ATOMIC_ACQUIRE 2
#  define __ATOMIC_RELEASE 3
#  define __ATOMIC_ACQ_REL 4
#  define __ATOMIC_SEQ_CST 5
#endif

/* Map integer order to C11 memory_order */
static __forceinline memory_order __msvc_atomic_to_mo(int ord) {
    switch (ord) {
        case __ATOMIC_RELAXED: return memory_order_relaxed;
        case __ATOMIC_CONSUME: return memory_order_consume;
        case __ATOMIC_ACQUIRE: return memory_order_acquire;
        case __ATOMIC_RELEASE: return memory_order_release;
        case __ATOMIC_ACQ_REL: return memory_order_acq_rel;
        default:               return memory_order_seq_cst;
    }
}

/* Builtin shims (use C++ overloads if available; otherwise typed helpers) */
#ifdef __cplusplus

template<class T>
static __forceinline T __atomic_load_n(const volatile T *p, int ord) {
    return atomic_load_explicit(const_cast<volatile T*>(p), __msvc_atomic_to_mo(ord));
}
template<class T>
static __forceinline void __atomic_store_n(volatile T *p, T v, int ord) {
    atomic_store_explicit(p, v, __msvc_atomic_to_mo(ord));
}
template<class T>
static __forceinline T __atomic_exchange_n(volatile T *p, T v, int ord) {
    return atomic_exchange_explicit(p, v, __msvc_atomic_to_mo(ord));
}
template<class T>
static __forceinline int __atomic_compare_exchange_n(volatile T *p, T *expected, T desired,
                                                     int weak, int success, int failure) {
    (void)weak;
    return atomic_compare_exchange_strong_explicit(p, expected, desired,
                                                   __msvc_atomic_to_mo(success),
                                                   __msvc_atomic_to_mo(failure));
}
template<class T>
static __forceinline T __atomic_fetch_add(volatile T *p, T v, int ord) {
    return atomic_fetch_add_explicit(p, v, __msvc_atomic_to_mo(ord));
}
template<class T>
static __forceinline T __atomic_fetch_sub(volatile T *p, T v, int ord) {
    return atomic_fetch_sub_explicit(p, v, __msvc_atomic_to_mo(ord));
}
static __forceinline void __atomic_thread_fence(int ord) {
    atomic_thread_fence(__msvc_atomic_to_mo(ord));
}

#else /* !__cplusplus — fall back to minimal C helpers for common widths */

#define __atomic_load_n(p, ord) \
    ( (sizeof(*(p))==8) ? (size_t)atomic_compat_load_u64((volatile uint64_t*)(p)) : \
      (sizeof(*(p))==4) ? (size_t)atomic_compat_load_u32((volatile uint32_t*)(p)) : \
      (size_t)atomic_compat_load_ptr((void * volatile *)(p)) )

#define __atomic_store_n(p, v, ord) do {                                       \
    if      (sizeof(*(p))==8) atomic_compat_store_u64((volatile uint64_t*)(p), (uint64_t)(v)); \
    else if (sizeof(*(p))==4) atomic_compat_store_u32((volatile uint32_t*)(p), (uint32_t)(v)); \
    else                      atomic_compat_store_ptr((void * volatile *)(p), (void*)(uintptr_t)(v)); \
} while(0)

#define __atomic_exchange_n(p, v, ord) \
    ( (sizeof(*(p))==8) ? (uint64_t)atomic_compat_xchg_u64((volatile uint64_t*)(p), (uint64_t)(v)) : \
      (sizeof(*(p))==4) ? (uint32_t)atomic_compat_xchg_u32((volatile uint32_t*)(p), (uint32_t)(v)) : \
                          (uintptr_t)atomic_compat_xchg_ptr((void * volatile *)(p), (void*)(uintptr_t)(v)) )

/* Compare-exchange C fallback supports 32/64-bit and pointers */
static __forceinline int __atomic_compare_exchange_n_impl(void *pp, void *expected, void *desired,
                                                          size_t sz) {
    if (sz == 8) {
        uint64_t *p = (uint64_t*)pp; uint64_t *e = (uint64_t*)expected; uint64_t d = *(uint64_t*)desired;
        return atomic_compat_cas_u64((volatile uint64_t*)p, e, d);
    } else if (sz == 4) {
        uint32_t *p = (uint32_t*)pp; uint32_t *e = (uint32_t*)expected; uint32_t d = *(uint32_t*)desired;
        return atomic_compat_cas_u32((volatile uint32_t*)p, e, d);
    } else {
        void **p = (void**)pp; void **e = (void**)expected; void *d = *(void**)desired;
        return atomic_compat_cas_ptr((void * volatile *)p, e, d);
    }
}
#define __atomic_compare_exchange_n(p, expected, desired, weak, succ, fail) \
    __atomic_compare_exchange_n_impl((void*)(p), (void*)(expected), (void*)&(desired), sizeof(*(p)))

#define __atomic_fetch_add(p, v, ord) \
    ( (sizeof(*(p))==8) ? (uint64_t)atomic_compat_fadd_u64((volatile uint64_t*)(p), (uint64_t)(v)) : \
                          (uint32_t)atomic_compat_fadd_u32((volatile uint32_t*)(p), (uint32_t)(v)) )

#define __atomic_fetch_sub(p, v, ord) __atomic_fetch_add((p), (-(v)), (ord))

static __forceinline void __atomic_thread_fence(int ord) {
    (void)ord; atomic_thread_fence(memory_order_seq_cst);
}
#endif /* __cplusplus */
#endif /* _MSC_VER && !__clang__ */

/* ============================= NON-MSVC PATH ============================= */
#else /* !_MSC_VER */

#  include <stdatomic.h>
#  define ATOMIC_COMPAT_HAVE_ATOMICS 1
#  define ATOMIC_VAR(type) _Atomic(type)

#endif /* _MSC_VER */

/* ============================= COMMON SHORTCUTS ============================= */
/* Provide non-explicit macros to match C11 defaults (seq_cst). On MSVC C++,
 * these resolve to the overloads above; on non-MSVC they come from stdatomic.h.
 */
#ifndef _MSC_VER
/* On non-MSVC, stdatomic already defines these. */
#else
#  ifdef __cplusplus
    /* C++ overloads already defined for MSVC above. Nothing to do. */
#  else
    /* Minimal C convenience macros for common widths when compiling as C under MSVC. */
#    define atomic_load(ptr)       ((sizeof(*(ptr))==8) ? (size_t)atomic_compat_load_u64((volatile uint64_t*)(ptr)) : \
                                   (sizeof(*(ptr))==4) ? (size_t)atomic_compat_load_u32((volatile uint32_t*)(ptr)) : \
                                                         (size_t)atomic_compat_load_ptr((void * volatile *)(ptr)))
#    define atomic_store(ptr, v)   do { if (sizeof(*(ptr))==8) atomic_compat_store_u64((volatile uint64_t*)(ptr),(uint64_t)(v)); \
                                        else if (sizeof(*(ptr))==4) atomic_compat_store_u32((volatile uint32_t*)(ptr),(uint32_t)(v)); \
                                        else atomic_compat_store_ptr((void * volatile *)(ptr),(void*)(uintptr_t)(v)); } while(0)
#    define atomic_exchange(ptr,v) ((sizeof(*(ptr))==8) ? (uint64_t)atomic_compat_xchg_u64((volatile uint64_t*)(ptr),(uint64_t)(v)) : \
                                   (sizeof(*(ptr))==4) ? (uint32_t)atomic_compat_xchg_u32((volatile uint32_t*)(ptr),(uint32_t)(v)) : \
                                                         (uintptr_t)atomic_compat_xchg_ptr((void * volatile *)(ptr),(void*)(uintptr_t)(v)))
#    define atomic_fetch_add(ptr,v) ((sizeof(*(ptr))==8) ? (uint64_t)atomic_compat_fadd_u64((volatile uint64_t*)(ptr),(uint64_t)(v)) : \
                                     (uint32_t)atomic_compat_fadd_u32((volatile uint32_t*)(ptr),(uint32_t)(v)))
#    define atomic_fetch_sub(ptr,v) (atomic_fetch_add((ptr), (-(v))))
#  endif
#endif /* _MSC_VER */

/* ======== atomic_flag non-explicit convenience (seq_cst) ======== */
#if defined(_MSC_VER)
#  define atomic_flag_test_and_set(obj) atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)
#  define atomic_flag_clear(obj)        atomic_flag_clear_explicit((obj), memory_order_seq_cst)
#endif

#endif /* ATOMIC_COMPAT_H */
