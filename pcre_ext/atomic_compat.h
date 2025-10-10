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

#if defined(_MSC_VER)

#include <windows.h>
#include <intrin.h>

#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) volatile type

#ifndef ATOMIC_VAR_INIT
#  define ATOMIC_VAR_INIT(value) (value)
#endif

#ifdef atomic_thread_fence
#undef atomic_thread_fence
#endif
#define atomic_thread_fence(order) MemoryBarrier()

#ifdef atomic_signal_fence
#undef atomic_signal_fence
#endif
#define atomic_signal_fence(order) (void)(order)

/* ---- FIX: use intptr_t for pointer-width atomic types ---- */
typedef intptr_t  atomic_compat_long_type;
typedef int64_t   atomic_compat_longlong_type;
/* ---------------------------------------------------------- */

static inline atomic_compat_long_type atomic_compat_long_load(volatile atomic_compat_long_type *ptr)
{
#if defined(_WIN64)
    return InterlockedCompareExchange64((volatile LONGLONG *)ptr, 0, 0);
#else
    return InterlockedCompareExchange((volatile LONG *)ptr, 0, 0);
#endif
}

static inline void atomic_compat_long_store(volatile atomic_compat_long_type *ptr, atomic_compat_long_type value)
{
#if defined(_WIN64)
    InterlockedExchange64((volatile LONGLONG *)ptr, value);
#else
    InterlockedExchange((volatile LONG *)ptr, (LONG)value);
#endif
}

static inline atomic_compat_long_type atomic_compat_long_exchange(volatile atomic_compat_long_type *ptr,
                                                                  atomic_compat_long_type value)
{
#if defined(_WIN64)
    return InterlockedExchange64((volatile LONGLONG *)ptr, value);
#else
    return InterlockedExchange((volatile LONG *)ptr, (LONG)value);
#endif
}

static inline atomic_compat_long_type atomic_compat_long_fetch_add(volatile atomic_compat_long_type *ptr,
                                                                   atomic_compat_long_type value)
{
#if defined(_WIN64)
    return InterlockedExchangeAdd64((volatile LONGLONG *)ptr, value);
#else
    return InterlockedExchangeAdd((volatile LONG *)ptr, (LONG)value);
#endif
}

static inline int atomic_compat_long_compare_exchange(volatile atomic_compat_long_type *ptr,
                                                      atomic_compat_long_type *expected,
                                                      atomic_compat_long_type desired)
{
#if defined(_WIN64)
    atomic_compat_long_type original = InterlockedCompareExchange64((volatile LONGLONG *)ptr, desired, *expected);
#else
    atomic_compat_long_type original = InterlockedCompareExchange((volatile LONG *)ptr, (LONG)desired, (LONG)*expected);
#endif
    if (original == *expected) {
        return 1;
    }
    *expected = original;
    return 0;
}

static inline size_t atomic_compat_size_load(volatile size_t *ptr)
{
#if defined(_WIN64)
    return (size_t)InterlockedCompareExchange64((volatile LONGLONG *)ptr, 0, 0);
#else
    return (size_t)InterlockedCompareExchange((volatile LONG *)ptr, 0, 0);
#endif
}

static inline void atomic_compat_size_store(volatile size_t *ptr, size_t value)
{
#if defined(_WIN64)
    InterlockedExchange64((volatile LONGLONG *)ptr, (LONGLONG)value);
#else
    InterlockedExchange((volatile LONG *)ptr, (LONG)value);
#endif
}

static inline void *atomic_compat_pointer_load_impl(void * volatile *ptr)
{
    return InterlockedCompareExchangePointer(ptr, NULL, NULL);
}

static inline void atomic_compat_pointer_store_impl(void * volatile *ptr, void *value)
{
    InterlockedExchangePointer(ptr, value);
}

static inline void *atomic_compat_pointer_exchange_impl(void * volatile *ptr, void *value)
{
    return InterlockedExchangePointer(ptr, value);
}

static inline int atomic_compat_pointer_compare_exchange_impl(void * volatile *ptr,
                                                              void **expected,
                                                              void *desired)
{
    void *original = InterlockedCompareExchangePointer(ptr, desired, *expected);
    if (original == *expected) {
        return 1;
    }
    *expected = original;
    return 0;
}

/* ---- Generic macros stay the same, now pointer-width safe ---- */

#define atomic_compat_load(ptr) \
    _Generic((ptr), \
        volatile int *: (int)atomic_compat_long_load((volatile atomic_compat_long_type *)(ptr)), \
        int *: (int)atomic_compat_long_load((volatile atomic_compat_long_type *)(ptr)), \
        volatile uint32_t *: (uint32_t)atomic_compat_long_load((volatile atomic_compat_long_type *)(ptr)), \
        uint32_t *: (uint32_t)atomic_compat_long_load((volatile atomic_compat_long_type *)(ptr)), \
        volatile size_t *: atomic_compat_size_load((volatile size_t *)(ptr)), \
        size_t *: atomic_compat_size_load((volatile size_t *)(ptr)), \
        default: atomic_compat_pointer_load_impl((void * volatile *)(ptr)) \
    )

#define atomic_compat_store(ptr, value) \
    _Generic((ptr), \
        volatile int *: atomic_compat_long_store((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        int *: atomic_compat_long_store((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        volatile uint32_t *: atomic_compat_long_store((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        uint32_t *: atomic_compat_long_store((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        volatile size_t *: atomic_compat_size_store((volatile size_t *)(ptr), (size_t)(value)), \
        size_t *: atomic_compat_size_store((volatile size_t *)(ptr), (size_t)(value)), \
        default: atomic_compat_pointer_store_impl((void * volatile *)(ptr), (void *)(value)) \
    )

#define atomic_compat_exchange(ptr, value) \
    _Generic((ptr), \
        volatile int *: (int)atomic_compat_long_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        int *: (int)atomic_compat_long_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        volatile uint32_t *: (uint32_t)atomic_compat_long_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        uint32_t *: (uint32_t)atomic_compat_long_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        default: atomic_compat_pointer_exchange_impl((void * volatile *)(ptr), (void *)(value)) \
    )

#define atomic_compat_fetch_add(ptr, value) \
    _Generic((ptr), \
        volatile int *: (int)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        int *: (int)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        volatile uint32_t *: (uint32_t)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)), \
        uint32_t *: (uint32_t)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)(value)) \
    )

#define atomic_compat_fetch_sub(ptr, value) \
    _Generic((ptr), \
        volatile int *: (int)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)-(value)), \
        int *: (int)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)-(value)), \
        volatile uint32_t *: (uint32_t)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)-(value)), \
        uint32_t *: (uint32_t)atomic_compat_long_fetch_add((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type)-(value)) \
    )

#define atomic_compat_compare_exchange(ptr, expected, desired) \
    _Generic((ptr), \
        volatile int *: atomic_compat_long_compare_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type *)(expected), (atomic_compat_long_type)(desired)), \
        int *: atomic_compat_long_compare_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type *)(expected), (atomic_compat_long_type)(desired)), \
        volatile uint32_t *: atomic_compat_long_compare_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type *)(expected), (atomic_compat_long_type)(desired)), \
        uint32_t *: atomic_compat_long_compare_exchange((volatile atomic_compat_long_type *)(ptr), (atomic_compat_long_type *)(expected), (atomic_compat_long_type)(desired)), \
        default: atomic_compat_pointer_compare_exchange_impl((void * volatile *)(ptr), (void **)(expected), (void *)(desired)) \
    )

#ifdef atomic_load_explicit
#undef atomic_load_explicit
#endif
#define atomic_load_explicit(ptr, order) atomic_compat_load(ptr)

#ifdef atomic_store_explicit
#undef atomic_store_explicit
#endif
#define atomic_store_explicit(ptr, value, order) atomic_compat_store(ptr, value)

#ifdef atomic_exchange_explicit
#undef atomic_exchange_explicit
#endif
#define atomic_exchange_explicit(ptr, value, order) atomic_compat_exchange(ptr, value)

#ifdef atomic_fetch_add_explicit
#undef atomic_fetch_add_explicit
#endif
#define atomic_fetch_add_explicit(ptr, value, order) atomic_compat_fetch_add(ptr, value)

#ifdef atomic_fetch_sub_explicit
#undef atomic_fetch_sub_explicit
#endif
#define atomic_fetch_sub_explicit(ptr, value, order) atomic_compat_fetch_sub(ptr, value)

#ifdef atomic_compare_exchange_strong_explicit
#undef atomic_compare_exchange_strong_explicit
#endif
#define atomic_compare_exchange_strong_explicit(ptr, expected, desired, success_order, failure_order) \
    atomic_compat_compare_exchange(ptr, expected, desired)

#else /* !_MSC_VER */

#include <stdatomic.h>
#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) _Atomic(type)

#endif /* _MSC_VER */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ATOMIC_COMPAT_H */
