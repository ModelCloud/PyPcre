// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

#define MODULE_COMPILE_CACHE_LIMIT 128

typedef struct {
    PyObject *map;
    PyObject *order;
    Py_ssize_t limit;
} PatternCacheState;

static Py_tss_t pattern_cache_tss = Py_tss_NEEDS_INIT;
static ATOMIC_VAR(int) pattern_cache_tss_ready = ATOMIC_VAR_INIT(0);
static PatternCacheState global_pattern_cache = {NULL, NULL, MODULE_COMPILE_CACHE_LIMIT};
static PyThread_type_lock global_pattern_cache_lock = NULL;
static ATOMIC_VAR(int) pattern_cache_global_mode = ATOMIC_VAR_INIT(0);

static inline int
pattern_cache_is_global(void)
{
    return atomic_load_explicit(&pattern_cache_global_mode, memory_order_acquire);
}

static int
pattern_cache_tss_initialize(void)
{
    if (atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire)) {
        return 0;
    }
    if (PyThread_tss_create(&pattern_cache_tss) != 0) {
        PyErr_NoMemory();
        return -1;
    }
    atomic_store_explicit(&pattern_cache_tss_ready, 1, memory_order_release);
    return 0;
}

static PatternCacheState *
thread_pattern_cache_state_get(void)
{
    if (!atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire)) {
        return NULL;
    }
    return (PatternCacheState *)PyThread_tss_get(&pattern_cache_tss);
}

static PatternCacheState *
thread_pattern_cache_state_get_or_create(void)
{
    PatternCacheState *state = thread_pattern_cache_state_get();
    if (state != NULL) {
        return state;
    }
    if (!atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire)) {
        PyErr_SetString(PyExc_RuntimeError, "pattern cache subsystem not initialized");
        return NULL;
    }
    state = (PatternCacheState *)PyMem_Calloc(1, sizeof(*state));
    if (state == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    state->limit = MODULE_COMPILE_CACHE_LIMIT;
    if (PyThread_tss_set(&pattern_cache_tss, state) != 0) {
        PyMem_Free(state);
        PyErr_SetString(PyExc_RuntimeError, "failed to store pattern cache state");
        return NULL;
    }
    return state;
}

static int
pattern_cache_state_ensure(PatternCacheState *state)
{
    if (state->map == NULL) {
        PyObject *map = PyDict_New();
        if (map == NULL) {
            return -1;
        }
        PyObject *order = PyList_New(0);
        if (order == NULL) {
            Py_DECREF(map);
            return -1;
        }
        state->map = map;
        state->order = order;
    } else if (state->order == NULL) {
        state->order = PyList_New(0);
        if (state->order == NULL) {
            return -1;
        }
    }
    if (state->limit <= 0) {
        state->limit = MODULE_COMPILE_CACHE_LIMIT;
    }
    return 0;
}

static void
pattern_cache_state_clear(PatternCacheState *state)
{
    if (state == NULL) {
        return;
    }
    Py_CLEAR(state->map);
    Py_CLEAR(state->order);
}

static int
pattern_cache_acquire_state(PatternCacheState **state_out, int *lock_held)
{
    if (state_out == NULL || lock_held == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "invalid pattern cache request");
        return -1;
    }

    if (pattern_cache_is_global()) {
        if (global_pattern_cache_lock != NULL) {
            PyThread_acquire_lock(global_pattern_cache_lock, 1);
            *lock_held = 1;
        } else {
            *lock_held = 0;
        }
        if (pattern_cache_state_ensure(&global_pattern_cache) < 0) {
            if (*lock_held) {
                PyThread_release_lock(global_pattern_cache_lock);
            }
            return -1;
        }
        *state_out = &global_pattern_cache;
        return 0;
    }

    PatternCacheState *state = thread_pattern_cache_state_get_or_create();
    if (state == NULL) {
        return -1;
    }
    if (pattern_cache_state_ensure(state) < 0) {
        return -1;
    }
    *state_out = state;
    *lock_held = 0;
    return 0;
}

static inline void
pattern_cache_release_state(int lock_held)
{
    if (lock_held && global_pattern_cache_lock != NULL) {
        PyThread_release_lock(global_pattern_cache_lock);
    }
}

int
pattern_cache_initialize(int global_mode)
{
    atomic_store_explicit(&pattern_cache_global_mode, global_mode ? 1 : 0, memory_order_release);
    if (global_mode) {
        if (global_pattern_cache_lock == NULL) {
            global_pattern_cache_lock = PyThread_allocate_lock();
            if (global_pattern_cache_lock == NULL) {
                PyErr_NoMemory();
                return -1;
            }
        }
        global_pattern_cache.limit = MODULE_COMPILE_CACHE_LIMIT;
        if (pattern_cache_state_ensure(&global_pattern_cache) < 0) {
            return -1;
        }
        return 0;
    }

    global_pattern_cache.limit = MODULE_COMPILE_CACHE_LIMIT;
    pattern_cache_state_clear(&global_pattern_cache);
    if (pattern_cache_tss_initialize() < 0) {
        return -1;
    }
    return 0;
}

void
pattern_cache_teardown(void)
{
    if (pattern_cache_is_global()) {
        pattern_cache_state_clear(&global_pattern_cache);
        if (global_pattern_cache_lock != NULL) {
            PyThread_free_lock(global_pattern_cache_lock);
            global_pattern_cache_lock = NULL;
        }
        atomic_store_explicit(&pattern_cache_global_mode, 0, memory_order_release);
        return;
    }

    if (!atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire)) {
        atomic_store_explicit(&pattern_cache_global_mode, 0, memory_order_release);
        return;
    }

    PatternCacheState *state = thread_pattern_cache_state_get();
    if (state != NULL) {
        pattern_cache_state_clear(state);
        PyMem_Free(state);
        PyThread_tss_set(&pattern_cache_tss, NULL);
    }
    PyThread_tss_delete(&pattern_cache_tss);
    atomic_store_explicit(&pattern_cache_tss_ready, 0, memory_order_release);
    atomic_store_explicit(&pattern_cache_global_mode, 0, memory_order_release);
}

static void
pattern_cache_touch(PatternCacheState *state, PyObject *cache_key)
{
    if (state->order == NULL) {
        return;
    }
    Py_ssize_t idx = PySequence_Index(state->order, cache_key);
    if (idx >= 0) {
        if (PySequence_DelItem(state->order, idx) < 0) {
            PyErr_Clear();
            return;
        }
    } else if (PyErr_Occurred()) {
        PyErr_Clear();
        return;
    }
    if (PyList_Append(state->order, cache_key) < 0) {
        PyErr_Clear();
    }
}

int
pattern_cache_lookup(PyObject *cache_key, PatternObject **out_pattern)
{
    if (out_pattern == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "pattern cache lookup output required");
        return -1;
    }
    *out_pattern = NULL;

    PatternCacheState *state = NULL;
    int lock_held = 0;
    if (pattern_cache_acquire_state(&state, &lock_held) < 0) {
        return -1;
    }

    if (state == NULL || state->map == NULL) {
        pattern_cache_release_state(lock_held);
        return 0;
    }

    PyObject *cached = PyDict_GetItemWithError(state->map, cache_key);
    if (cached != NULL) {
        Py_INCREF(cached);
        *out_pattern = (PatternObject *)cached;
        pattern_cache_touch(state, cache_key);
    } else if (PyErr_Occurred()) {
        pattern_cache_release_state(lock_held);
        return -1;
    }

    pattern_cache_release_state(lock_held);
    return 0;
}

static void
pattern_cache_evict_if_needed(PatternCacheState *state)
{
    if (state->order == NULL || state->map == NULL) {
        return;
    }
    if (state->limit >= 0 && PyList_GET_SIZE(state->order) > state->limit) {
        PyObject *old_key = PyList_GET_ITEM(state->order, 0);
        Py_INCREF(old_key);
        if (PySequence_DelItem(state->order, 0) < 0) {
            PyErr_Clear();
        } else if (PyDict_DelItem(state->map, old_key) < 0) {
            PyErr_Clear();
        }
        Py_DECREF(old_key);
    }
}

int
pattern_cache_store(PyObject *cache_key, PatternObject *pattern)
{
    PatternCacheState *state = NULL;
    int lock_held = 0;
    if (pattern_cache_acquire_state(&state, &lock_held) < 0) {
        return -1;
    }

    if (state == NULL || state->map == NULL) {
        pattern_cache_release_state(lock_held);
        return 0;
    }

    if (PyDict_SetItem(state->map, cache_key, (PyObject *)pattern) < 0) {
        pattern_cache_release_state(lock_held);
        return -1;
    }

    if (state->order != NULL) {
        if (PyList_Append(state->order, cache_key) < 0) {
            PyErr_Clear();
        } else {
            pattern_cache_evict_if_needed(state);
        }
    }

    pattern_cache_release_state(lock_held);
    return 0;
}

void
pattern_cache_clear_current(void)
{
    if (pattern_cache_is_global()) {
        if (global_pattern_cache_lock != NULL) {
            PyThread_acquire_lock(global_pattern_cache_lock, 1);
        }
        pattern_cache_state_clear(&global_pattern_cache);
        if (global_pattern_cache_lock != NULL) {
            PyThread_release_lock(global_pattern_cache_lock);
        }
        return;
    }

    if (!atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire)) {
        return;
    }

    PatternCacheState *state = thread_pattern_cache_state_get();
    if (state != NULL) {
        pattern_cache_state_clear(state);
    }
}

PyObject *
module_clear_pattern_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    pattern_cache_clear_current();
    Py_RETURN_NONE;
}
