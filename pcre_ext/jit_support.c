// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

static PyThread_type_lock jit_serial_lock = NULL;

#if defined(PCRE_EXT_HAVE_ATOMICS)
static ATOMIC_VAR(int) default_jit_enabled = ATOMIC_VAR_INIT(1);
#else
static int default_jit_enabled = 1;
static PyThread_type_lock default_jit_lock = NULL;
#endif

int
jit_support_initialize(int force_serial_lock)
{
#if !defined(PCRE_EXT_HAVE_ATOMICS)
    if (default_jit_lock == NULL) {
        default_jit_lock = PyThread_allocate_lock();
        if (default_jit_lock == NULL) {
            PyErr_NoMemory();
            return -1;
        }
    }
#endif
    if (force_serial_lock) {
        if (jit_serial_lock == NULL) {
            jit_serial_lock = PyThread_allocate_lock();
            if (jit_serial_lock == NULL) {
                PyErr_NoMemory();
                return -1;
            }
        }
    }
    return 0;
}

void
jit_support_teardown(void)
{
#if !defined(PCRE_EXT_HAVE_ATOMICS)
    if (default_jit_lock != NULL) {
        PyThread_free_lock(default_jit_lock);
        default_jit_lock = NULL;
    }
#endif
    if (jit_serial_lock != NULL) {
        PyThread_free_lock(jit_serial_lock);
        jit_serial_lock = NULL;
    }
}

void
jit_guard_acquire(void)
{
    if (jit_serial_lock != NULL) {
        PyThread_acquire_lock(jit_serial_lock, 1);
    }
}

void
jit_guard_release(void)
{
    if (jit_serial_lock != NULL) {
        PyThread_release_lock(jit_serial_lock);
    }
}

int
default_jit_get(void)
{
#if defined(PCRE_EXT_HAVE_ATOMICS)
    if (jit_serial_lock != NULL) {
        jit_guard_acquire();
        int value = atomic_load_explicit(&default_jit_enabled, memory_order_acquire);
        jit_guard_release();
        return value;
    }
    return atomic_load_explicit(&default_jit_enabled, memory_order_acquire);
#else
    PyThread_acquire_lock(default_jit_lock, 1);
    int value = default_jit_enabled;
    PyThread_release_lock(default_jit_lock);
    return value;
#endif
}

void
default_jit_set(int value)
{
#if defined(PCRE_EXT_HAVE_ATOMICS)
    if (jit_serial_lock != NULL) {
        jit_guard_acquire();
        atomic_store_explicit(&default_jit_enabled, value, memory_order_release);
        jit_guard_release();
        return;
    }
    atomic_store_explicit(&default_jit_enabled, value, memory_order_release);
#else
    PyThread_acquire_lock(default_jit_lock, 1);
    default_jit_enabled = value;
    PyThread_release_lock(default_jit_lock);
#endif
}

int
pattern_jit_get(PatternObject *pattern)
{
#if defined(PCRE_EXT_HAVE_ATOMICS)
    if (jit_serial_lock != NULL) {
        jit_guard_acquire();
        int value = atomic_load_explicit(&pattern->jit_enabled, memory_order_acquire);
        jit_guard_release();
        return value;
    }
    return atomic_load_explicit(&pattern->jit_enabled, memory_order_acquire);
#else
    if (pattern->jit_lock != NULL) {
        PyThread_acquire_lock(pattern->jit_lock, 1);
        int value = pattern->jit_enabled;
        PyThread_release_lock(pattern->jit_lock);
        return value;
    }
    return pattern->jit_enabled;
#endif
}

void
pattern_jit_set(PatternObject *pattern, int value)
{
#if defined(PCRE_EXT_HAVE_ATOMICS)
    if (jit_serial_lock != NULL) {
        jit_guard_acquire();
        atomic_store_explicit(&pattern->jit_enabled, value, memory_order_release);
        jit_guard_release();
        return;
    }
    atomic_store_explicit(&pattern->jit_enabled, value, memory_order_release);
#else
    if (pattern->jit_lock != NULL) {
        PyThread_acquire_lock(pattern->jit_lock, 1);
        pattern->jit_enabled = value;
        PyThread_release_lock(pattern->jit_lock);
        return;
    }
    pattern->jit_enabled = value;
#endif
}
