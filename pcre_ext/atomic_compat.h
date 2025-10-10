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

#if defined(_MSC_VER) && !defined(ATOMIC_COMPAT_FORCE_STDATOMIC)
#  define ATOMIC_COMPAT_MSVC 1
#else
#  define ATOMIC_COMPAT_MSVC 0
#endif

#if !ATOMIC_COMPAT_MSVC

#include <stdatomic.h>

#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) _Atomic(type)

#else  /* ATOMIC_COMPAT_MSVC */

#include <windows.h>
#include <intrin.h>

typedef enum atomic_compat_memory_order {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) volatile type

#ifndef ATOMIC_VAR_INIT
#  define ATOMIC_VAR_INIT(value) (value)
#endif

#define atomic_thread_fence(order) MemoryBarrier()
#define atomic_signal_fence(order) (void)(order)

static inline LONG atomic_compat_long_load(volatile LONG *ptr)
{
    return InterlockedCompareExchange(ptr, 0, 0);
}

static inline void atomic_compat_long_store(volatile LONG *ptr, LONG value)
{
    InterlockedExchange(ptr, value);
}

static inline LONG atomic_compat_long_exchange(volatile LONG *ptr, LONG value)
{
    return InterlockedExchange(ptr, value);
}

static inline LONG atomic_compat_long_fetch_add(volatile LONG *ptr, LONG value)
{
    return InterlockedExchangeAdd(ptr, value);
}

static inline int atomic_compat_long_compare_exchange(
    volatile LONG *ptr,
    LONG *expected,
    LONG desired
)
{
    LONG original = InterlockedCompareExchange(ptr, desired, *expected);
    if (original == *expected) {
        return 1;
    }
    *expected = original;
    return 0;
}

#if defined(_WIN64)
static inline LONGLONG atomic_compat_longlong_load(volatile LONGLONG *ptr)
{
    return InterlockedCompareExchange64(ptr, 0, 0);
}

static inline void atomic_compat_longlong_store(volatile LONGLONG *ptr, LONGLONG value)
{
    InterlockedExchange64(ptr, value);
}

static inline LONGLONG atomic_compat_longlong_exchange(volatile LONGLONG *ptr, LONGLONG value)
{
    return InterlockedExchange64(ptr, value);
}
#else
static inline LONGLONG atomic_compat_longlong_load(volatile LONGLONG *ptr)
{
    LONGLONG value;
    do {
        value = *ptr;
    } while (InterlockedCompareExchange64(ptr, value, value) != value);
    return value;
}

static inline void atomic_compat_longlong_store(volatile LONGLONG *ptr, LONGLONG value)
{
    InterlockedExchange64(ptr, value);
}

static inline LONGLONG atomic_compat_longlong_exchange(volatile LONGLONG *ptr, LONGLONG value)
{
    return InterlockedExchange64(ptr, value);
}
#endif

static inline void *atomic_compat_pointer_load(void * volatile *ptr)
{
    return InterlockedCompareExchangePointer(ptr, NULL, NULL);
}

static inline void atomic_compat_pointer_store(void * volatile *ptr, void *value)
{
    InterlockedExchangePointer(ptr, value);
}

static inline void *atomic_compat_pointer_exchange(void * volatile *ptr, void *value)
{
    return InterlockedExchangePointer(ptr, value);
}

static inline int atomic_compat_pointer_compare_exchange(
    void * volatile *ptr,
    void **expected,
    void *desired
)
{
    void *original = InterlockedCompareExchangePointer(ptr, desired, *expected);
    if (original == *expected) {
        return 1;
    }
    *expected = original;
    return 0;
}

#if defined(_WIN64)
static inline size_t atomic_compat_size_load(volatile size_t *ptr)
{
    return (size_t)atomic_compat_longlong_load((volatile LONGLONG *)ptr);
}

static inline void atomic_compat_size_store(volatile size_t *ptr, size_t value)
{
    atomic_compat_longlong_store((volatile LONGLONG *)ptr, (LONGLONG)value);
}
#else
static inline size_t atomic_compat_size_load(volatile size_t *ptr)
{
    return (size_t)atomic_compat_long_load((volatile LONG *)ptr);
}

static inline void atomic_compat_size_store(volatile size_t *ptr, size_t value)
{
    atomic_compat_long_store((volatile LONG *)ptr, (LONG)value);
}
#endif

static inline void *atomic_compat_pointer_default_load(void * volatile *ptr)
{
    return atomic_compat_pointer_load(ptr);
}

static inline void atomic_compat_pointer_default_store(void * volatile *ptr, void *value)
{
    atomic_compat_pointer_store(ptr, value);
}

static inline void *atomic_compat_pointer_default_exchange(void * volatile *ptr, void *value)
{
    return atomic_compat_pointer_exchange(ptr, value);
}

static inline int atomic_compat_pointer_default_compare_exchange(
    void * volatile *ptr,
    void **expected,
    void *desired
)
{
    return atomic_compat_pointer_compare_exchange(ptr, expected, desired);
}

#define atomic_compat_load(ptr) \
    _Generic(*(ptr), \
        int: (int)atomic_compat_long_load((volatile LONG *)(ptr)), \
        volatile int: (int)atomic_compat_long_load((volatile LONG *)(ptr)), \
        unsigned int: (unsigned int)atomic_compat_long_load((volatile LONG *)(ptr)), \
        volatile unsigned int: (unsigned int)atomic_compat_long_load((volatile LONG *)(ptr)), \
        long: (long)atomic_compat_long_load((volatile LONG *)(ptr)), \
        volatile long: (long)atomic_compat_long_load((volatile LONG *)(ptr)), \
        unsigned long: (unsigned long)atomic_compat_long_load((volatile LONG *)(ptr)), \
        volatile unsigned long: (unsigned long)atomic_compat_long_load((volatile LONG *)(ptr)), \
        size_t: atomic_compat_size_load((volatile size_t *)(ptr)), \
        volatile size_t: atomic_compat_size_load((volatile size_t *)(ptr)), \
        default: atomic_compat_pointer_default_load((void * volatile *)(ptr)) \
    )

#define atomic_compat_store(ptr, value) \
    _Generic(*(ptr), \
        int: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        volatile int: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        unsigned int: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        volatile unsigned int: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        long: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        volatile long: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        unsigned long: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        volatile unsigned long: atomic_compat_long_store((volatile LONG *)(ptr), (LONG)(value)), \
        size_t: atomic_compat_size_store((volatile size_t *)(ptr), (size_t)(value)), \
        volatile size_t: atomic_compat_size_store((volatile size_t *)(ptr), (size_t)(value)), \
        default: atomic_compat_pointer_default_store((void * volatile *)(ptr), (void *)(value)) \
    )

#define atomic_compat_exchange(ptr, value) \
    _Generic(*(ptr), \
        int: (int)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        volatile int: (int)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        unsigned int: (unsigned int)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        volatile unsigned int: (unsigned int)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        long: (long)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        volatile long: (long)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        unsigned long: (unsigned long)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        volatile unsigned long: (unsigned long)atomic_compat_long_exchange((volatile LONG *)(ptr), (LONG)(value)), \
        default: atomic_compat_pointer_default_exchange((void * volatile *)(ptr), (void *)(value)) \
    )

#define atomic_compat_fetch_add(ptr, value) \
    _Generic(*(ptr), \
        int: (int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)), \
        volatile int: (int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)), \
        unsigned int: (unsigned int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)), \
        volatile unsigned int: (unsigned int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)), \
        long: (long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)), \
        volatile long: (long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)), \
        unsigned long: (unsigned long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)), \
        volatile unsigned long: (unsigned long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)(value)) \
    )

#define atomic_compat_fetch_sub(ptr, value) \
    _Generic(*(ptr), \
        int: (int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)-(value)), \
        volatile int: (int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), (LONG)-(value)), \
        unsigned int: (unsigned int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), -(LONG)(value)), \
        volatile unsigned int: (unsigned int)atomic_compat_long_fetch_add((volatile LONG *)(ptr), -(LONG)(value)), \
        long: (long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), -(LONG)(value)), \
        volatile long: (long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), -(LONG)(value)), \
        unsigned long: (unsigned long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), -(LONG)(value)), \
        volatile unsigned long: (unsigned long)atomic_compat_long_fetch_add((volatile LONG *)(ptr), -(LONG)(value)) \
    )

#define atomic_compat_compare_exchange(ptr, expected, desired) \
    _Generic(*(ptr), \
        int: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        volatile int: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        unsigned int: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        volatile unsigned int: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        long: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        volatile long: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        unsigned long: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        volatile unsigned long: atomic_compat_long_compare_exchange((volatile LONG *)(ptr), (LONG *)(expected), (LONG)(desired)), \
        default: atomic_compat_pointer_default_compare_exchange((void * volatile *)(ptr), (void **)(expected), (void *)(desired)) \
    )

#define atomic_load_explicit(ptr, order) ((void)(order), atomic_compat_load(ptr))
#define atomic_store_explicit(ptr, value, order) do { (void)(order); atomic_compat_store(ptr, value); } while (0)
#define atomic_exchange_explicit(ptr, value, order) ((void)(order), atomic_compat_exchange(ptr, value))
#define atomic_fetch_add_explicit(ptr, value, order) ((void)(order), atomic_compat_fetch_add(ptr, value))
#define atomic_fetch_sub_explicit(ptr, value, order) ((void)(order), atomic_compat_fetch_sub(ptr, value))
#define atomic_compare_exchange_strong_explicit(ptr, expected, desired, success_order, failure_order) \
    ((void)(success_order), (void)(failure_order), atomic_compat_compare_exchange(ptr, expected, desired))

#endif /* ATOMIC_COMPAT_MSVC */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ATOMIC_COMPAT_H */
