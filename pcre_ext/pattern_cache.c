// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

#define MODULE_COMPILE_CACHE_LIMIT 128

typedef struct {
    PyObject *map;
    PyObject *order;
    PyObject *cleanup_token;
    Py_ssize_t limit;
} PatternCacheState;

static Py_tss_t pattern_cache_tss = Py_tss_NEEDS_INIT;
static ATOMIC_VAR(int) pattern_cache_tss_ready = ATOMIC_VAR_INIT(0);
/* In global mode, map/order are created during module_exec before the mode
 * flag is published and stay alive for the module's lifetime; mutations are
 * serialized with a critical section on the map object.  A raw PyThread lock
 * here previously deadlocked free-threaded stop-the-world pauses (attached
 * waiter) and self-deadlocked when a GC finalizer re-entered pcre.compile. */
static PatternCacheState global_pattern_cache = {NULL, NULL, NULL, MODULE_COMPILE_CACHE_LIMIT};
static ATOMIC_VAR(int) pattern_cache_global_mode = ATOMIC_VAR_INIT(0);
static PyObject *pattern_cache_cleanup_key = NULL;

#define PATTERN_CACHE_CAPSULE_NAME "pcre.pattern_cache.thread_state"

static void pattern_cache_thread_state_free(PatternCacheState *state);
static void pattern_cache_capsule_destructor(PyObject *capsule);

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

    PyObject *dict = PyThreadState_GetDict();
    if (dict == NULL || pattern_cache_cleanup_key == NULL) {
        PyThread_tss_set(&pattern_cache_tss, NULL);
        pattern_cache_thread_state_free(state);
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_RuntimeError, "thread state dictionary unavailable");
        }
        return NULL;
    }
    {
        PyObject *capsule = PyCapsule_New(
            state,
            PATTERN_CACHE_CAPSULE_NAME,
            pattern_cache_capsule_destructor
        );
        if (capsule == NULL) {
            PyThread_tss_set(&pattern_cache_tss, NULL);
            PyMem_Free(state);
            return NULL;
        }
        if (PyDict_SetItem(dict, pattern_cache_cleanup_key, capsule) < 0) {
            Py_DECREF(capsule);
            PyThread_tss_set(&pattern_cache_tss, NULL);
            PyMem_Free(state);
            return NULL;
        }
        state->cleanup_token = capsule;
        Py_DECREF(capsule);
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

static void
pattern_cache_thread_state_free(PatternCacheState *state)
{
    if (state == NULL) {
        return;
    }
    pattern_cache_state_clear(state);
    PyMem_Free(state);
}

static void
pattern_cache_capsule_destructor(PyObject *capsule)
{
    PatternCacheState *state = PyCapsule_GetPointer(
        capsule,
        PATTERN_CACHE_CAPSULE_NAME
    );
    if (state == NULL) {
        PyErr_Clear();
        return;
    }
    if (state->cleanup_token != capsule) {
        return;
    }
    state->cleanup_token = NULL;
    if (atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire) &&
        thread_pattern_cache_state_get() == state) {
        (void)PyThread_tss_set(&pattern_cache_tss, NULL);
    }
    pattern_cache_thread_state_free(state);
}

static PatternCacheState *
pattern_cache_thread_state_acquire(void)
{
    PatternCacheState *state = thread_pattern_cache_state_get_or_create();
    if (state == NULL) {
        return NULL;
    }
    if (pattern_cache_state_ensure(state) < 0) {
        return NULL;
    }
    return state;
}

int
pattern_cache_initialize(int global_mode)
{
    if (global_mode) {
        global_pattern_cache.limit = MODULE_COMPILE_CACHE_LIMIT;
        if (pattern_cache_state_ensure(&global_pattern_cache) < 0) {
            return -1;
        }
        /* Publish the mode only after the shared containers fully exist so a
         * concurrent lookup cannot observe global mode with a NULL map. */
        atomic_store_explicit(&pattern_cache_global_mode, 1, memory_order_release);
        return 0;
    }

    if (!pattern_cache_is_global()) {
        /* Only reset shared state when not already committed to global mode
         * (a re-exec must not clear a cache other threads are using). */
        global_pattern_cache.limit = MODULE_COMPILE_CACHE_LIMIT;
    }
    atomic_store_explicit(&pattern_cache_global_mode, 0, memory_order_release);
    if (pattern_cache_tss_initialize() < 0) {
        return -1;
    }
    if (pattern_cache_cleanup_key == NULL) {
        pattern_cache_cleanup_key = PyUnicode_FromString("_pcre2_pattern_cache_state");
        if (pattern_cache_cleanup_key == NULL) {
            return -1;
        }
    }
    return 0;
}

void
pattern_cache_teardown(void)
{
    if (pattern_cache_is_global()) {
        /* Only reached when the very first module initialization fails
         * (module_exec guards re-exec error paths), so no other thread can
         * hold references into these containers. */
        atomic_store_explicit(&pattern_cache_global_mode, 0, memory_order_release);
        pattern_cache_state_clear(&global_pattern_cache);
        Py_CLEAR(pattern_cache_cleanup_key);
        return;
    }

    if (!atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire)) {
        atomic_store_explicit(&pattern_cache_global_mode, 0, memory_order_release);
        return;
    }

    PatternCacheState *state = thread_pattern_cache_state_get();
    if (state != NULL) {
        if (state->cleanup_token != NULL) {
            PyObject *dict = PyThreadState_GetDict();
            if (dict != NULL && pattern_cache_cleanup_key != NULL) {
                if (PyDict_DelItem(dict, pattern_cache_cleanup_key) < 0) {
                    PyErr_Clear();
                }
            }
        } else {
            pattern_cache_thread_state_free(state);
            PyThread_tss_set(&pattern_cache_tss, NULL);
        }
    }
    PyThread_tss_delete(&pattern_cache_tss);
    atomic_store_explicit(&pattern_cache_tss_ready, 0, memory_order_release);
    atomic_store_explicit(&pattern_cache_global_mode, 0, memory_order_release);
    Py_CLEAR(pattern_cache_cleanup_key);
}

int
pattern_cache_lookup(PyObject *cache_key, PatternObject **out_pattern)
{
    if (out_pattern == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "pattern cache lookup output required");
        return -1;
    }
    *out_pattern = NULL;

    if (pattern_cache_is_global()) {
        PyObject *map = global_pattern_cache.map;
        if (map == NULL) {
            return 0;
        }
#if PY_VERSION_HEX >= 0x030D0000
        /* Owned-reference lookup under the dict's internal lock: safe against
         * concurrent eviction on free-threaded builds. */
        PyObject *cached = NULL;
        if (PyDict_GetItemRef(map, cache_key, &cached) < 0) {
            return -1;
        }
        *out_pattern = (PatternObject *)cached;
        return 0;
#else
        PyObject *cached = PyDict_GetItemWithError(map, cache_key);
        if (cached != NULL) {
            Py_INCREF(cached);
            *out_pattern = (PatternObject *)cached;
        } else if (PyErr_Occurred()) {
            return -1;
        }
        return 0;
#endif
    }

    PatternCacheState *state = pattern_cache_thread_state_acquire();
    if (state == NULL) {
        return -1;
    }
    if (state->map == NULL) {
        return 0;
    }

    PyObject *cached = PyDict_GetItemWithError(state->map, cache_key);
    if (cached != NULL) {
        Py_INCREF(cached);
        *out_pattern = (PatternObject *)cached;
    } else if (PyErr_Occurred()) {
        return -1;
    }
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
        /* Remove from the map first: if the order-list delete fails the entry
         * is retried next time, whereas the reverse order could strand an
         * unevictable entry in the map and let it grow past the limit. */
        if (PyDict_DelItem(state->map, old_key) < 0) {
            PyErr_Clear();
        }
        if (PySequence_DelItem(state->order, 0) < 0) {
            PyErr_Clear();
        }
        Py_DECREF(old_key);
    }
}

static int
pattern_cache_store_locked(PatternCacheState *state, PyObject *cache_key, PatternObject *pattern)
{
    int already_present = PyDict_Contains(state->map, cache_key);
    if (already_present < 0) {
        return -1;
    }
    if (PyDict_SetItem(state->map, cache_key, (PyObject *)pattern) < 0) {
        return -1;
    }

    if (!already_present && state->order != NULL) {
        if (PyList_Append(state->order, cache_key) < 0) {
            PyErr_Clear();
        } else {
            pattern_cache_evict_if_needed(state);
        }
    }
    return 0;
}

int
pattern_cache_store(PyObject *cache_key, PatternObject *pattern)
{
    if (pattern_cache_is_global()) {
        PyObject *map = global_pattern_cache.map;
        if (map == NULL) {
            return 0;
        }
        int rc = 0;
        /* Critical sections keep map/order coherent without the deadlocks of
         * a raw lock: a blocked waiter parks GC-safely, and reentrant entry
         * from a GC finalizer suspends the outer section instead of hanging. */
        Py_BEGIN_CRITICAL_SECTION(map);
        rc = pattern_cache_store_locked(&global_pattern_cache, cache_key, pattern);
        Py_END_CRITICAL_SECTION();
        return rc;
    }

    PatternCacheState *state = pattern_cache_thread_state_acquire();
    if (state == NULL) {
        return -1;
    }
    if (state->map == NULL) {
        return 0;
    }
    return pattern_cache_store_locked(state, cache_key, pattern);
}

void
pattern_cache_clear_current(void)
{
    if (pattern_cache_is_global()) {
        PyObject *map = global_pattern_cache.map;
        if (map == NULL) {
            return;
        }
        /* Empty the containers in place; the container objects themselves
         * stay alive for the module's lifetime so concurrent lookups and
         * stores always see valid dicts/lists. */
        Py_BEGIN_CRITICAL_SECTION(map);
        PyDict_Clear(map);
        if (global_pattern_cache.order != NULL) {
            if (PyList_SetSlice(global_pattern_cache.order, 0,
                                PyList_GET_SIZE(global_pattern_cache.order), NULL) < 0) {
                PyErr_Clear();
            }
        }
        Py_END_CRITICAL_SECTION();
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
