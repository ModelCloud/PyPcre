// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

#include <string.h>

typedef struct ThreadCacheState {
    pcre2_match_data *match_cached;
    uint32_t match_ovec_count;
    uint32_t match_capacity;

    pcre2_jit_stack *jit_cached;
    uint32_t jit_capacity;
    size_t jit_start_size;
    size_t jit_max_size;

    pcre2_match_context *match_context;
    pcre2_match_context *offset_match_context;
    PyObject *cleanup_token;
} ThreadCacheState;

static void thread_cache_state_clear(ThreadCacheState *state);
static inline void thread_cache_state_free(ThreadCacheState *state);

typedef enum CacheStrategy {
    CACHE_STRATEGY_THREAD_LOCAL = 0,
    CACHE_STRATEGY_GLOBAL = 1
} CacheStrategy;

static ATOMIC_VAR(int) cache_strategy = ATOMIC_VAR_INIT(CACHE_STRATEGY_THREAD_LOCAL);
static ATOMIC_VAR(int) cache_strategy_locked = ATOMIC_VAR_INIT(0);

static Py_tss_t cache_tss = Py_tss_NEEDS_INIT;
static ATOMIC_VAR(int) cache_tss_ready = ATOMIC_VAR_INIT(0);

static ATOMIC_VAR(int) context_cache_enabled = ATOMIC_VAR_INIT(1);

static ATOMIC_VAR(pcre2_match_data *) global_match_cached = ATOMIC_VAR_INIT(NULL);
static ATOMIC_VAR(uint32_t) global_match_capacity = ATOMIC_VAR_INIT(1);

static ATOMIC_VAR(pcre2_jit_stack *) global_jit_cached = ATOMIC_VAR_INIT(NULL);
static ATOMIC_VAR(uint32_t) global_jit_capacity = ATOMIC_VAR_INIT(1);
static ATOMIC_VAR(size_t) global_jit_start_size = ATOMIC_VAR_INIT(32 * 1024);
static ATOMIC_VAR(size_t) global_jit_max_size = ATOMIC_VAR_INIT(1024 * 1024);
static PyThread_type_lock global_cache_lock = NULL;

static ATOMIC_VAR(int) debug_thread_cache_count = ATOMIC_VAR_INIT(0);
static int debug_thread_cache_enabled = 0;

static PyObject *thread_cache_cleanup_key = NULL;
#define THREAD_CACHE_CAPSULE_NAME "pcre.cache.thread_state"

static void thread_cache_capsule_destructor(PyObject *capsule);

static inline void
global_cache_lock_acquire(void)
{
    if (global_cache_lock != NULL) {
        PyThread_acquire_lock(global_cache_lock, WAIT_LOCK);
    }
}

static inline void
global_cache_lock_release(void)
{
    if (global_cache_lock != NULL) {
        PyThread_release_lock(global_cache_lock);
    }
}

static inline uint32_t
clamp_cache_capacity(size_t value)
{
    return value == 0 ? 0u : 1u;
}

static int
parse_nonnegative_size(PyObject *value, size_t *result)
{
    PyObject *index = PyNumber_Index(value);
    if (index == NULL) {
        return -1;
    }
    size_t parsed = PyLong_AsSize_t(index);
    Py_DECREF(index);
    if (parsed == (size_t)-1 && PyErr_Occurred()) {
        return -1;
    }
    *result = parsed;
    return 0;
}

static inline uint32_t
required_ovector_pairs(PatternObject *self)
{
    uint32_t required = self->capture_count + 1;
    if (required == 0) {
        required = 1;
    }
    return required;
}

static inline ThreadCacheState *
thread_cache_state_get(void)
{
    if (!atomic_load_explicit(&cache_tss_ready, memory_order_acquire)) {
        return NULL;
    }
    return (ThreadCacheState *)PyThread_tss_get(&cache_tss);
}

static ThreadCacheState *
thread_cache_state_get_or_create(void)
{
    ThreadCacheState *state = thread_cache_state_get();
    if (state != NULL) {
        return state;
    }

    if (!atomic_load_explicit(&cache_tss_ready, memory_order_acquire)) {
        PyErr_SetString(PyExc_RuntimeError, "cache subsystem not initialized");
        return NULL;
    }

    state = (ThreadCacheState *)PyMem_Calloc(1, sizeof(*state));
    if (state == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    state->match_capacity = 1;
    state->jit_capacity = 1;
    state->jit_start_size = 32 * 1024;
    state->jit_max_size = 1024 * 1024;

    if (PyThread_tss_set(&cache_tss, state) != 0) {
        PyMem_Free(state);
        PyErr_SetString(PyExc_RuntimeError, "failed to store thread cache state");
        return NULL;
    }

    if (debug_thread_cache_enabled) {
        atomic_fetch_add_explicit(&debug_thread_cache_count, 1, memory_order_relaxed);
    }

    PyObject *dict = PyThreadState_GetDict();
    if (dict == NULL) {
        PyThread_tss_set(&cache_tss, NULL);
        thread_cache_state_free(state);
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_RuntimeError, "thread state dictionary unavailable");
        }
        return NULL;
    }
    {
        PyObject *key = thread_cache_cleanup_key;
        if (key == NULL) {
            PyThread_tss_set(&cache_tss, NULL);
            thread_cache_state_free(state);
            PyErr_SetString(PyExc_RuntimeError, "cache cleanup key unavailable");
            return NULL;
        }
        PyObject *capsule = PyCapsule_New(state, THREAD_CACHE_CAPSULE_NAME, thread_cache_capsule_destructor);
        if (capsule == NULL) {
            PyThread_tss_set(&cache_tss, NULL);
            thread_cache_state_free(state);
            return NULL;
        }
        if (PyDict_SetItem(dict, key, capsule) < 0) {
            Py_DECREF(capsule);
            PyThread_tss_set(&cache_tss, NULL);
            thread_cache_state_free(state);
            return NULL;
        }
        state->cleanup_token = capsule;
        Py_DECREF(capsule);
    }

    return state;
}

static inline void
thread_match_cache_clear(ThreadCacheState *state)
{
    if (state->match_cached != NULL) {
        pcre2_match_data_free(state->match_cached);
        state->match_cached = NULL;
        state->match_ovec_count = 0;
    }
}

static inline void
thread_jit_cache_clear(ThreadCacheState *state)
{
    if (state->jit_cached != NULL) {
        pcre2_jit_stack_free(state->jit_cached);
        state->jit_cached = NULL;
    }
}

static void
thread_cache_state_clear(ThreadCacheState *state)
{
    if (state == NULL) {
        return;
    }

    thread_match_cache_clear(state);
    thread_jit_cache_clear(state);

    if (state->match_context != NULL) {
        pcre2_match_context_free(state->match_context);
        state->match_context = NULL;
    }
    if (state->offset_match_context != NULL) {
        pcre2_match_context_free(state->offset_match_context);
        state->offset_match_context = NULL;
    }
}

static inline void
thread_cache_state_free(ThreadCacheState *state)
{
    if (state == NULL) {
        return;
    }
    thread_cache_state_clear(state);
    if (debug_thread_cache_enabled) {
        atomic_fetch_sub_explicit(&debug_thread_cache_count, 1, memory_order_relaxed);
    }
    PyMem_Free(state);
}

static void
thread_cache_capsule_destructor(PyObject *capsule)
{
    ThreadCacheState *state = PyCapsule_GetPointer(capsule, THREAD_CACHE_CAPSULE_NAME);
    if (state == NULL) {
        PyErr_Clear();
        return;
    }
    if (state->cleanup_token != capsule) {
        return;
    }
    state->cleanup_token = NULL;
    if (atomic_load_explicit(&cache_tss_ready, memory_order_acquire)) {
        ThreadCacheState *current = (ThreadCacheState *)PyThread_tss_get(&cache_tss);
        if (current == state) {
            (void)PyThread_tss_set(&cache_tss, NULL);
        }
    }
    thread_cache_state_free(state);
}

static void
thread_cache_teardown(void)
{
    if (!atomic_load_explicit(&cache_tss_ready, memory_order_acquire)) {
        return;
    }

    ThreadCacheState *state = thread_cache_state_get();
    if (state != NULL) {
        if (state->cleanup_token != NULL) {
            PyObject *dict = PyThreadState_GetDict();
            if (dict != NULL && thread_cache_cleanup_key != NULL) {
                if (PyDict_DelItem(dict, thread_cache_cleanup_key) < 0) {
                    PyErr_Clear();
                }
            }
            PyThread_tss_set(&cache_tss, NULL);
        } else {
            thread_cache_state_free(state);
            PyThread_tss_set(&cache_tss, NULL);
            state = NULL;
        }
    }

    PyThread_tss_delete(&cache_tss);
    atomic_store_explicit(&cache_tss_ready, 0, memory_order_release);
}

static inline void
global_match_cache_clear(void)
{
    global_cache_lock_acquire();
    pcre2_match_data *cached = atomic_exchange_explicit(&global_match_cached, NULL, memory_order_acq_rel);
    global_cache_lock_release();
    if (cached != NULL) {
        pcre2_match_data_free(cached);
    }
}

static inline void
global_jit_cache_clear(void)
{
    global_cache_lock_acquire();
    pcre2_jit_stack *cached = atomic_exchange_explicit(&global_jit_cached, NULL, memory_order_acq_rel);
    global_cache_lock_release();
    if (cached != NULL) {
        pcre2_jit_stack_free(cached);
    }
}

static void
global_cache_teardown(void)
{
    global_match_cache_clear();
    global_jit_cache_clear();
    atomic_store_explicit(&global_match_capacity, 1, memory_order_release);
    atomic_store_explicit(&global_jit_capacity, 1, memory_order_release);
    atomic_store_explicit(&global_jit_start_size, 32 * 1024, memory_order_release);
    atomic_store_explicit(&global_jit_max_size, 1024 * 1024, memory_order_release);
}

static inline CacheStrategy
cache_strategy_get(void)
{
    return (CacheStrategy)atomic_load_explicit(&cache_strategy, memory_order_acquire);
}

static inline void
cache_strategy_set(CacheStrategy strategy)
{
    atomic_store_explicit(&cache_strategy, (int)strategy, memory_order_release);
}

static inline void
cache_strategy_set_locked(int locked)
{
    atomic_store_explicit(&cache_strategy_locked, locked ? 1 : 0, memory_order_release);
}

static inline void
mark_cache_strategy_locked(void)
{
    int expected = 0;
    (void)atomic_compare_exchange_strong_explicit(
        &cache_strategy_locked,
        &expected,
        1,
        memory_order_acq_rel,
        memory_order_acquire
    );
}

static inline const char *
cache_strategy_name(CacheStrategy strategy)
{
    return strategy == CACHE_STRATEGY_THREAD_LOCAL ? "thread-local" : "global";
}

static pcre2_match_data *
thread_match_data_cache_acquire(PatternObject *self)
{
    ThreadCacheState *state = thread_cache_state_get_or_create();
    if (state == NULL) {
        return NULL;
    }

    uint32_t required_pairs = required_ovector_pairs(self);

    if (state->match_capacity != 0 && state->match_cached != NULL) {
        if (state->match_ovec_count >= required_pairs) {
            pcre2_match_data *cached = state->match_cached;
            state->match_cached = NULL;
            state->match_ovec_count = 0;
            return cached;
        }
        pcre2_match_data_free(state->match_cached);
        state->match_cached = NULL;
        state->match_ovec_count = 0;
    }

    pcre2_match_data *match_data = pcre2_match_data_create(required_pairs, NULL);
    if (match_data != NULL) {
        return match_data;
    }

    return pcre2_match_data_create_from_pattern(self->code, NULL);
}

static void
thread_match_data_cache_release(pcre2_match_data *match_data)
{
    ThreadCacheState *state = thread_cache_state_get();
    if (state == NULL || match_data == NULL) {
        if (match_data != NULL) {
            pcre2_match_data_free(match_data);
        }
        return;
    }

    if (state->match_capacity == 0 || state->match_cached != NULL) {
        pcre2_match_data_free(match_data);
        return;
    }

    state->match_cached = match_data;
    state->match_ovec_count = pcre2_get_ovector_count(match_data);
}

static pcre2_match_data *
global_match_data_cache_acquire(PatternObject *self)
{
    uint32_t required_pairs = required_ovector_pairs(self);

    global_cache_lock_acquire();
    if (atomic_load_explicit(&global_match_capacity, memory_order_acquire) != 0) {
        pcre2_match_data *cached = atomic_exchange_explicit(&global_match_cached, NULL, memory_order_acq_rel);
        if (cached != NULL) {
            global_cache_lock_release();
            uint32_t cached_pairs = pcre2_get_ovector_count(cached);
            if (cached_pairs >= required_pairs) {
                return cached;
            }
            pcre2_match_data_free(cached);
            pcre2_match_data *replacement = pcre2_match_data_create(required_pairs, NULL);
            if (replacement != NULL) {
                return replacement;
            }
            return pcre2_match_data_create_from_pattern(self->code, NULL);
        }
    }
    global_cache_lock_release();

    pcre2_match_data *match_data = pcre2_match_data_create(required_pairs, NULL);
    if (match_data != NULL) {
        return match_data;
    }

    return pcre2_match_data_create_from_pattern(self->code, NULL);
}

static void
global_match_data_cache_release(pcre2_match_data *match_data)
{
    if (match_data == NULL) {
        return;
    }

    global_cache_lock_acquire();
    if (atomic_load_explicit(&global_match_capacity, memory_order_acquire) != 0 &&
        atomic_load_explicit(&global_match_cached, memory_order_acquire) == NULL) {
        atomic_store_explicit(&global_match_cached, match_data, memory_order_release);
        global_cache_lock_release();
        return;
    }
    global_cache_lock_release();
    pcre2_match_data_free(match_data);
}

static pcre2_jit_stack *
thread_jit_stack_cache_acquire(void)
{
    ThreadCacheState *state = thread_cache_state_get_or_create();
    if (state == NULL) {
        return NULL;
    }

    if (state->jit_capacity != 0 && state->jit_cached != NULL) {
        pcre2_jit_stack *cached = state->jit_cached;
        state->jit_cached = NULL;
        return cached;
    }

    return pcre2_jit_stack_create(state->jit_start_size, state->jit_max_size, NULL);
}

static void
thread_jit_stack_cache_release(pcre2_jit_stack *jit_stack)
{
    if (jit_stack == NULL) {
        return;
    }

    ThreadCacheState *state = thread_cache_state_get();
    if (state == NULL || state->jit_capacity == 0 || state->jit_cached != NULL) {
        pcre2_jit_stack_free(jit_stack);
        return;
    }

    state->jit_cached = jit_stack;
}

static pcre2_jit_stack *
global_jit_stack_cache_acquire(void)
{
    global_cache_lock_acquire();
    if (atomic_load_explicit(&global_jit_capacity, memory_order_acquire) != 0) {
        pcre2_jit_stack *cached = atomic_exchange_explicit(&global_jit_cached, NULL, memory_order_acq_rel);
        if (cached != NULL) {
            global_cache_lock_release();
            return cached;
        }
    }

    size_t start = atomic_load_explicit(&global_jit_start_size, memory_order_acquire);
    size_t max = atomic_load_explicit(&global_jit_max_size, memory_order_acquire);
    global_cache_lock_release();
    return pcre2_jit_stack_create(start, max, NULL);
}

static void
global_jit_stack_cache_release(pcre2_jit_stack *jit_stack)
{
    if (jit_stack == NULL) {
        return;
    }

    global_cache_lock_acquire();
    if (atomic_load_explicit(&global_jit_capacity, memory_order_acquire) != 0 &&
        atomic_load_explicit(&global_jit_cached, memory_order_acquire) == NULL) {
        atomic_store_explicit(&global_jit_cached, jit_stack, memory_order_release);
        global_cache_lock_release();
        return;
    }
    global_cache_lock_release();
    pcre2_jit_stack_free(jit_stack);
}

int
cache_initialize(int global_mode)
{
    if (global_cache_lock == NULL) {
        global_cache_lock = PyThread_allocate_lock();
        if (global_cache_lock == NULL) {
            PyErr_NoMemory();
            return -1;
        }
    }
    if (!atomic_load_explicit(&cache_tss_ready, memory_order_acquire)) {
        if (PyThread_tss_create(&cache_tss) != 0) {
            PyErr_NoMemory();
            return -1;
        }
        atomic_store_explicit(&cache_tss_ready, 1, memory_order_release);
    }

    if (thread_cache_cleanup_key == NULL) {
        thread_cache_cleanup_key = PyUnicode_FromString("_pcre2_cache_state");
        if (thread_cache_cleanup_key == NULL) {
            return -1;
        }
    }

    debug_thread_cache_enabled = env_flag_is_true(Py_GETENV("PYPCRE_DEBUG"));
    if (!debug_thread_cache_enabled) {
        atomic_store_explicit(&debug_thread_cache_count, 0, memory_order_relaxed);
    }

    cache_strategy_set(global_mode ? CACHE_STRATEGY_GLOBAL : CACHE_STRATEGY_THREAD_LOCAL);
    cache_strategy_set_locked(0);
    atomic_store_explicit(&context_cache_enabled, 1, memory_order_release);

    global_match_cache_clear();
    global_jit_cache_clear();
    atomic_store_explicit(&global_match_capacity, 1, memory_order_release);
    atomic_store_explicit(&global_jit_capacity, 1, memory_order_release);
    atomic_store_explicit(&global_jit_start_size, 32 * 1024, memory_order_release);
    atomic_store_explicit(&global_jit_max_size, 1024 * 1024, memory_order_release);
    return 0;
}

void
cache_teardown(void)
{
    thread_cache_teardown();
    global_cache_teardown();
    if (global_cache_lock != NULL) {
        PyThread_free_lock(global_cache_lock);
        global_cache_lock = NULL;
    }
    cache_strategy_set_locked(0);
    cache_strategy_set(CACHE_STRATEGY_THREAD_LOCAL);
    Py_CLEAR(thread_cache_cleanup_key);
}

pcre2_match_data *
match_data_cache_acquire(PatternObject *self)
{
    mark_cache_strategy_locked();
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        return thread_match_data_cache_acquire(self);
    }
    return global_match_data_cache_acquire(self);
}

void
match_data_cache_release(pcre2_match_data *match_data)
{
    if (match_data == NULL) {
        return;
    }

    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        thread_match_data_cache_release(match_data);
    } else {
        global_match_data_cache_release(match_data);
    }
}

pcre2_match_context *
match_context_cache_acquire(int use_offset_limit)
{
    if (!atomic_load_explicit(&context_cache_enabled, memory_order_acquire)) {
        pcre2_match_context *context = pcre2_match_context_create(NULL);
        if (context == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        return context;
    }

    ThreadCacheState *state = thread_cache_state_get_or_create();
    if (state == NULL) {
        return NULL;
    }

    pcre2_match_context **slot = use_offset_limit ?
        &state->offset_match_context :
        &state->match_context;

    if (*slot == NULL) {
        *slot = pcre2_match_context_create(NULL);
        if (*slot == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
    }

    return *slot;
}

void
match_context_cache_release(pcre2_match_context *context, int had_offset_limit)
{
    if (context == NULL) {
        return;
    }

    pcre2_jit_stack_assign(context, NULL, NULL);

#if defined(PCRE2_USE_OFFSET_LIMIT)
    if (had_offset_limit) {
        (void)pcre2_set_offset_limit(context, PCRE2_UNSET);
    }
#else
    (void)had_offset_limit;
#endif

    if (!atomic_load_explicit(&context_cache_enabled, memory_order_acquire)) {
        pcre2_match_context_free(context);
    }
}

void
cache_set_context_cache_enabled(int enabled)
{
    atomic_store_explicit(&context_cache_enabled, enabled ? 1 : 0, memory_order_release);
}

PyObject *
module_debug_thread_cache_count(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (!debug_thread_cache_enabled) {
        return PyLong_FromLong(-1);
    }
    int value = atomic_load_explicit(&debug_thread_cache_count, memory_order_relaxed);
    return PyLong_FromLong(value);
}

pcre2_jit_stack *
jit_stack_cache_acquire(void)
{
    mark_cache_strategy_locked();
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        return thread_jit_stack_cache_acquire();
    }
    return global_jit_stack_cache_acquire();
}

void
jit_stack_cache_release(pcre2_jit_stack *jit_stack)
{
    if (jit_stack == NULL) {
        return;
    }

    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        thread_jit_stack_cache_release(jit_stack);
    } else {
        global_jit_stack_cache_release(jit_stack);
    }
}

PyObject *
module_get_match_data_cache_size(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return PyLong_FromUnsignedLong((unsigned long)state->match_capacity);
    }

    global_cache_lock_acquire();
    uint32_t capacity = atomic_load_explicit(&global_match_capacity, memory_order_acquire);
    global_cache_lock_release();
    return PyLong_FromUnsignedLong((unsigned long)capacity);
}

PyObject *
module_set_match_data_cache_size(PyObject *Py_UNUSED(module), PyObject *args)
{
    PyObject *size_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &size_obj)) {
        return NULL;
    }
    size_t size = 0;
    if (parse_nonnegative_size(size_obj, &size) < 0) {
        return NULL;
    }

    uint32_t capacity = clamp_cache_capacity(size);
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        state->match_capacity = capacity;
        if (capacity == 0) {
            thread_match_cache_clear(state);
        }
        Py_RETURN_NONE;
    }

    pcre2_match_data *discard = NULL;
    global_cache_lock_acquire();
    atomic_store_explicit(&global_match_capacity, capacity, memory_order_release);
    if (capacity == 0) {
        discard = atomic_exchange_explicit(&global_match_cached, NULL, memory_order_acq_rel);
    }
    global_cache_lock_release();
    if (discard != NULL) {
        pcre2_match_data_free(discard);
    }

    Py_RETURN_NONE;
}

PyObject *
module_clear_match_data_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        thread_match_cache_clear(state);
        Py_RETURN_NONE;
    }

    global_match_cache_clear();
    Py_RETURN_NONE;
}

PyObject *
module_get_match_data_cache_count(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        unsigned long count = state->match_cached != NULL ? 1ul : 0ul;
        return PyLong_FromUnsignedLong(count);
    }

    global_cache_lock_acquire();
    unsigned long count = atomic_load_explicit(&global_match_cached, memory_order_acquire) != NULL ? 1ul : 0ul;
    global_cache_lock_release();
    return PyLong_FromUnsignedLong(count);
}

PyObject *
module_get_jit_stack_cache_size(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return PyLong_FromUnsignedLong((unsigned long)state->jit_capacity);
    }

    global_cache_lock_acquire();
    uint32_t capacity = atomic_load_explicit(&global_jit_capacity, memory_order_acquire);
    global_cache_lock_release();
    return PyLong_FromUnsignedLong((unsigned long)capacity);
}

PyObject *
module_set_jit_stack_cache_size(PyObject *Py_UNUSED(module), PyObject *args)
{
    PyObject *size_obj = NULL;
    if (!PyArg_ParseTuple(args, "O", &size_obj)) {
        return NULL;
    }
    size_t size = 0;
    if (parse_nonnegative_size(size_obj, &size) < 0) {
        return NULL;
    }

    uint32_t capacity = clamp_cache_capacity(size);
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        state->jit_capacity = capacity;
        if (capacity == 0) {
            thread_jit_cache_clear(state);
        }
        Py_RETURN_NONE;
    }

    pcre2_jit_stack *discard = NULL;
    global_cache_lock_acquire();
    atomic_store_explicit(&global_jit_capacity, capacity, memory_order_release);
    if (capacity == 0) {
        discard = atomic_exchange_explicit(&global_jit_cached, NULL, memory_order_acq_rel);
    }
    global_cache_lock_release();
    if (discard != NULL) {
        pcre2_jit_stack_free(discard);
    }

    Py_RETURN_NONE;
}

PyObject *
module_clear_jit_stack_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        thread_jit_cache_clear(state);
        Py_RETURN_NONE;
    }

    global_jit_cache_clear();
    Py_RETURN_NONE;
}

PyObject *
module_get_jit_stack_cache_count(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        unsigned long count = state->jit_cached != NULL ? 1ul : 0ul;
        return PyLong_FromUnsignedLong(count);
    }

    global_cache_lock_acquire();
    unsigned long count = atomic_load_explicit(&global_jit_cached, memory_order_acquire) != NULL ? 1ul : 0ul;
    global_cache_lock_release();
    return PyLong_FromUnsignedLong(count);
}

PyObject *
module_get_jit_stack_limits(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return Py_BuildValue(
            "KK",
            (unsigned long long)state->jit_start_size,
            (unsigned long long)state->jit_max_size
        );
    }

    global_cache_lock_acquire();
    size_t start = atomic_load_explicit(&global_jit_start_size, memory_order_acquire);
    size_t max = atomic_load_explicit(&global_jit_max_size, memory_order_acquire);
    global_cache_lock_release();
    return Py_BuildValue("KK", (unsigned long long)start, (unsigned long long)max);
}

PyObject *
module_set_jit_stack_limits(PyObject *Py_UNUSED(module), PyObject *args)
{
    PyObject *start_obj = NULL;
    PyObject *max_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &start_obj, &max_obj)) {
        return NULL;
    }
    size_t start = 0;
    size_t max = 0;
    if (parse_nonnegative_size(start_obj, &start) < 0 ||
        parse_nonnegative_size(max_obj, &max) < 0) {
        return NULL;
    }

    if (start == 0 || max == 0) {
        PyErr_SetString(PyExc_ValueError, "start and max must be greater than zero");
        return NULL;
    }

    if (start > max) {
        PyErr_SetString(PyExc_ValueError, "start must be <= max");
        return NULL;
    }

    CacheStrategy strategy = cache_strategy_get();
    if (strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        state->jit_start_size = (size_t)start;
        state->jit_max_size = (size_t)max;
        thread_jit_cache_clear(state);
        Py_RETURN_NONE;
    }

    global_cache_lock_acquire();
    atomic_store_explicit(&global_jit_start_size, start, memory_order_release);
    atomic_store_explicit(&global_jit_max_size, max, memory_order_release);
    pcre2_jit_stack *discard = atomic_exchange_explicit(
        &global_jit_cached, NULL, memory_order_acq_rel
    );
    global_cache_lock_release();
    if (discard != NULL) {
        pcre2_jit_stack_free(discard);
    }

    Py_RETURN_NONE;
}

PyObject *
module_get_cache_strategy(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    return PyUnicode_FromString(cache_strategy_name(cache_strategy_get()));
}

PyObject *
module_set_cache_strategy(PyObject *Py_UNUSED(module), PyObject *args)
{
    const char *name = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) {
        return NULL;
    }

    CacheStrategy desired;
    if (strcmp(name, "thread-local") == 0) {
        desired = CACHE_STRATEGY_THREAD_LOCAL;
    } else if (strcmp(name, "global") == 0) {
        desired = CACHE_STRATEGY_GLOBAL;
    } else {
        PyErr_Format(PyExc_ValueError, "unsupported cache strategy '%s'", name);
        return NULL;
    }

    CacheStrategy current = cache_strategy_get();
    if (desired == current) {
        Py_RETURN_NONE;
    }

    PyErr_Format(
        PyExc_RuntimeError,
        "cache strategy already locked to '%s'; set PYPCRE_CACHE_PATTERN_GLOBAL=1 "
        "before importing pcre to enable the global cache",
        cache_strategy_name(current)
    );
    return NULL;
}
