// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

#include <string.h>

typedef struct MatchDataCacheEntry {
    pcre2_match_data *match_data;
    uint32_t ovec_count;
    struct MatchDataCacheEntry *next;
} MatchDataCacheEntry;

typedef struct JitStackCacheEntry {
    pcre2_jit_stack *jit_stack;
    struct JitStackCacheEntry *next;
} JitStackCacheEntry;

typedef struct ThreadCacheState {
    MatchDataCacheEntry *match_head;
    uint32_t match_capacity;
    uint32_t match_count;

    JitStackCacheEntry *jit_head;
    uint32_t jit_capacity;
    uint32_t jit_count;
    size_t jit_start_size;
    size_t jit_max_size;
} ThreadCacheState;

typedef enum CacheStrategy {
    CACHE_STRATEGY_THREAD_LOCAL = 0,
    CACHE_STRATEGY_GLOBAL = 1
} CacheStrategy;

static CacheStrategy cache_strategy = CACHE_STRATEGY_THREAD_LOCAL;
static int cache_strategy_locked = 0;

static inline const char *
cache_strategy_name(CacheStrategy strategy)
{
    return strategy == CACHE_STRATEGY_THREAD_LOCAL ? "thread-local" : "global";
}

static inline void
mark_cache_strategy_locked(void)
{
    cache_strategy_locked = 1;
}

/* -------------------------------------------------------------------------- */
/* Thread-local cache implementation                                         */
/* -------------------------------------------------------------------------- */

static Py_tss_t cache_tss = Py_tss_NEEDS_INIT;
static int cache_tss_created = 0;

static ThreadCacheState *thread_cache_state_get(void);
static ThreadCacheState *thread_cache_state_get_or_create(void);
static void thread_match_data_cache_free_all(ThreadCacheState *state);
static void thread_match_data_cache_evict_tail(ThreadCacheState *state);
static void thread_jit_stack_cache_free_all(ThreadCacheState *state);
static void thread_jit_stack_cache_evict_tail(ThreadCacheState *state);

static int
thread_cache_initialize(void)
{
    if (!cache_tss_created) {
        if (PyThread_tss_create(&cache_tss) != 0) {
            PyErr_NoMemory();
            return -1;
        }
        cache_tss_created = 1;
    }

    if (thread_cache_state_get() == NULL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            PyThread_tss_delete(&cache_tss);
            cache_tss_created = 0;
            return -1;
        }
        (void)state;
    }

    return 0;
}

static void
thread_cache_teardown(void)
{
    if (!cache_tss_created) {
        return;
    }

    ThreadCacheState *state = thread_cache_state_get();
    if (state != NULL) {
        thread_match_data_cache_free_all(state);
        thread_jit_stack_cache_free_all(state);
        PyMem_Free(state);
        PyThread_tss_set(&cache_tss, NULL);
    }

    PyThread_tss_delete(&cache_tss);
    cache_tss_created = 0;
}

static ThreadCacheState *
thread_cache_state_get(void)
{
    if (!cache_tss_created) {
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

    if (!cache_tss_created) {
        PyErr_SetString(PyExc_RuntimeError, "cache subsystem not initialized");
        return NULL;
    }

    state = (ThreadCacheState *)PyMem_Calloc(1, sizeof(*state));
    if (state == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    state->match_capacity = 8;
    state->jit_capacity = 4;
    state->jit_start_size = 32 * 1024;
    state->jit_max_size = 1024 * 1024;

    if (PyThread_tss_set(&cache_tss, state) != 0) {
        PyMem_Free(state);
        PyErr_SetString(PyExc_RuntimeError, "failed to store thread cache state");
        return NULL;
    }

    return state;
}

static void
thread_match_data_cache_free_all(ThreadCacheState *state)
{
    MatchDataCacheEntry *node = state->match_head;
    state->match_head = NULL;
    state->match_count = 0;

    while (node != NULL) {
        MatchDataCacheEntry *next = node->next;
        pcre2_match_data_free(node->match_data);
        pcre_free(node);
        node = next;
    }
}

static void
thread_match_data_cache_evict_tail(ThreadCacheState *state)
{
    MatchDataCacheEntry *prev = NULL;
    MatchDataCacheEntry *node = state->match_head;

    if (node == NULL) {
        return;
    }

    while (node->next != NULL) {
        prev = node;
        node = node->next;
    }

    if (prev != NULL) {
        prev->next = NULL;
    } else {
        state->match_head = NULL;
    }

    if (state->match_count > 0) {
        state->match_count--;
    }

    pcre2_match_data_free(node->match_data);
    pcre_free(node);
}

static void
thread_jit_stack_cache_free_all(ThreadCacheState *state)
{
    JitStackCacheEntry *node = state->jit_head;
    state->jit_head = NULL;
    state->jit_count = 0;

    while (node != NULL) {
        JitStackCacheEntry *next = node->next;
        pcre2_jit_stack_free(node->jit_stack);
        pcre_free(node);
        node = next;
    }
}

static void
thread_jit_stack_cache_evict_tail(ThreadCacheState *state)
{
    JitStackCacheEntry *prev = NULL;
    JitStackCacheEntry *node = state->jit_head;

    if (node == NULL) {
        return;
    }

    while (node->next != NULL) {
        prev = node;
        node = node->next;
    }

    if (prev != NULL) {
        prev->next = NULL;
    } else {
        state->jit_head = NULL;
    }

    if (state->jit_count > 0) {
        state->jit_count--;
    }

    pcre2_jit_stack_free(node->jit_stack);
    pcre_free(node);
}

/* -------------------------------------------------------------------------- */
/* Global cache implementation                                               */
/* -------------------------------------------------------------------------- */

static MatchDataCacheEntry *global_match_head = NULL;
static uint32_t global_match_capacity = 32;
static uint32_t global_match_count = 0;
static PyThread_type_lock global_match_lock = NULL;

static JitStackCacheEntry *global_jit_head = NULL;
static uint32_t global_jit_capacity = 16;
static uint32_t global_jit_count = 0;
static PyThread_type_lock global_jit_lock = NULL;
static size_t global_jit_start_size = 32 * 1024;
static size_t global_jit_max_size = 1024 * 1024;

static int
global_cache_initialize(void)
{
    int match_allocated = 0;

    if (global_match_lock == NULL) {
        global_match_lock = PyThread_allocate_lock();
        if (global_match_lock == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        match_allocated = 1;
    }

    if (global_jit_lock == NULL) {
        global_jit_lock = PyThread_allocate_lock();
        if (global_jit_lock == NULL) {
            PyErr_NoMemory();
            if (match_allocated) {
                PyThread_free_lock(global_match_lock);
                global_match_lock = NULL;
            }
            return -1;
        }
    }

    return 0;
}

static void
global_match_data_cache_free_all_locked(void)
{
    MatchDataCacheEntry *node = global_match_head;
    global_match_head = NULL;
    global_match_count = 0;

    while (node != NULL) {
        MatchDataCacheEntry *next = node->next;
        pcre2_match_data_free(node->match_data);
        pcre_free(node);
        node = next;
    }
}

static void
global_match_data_cache_evict_tail_locked(void)
{
    if (global_match_head == NULL) {
        return;
    }

    MatchDataCacheEntry *prev = NULL;
    MatchDataCacheEntry *node = global_match_head;
    while (node->next != NULL) {
        prev = node;
        node = node->next;
    }

    if (prev != NULL) {
        prev->next = NULL;
    } else {
        global_match_head = NULL;
    }

    if (global_match_count > 0) {
        global_match_count--;
    }

    pcre2_match_data_free(node->match_data);
    pcre_free(node);
}

static void
global_jit_stack_cache_free_all_locked(void)
{
    JitStackCacheEntry *node = global_jit_head;
    global_jit_head = NULL;
    global_jit_count = 0;

    while (node != NULL) {
        JitStackCacheEntry *next = node->next;
        pcre2_jit_stack_free(node->jit_stack);
        pcre_free(node);
        node = next;
    }
}

static void
global_jit_stack_cache_evict_tail_locked(void)
{
    if (global_jit_head == NULL) {
        return;
    }

    JitStackCacheEntry *prev = NULL;
    JitStackCacheEntry *node = global_jit_head;
    while (node->next != NULL) {
        prev = node;
        node = node->next;
    }

    if (prev != NULL) {
        prev->next = NULL;
    } else {
        global_jit_head = NULL;
    }

    if (global_jit_count > 0) {
        global_jit_count--;
    }

    pcre2_jit_stack_free(node->jit_stack);
    pcre_free(node);
}

static void
global_cache_teardown(void)
{
    if (global_match_lock != NULL) {
        PyThread_acquire_lock(global_match_lock, 1);
        global_match_data_cache_free_all_locked();
        PyThread_release_lock(global_match_lock);
        PyThread_free_lock(global_match_lock);
        global_match_lock = NULL;
    } else {
        global_match_data_cache_free_all_locked();
    }

    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
        global_jit_stack_cache_free_all_locked();
        PyThread_release_lock(global_jit_lock);
        PyThread_free_lock(global_jit_lock);
        global_jit_lock = NULL;
    } else {
        global_jit_stack_cache_free_all_locked();
    }

    global_match_capacity = 32;
    global_match_count = 0;
    global_jit_capacity = 16;
    global_jit_count = 0;
    global_jit_start_size = 32 * 1024;
    global_jit_max_size = 1024 * 1024;
}

/* -------------------------------------------------------------------------- */
/* Shared entry points                                                        */
/* -------------------------------------------------------------------------- */

int
cache_initialize(void)
{
    if (thread_cache_initialize() < 0) {
        return -1;
    }
    if (global_cache_initialize() < 0) {
        thread_cache_teardown();
        return -1;
    }
    return 0;
}

void
cache_teardown(void)
{
    thread_cache_teardown();
    global_cache_teardown();
    cache_strategy_locked = 0;
    cache_strategy = CACHE_STRATEGY_THREAD_LOCAL;
}

static pcre2_match_data *
thread_match_data_cache_acquire(PatternObject *self)
{
    ThreadCacheState *state = thread_cache_state_get_or_create();
    uint32_t required_pairs;
    pcre2_match_data *cached = NULL;

    if (state == NULL) {
        return NULL;
    }

    required_pairs = self->capture_count + 1;
    if (required_pairs == 0) {
        required_pairs = 1;
    }

    if (state->match_capacity != 0) {
        MatchDataCacheEntry **link = &state->match_head;
        MatchDataCacheEntry *entry = state->match_head;
        while (entry != NULL) {
            if (entry->ovec_count >= required_pairs) {
                *link = entry->next;
                if (state->match_count > 0) {
                    state->match_count--;
                }
                cached = entry->match_data;
                pcre_free(entry);
                break;
            }
            link = &entry->next;
            entry = entry->next;
        }

        if (cached == NULL && state->match_count >= state->match_capacity) {
            thread_match_data_cache_evict_tail(state);
        }
    }

    if (cached != NULL) {
        return cached;
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

    if (state->match_capacity == 0) {
        pcre2_match_data_free(match_data);
        return;
    }

    MatchDataCacheEntry *entry = pcre_malloc(sizeof(*entry));
    if (entry == NULL) {
        pcre2_match_data_free(match_data);
        return;
    }

    entry->match_data = match_data;
    entry->ovec_count = pcre2_get_ovector_count(match_data);
    entry->next = state->match_head;
    state->match_head = entry;
    state->match_count++;

    while (state->match_count > state->match_capacity) {
        thread_match_data_cache_evict_tail(state);
    }
}

static pcre2_match_data *
global_match_data_cache_acquire(PatternObject *self)
{
    uint32_t required_pairs;
    pcre2_match_data *cached = NULL;

    required_pairs = self->capture_count + 1;
    if (required_pairs == 0) {
        required_pairs = 1;
    }

    if (global_match_lock != NULL) {
        PyThread_acquire_lock(global_match_lock, 1);
    }

    if (global_match_capacity != 0) {
        MatchDataCacheEntry **link = &global_match_head;
        MatchDataCacheEntry *entry = global_match_head;
        while (entry != NULL) {
            if (entry->ovec_count >= required_pairs) {
                *link = entry->next;
                if (global_match_count > 0) {
                    global_match_count--;
                }
                cached = entry->match_data;
                pcre_free(entry);
                break;
            }
            link = &entry->next;
            entry = entry->next;
        }

        if (cached == NULL && global_match_count >= global_match_capacity) {
            global_match_data_cache_evict_tail_locked();
        }
    }

    if (global_match_lock != NULL) {
        PyThread_release_lock(global_match_lock);
    }

    if (cached != NULL) {
        return cached;
    }

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

    if (global_match_lock != NULL) {
        PyThread_acquire_lock(global_match_lock, 1);
    }

    if (global_match_capacity == 0) {
        if (global_match_lock != NULL) {
            PyThread_release_lock(global_match_lock);
        }
        pcre2_match_data_free(match_data);
        return;
    }

    MatchDataCacheEntry *entry = pcre_malloc(sizeof(*entry));
    if (entry == NULL) {
        if (global_match_lock != NULL) {
            PyThread_release_lock(global_match_lock);
        }
        pcre2_match_data_free(match_data);
        return;
    }

    entry->match_data = match_data;
    entry->ovec_count = pcre2_get_ovector_count(match_data);
    entry->next = global_match_head;
    global_match_head = entry;
    global_match_count++;

    while (global_match_count > global_match_capacity) {
        global_match_data_cache_evict_tail_locked();
    }

    if (global_match_lock != NULL) {
        PyThread_release_lock(global_match_lock);
    }
}

static pcre2_jit_stack *
thread_jit_stack_cache_acquire(void)
{
    ThreadCacheState *state = thread_cache_state_get_or_create();
    pcre2_jit_stack *stack = NULL;

    if (state == NULL) {
        return NULL;
    }

    if (state->jit_head != NULL) {
        JitStackCacheEntry *entry = state->jit_head;
        state->jit_head = entry->next;
        if (state->jit_count > 0) {
            state->jit_count--;
        }
        stack = entry->jit_stack;
        pcre_free(entry);
    }

    if (stack != NULL) {
        return stack;
    }

    return pcre2_jit_stack_create(state->jit_start_size, state->jit_max_size, NULL);
}

static void
thread_jit_stack_cache_release(pcre2_jit_stack *jit_stack)
{
    ThreadCacheState *state = thread_cache_state_get();
    if (state == NULL || jit_stack == NULL) {
        if (jit_stack != NULL) {
            pcre2_jit_stack_free(jit_stack);
        }
        return;
    }

    if (state->jit_capacity == 0) {
        pcre2_jit_stack_free(jit_stack);
        return;
    }

    JitStackCacheEntry *entry = pcre_malloc(sizeof(*entry));
    if (entry == NULL) {
        pcre2_jit_stack_free(jit_stack);
        return;
    }

    entry->jit_stack = jit_stack;
    entry->next = state->jit_head;
    state->jit_head = entry;
    state->jit_count++;

    while (state->jit_count > state->jit_capacity) {
        thread_jit_stack_cache_evict_tail(state);
    }
}

static pcre2_jit_stack *
global_jit_stack_cache_acquire(void)
{
    pcre2_jit_stack *stack = NULL;
    size_t start_size = 0;
    size_t max_size = 0;

    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }

    if (global_jit_head != NULL) {
        JitStackCacheEntry *entry = global_jit_head;
        global_jit_head = entry->next;
        if (global_jit_count > 0) {
            global_jit_count--;
        }
        stack = entry->jit_stack;
        pcre_free(entry);
    }

    start_size = global_jit_start_size;
    max_size = global_jit_max_size;

    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }

    if (stack != NULL) {
        return stack;
    }

    return pcre2_jit_stack_create(start_size, max_size, NULL);
}

static void
global_jit_stack_cache_release(pcre2_jit_stack *jit_stack)
{
    if (jit_stack == NULL) {
        return;
    }

    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }

    if (global_jit_capacity == 0) {
        if (global_jit_lock != NULL) {
            PyThread_release_lock(global_jit_lock);
        }
        pcre2_jit_stack_free(jit_stack);
        return;
    }

    JitStackCacheEntry *entry = pcre_malloc(sizeof(*entry));
    if (entry == NULL) {
        if (global_jit_lock != NULL) {
            PyThread_release_lock(global_jit_lock);
        }
        pcre2_jit_stack_free(jit_stack);
        return;
    }

    entry->jit_stack = jit_stack;
    entry->next = global_jit_head;
    global_jit_head = entry;
    global_jit_count++;

    while (global_jit_count > global_jit_capacity) {
        global_jit_stack_cache_evict_tail_locked();
    }

    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }
}

pcre2_match_data *
match_data_cache_acquire(PatternObject *self)
{
    mark_cache_strategy_locked();
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
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
    mark_cache_strategy_locked();
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        thread_match_data_cache_release(match_data);
    } else {
        global_match_data_cache_release(match_data);
    }
}

pcre2_jit_stack *
jit_stack_cache_acquire(void)
{
    mark_cache_strategy_locked();
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
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
    mark_cache_strategy_locked();
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        thread_jit_stack_cache_release(jit_stack);
    } else {
        global_jit_stack_cache_release(jit_stack);
    }
}

PyObject *
module_get_match_data_cache_size(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return PyLong_FromUnsignedLong((unsigned long)state->match_capacity);
    }

    unsigned long value = 0;
    if (global_match_lock != NULL) {
        PyThread_acquire_lock(global_match_lock, 1);
    }
    value = global_match_capacity;
    if (global_match_lock != NULL) {
        PyThread_release_lock(global_match_lock);
    }
    return PyLong_FromUnsignedLong(value);
}

PyObject *
module_set_match_data_cache_size(PyObject *Py_UNUSED(module), PyObject *args)
{
    unsigned long size = 0;
    if (!PyArg_ParseTuple(args, "k", &size)) {
        return NULL;
    }

    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        state->match_capacity = (uint32_t)size;
        while (state->match_count > state->match_capacity) {
            thread_match_data_cache_evict_tail(state);
        }
        Py_RETURN_NONE;
    }

    if (global_match_lock != NULL) {
        PyThread_acquire_lock(global_match_lock, 1);
    }

    global_match_capacity = (uint32_t)size;
    while (global_match_count > global_match_capacity) {
        global_match_data_cache_evict_tail_locked();
    }

    if (global_match_lock != NULL) {
        PyThread_release_lock(global_match_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_clear_match_data_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        thread_match_data_cache_free_all(state);
        Py_RETURN_NONE;
    }

    if (global_match_lock != NULL) {
        PyThread_acquire_lock(global_match_lock, 1);
    }
    global_match_data_cache_free_all_locked();
    if (global_match_lock != NULL) {
        PyThread_release_lock(global_match_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_get_match_data_cache_count(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return PyLong_FromUnsignedLong((unsigned long)state->match_count);
    }

    unsigned long count = 0;
    if (global_match_lock != NULL) {
        PyThread_acquire_lock(global_match_lock, 1);
    }
    count = global_match_count;
    if (global_match_lock != NULL) {
        PyThread_release_lock(global_match_lock);
    }
    return PyLong_FromUnsignedLong(count);
}

PyObject *
module_get_jit_stack_cache_size(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return PyLong_FromUnsignedLong((unsigned long)state->jit_capacity);
    }

    unsigned long value = 0;
    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }
    value = global_jit_capacity;
    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }
    return PyLong_FromUnsignedLong(value);
}

PyObject *
module_set_jit_stack_cache_size(PyObject *Py_UNUSED(module), PyObject *args)
{
    unsigned long size = 0;
    if (!PyArg_ParseTuple(args, "k", &size)) {
        return NULL;
    }

    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        state->jit_capacity = (uint32_t)size;
        while (state->jit_count > state->jit_capacity) {
            thread_jit_stack_cache_evict_tail(state);
        }
        Py_RETURN_NONE;
    }

    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }

    global_jit_capacity = (uint32_t)size;
    while (global_jit_count > global_jit_capacity) {
        global_jit_stack_cache_evict_tail_locked();
    }

    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_clear_jit_stack_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        thread_jit_stack_cache_free_all(state);
        Py_RETURN_NONE;
    }

    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }
    global_jit_stack_cache_free_all_locked();
    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_get_jit_stack_cache_count(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return PyLong_FromUnsignedLong((unsigned long)state->jit_count);
    }

    unsigned long count = 0;
    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }
    count = global_jit_count;
    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }
    return PyLong_FromUnsignedLong(count);
}

PyObject *
module_get_jit_stack_limits(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        return Py_BuildValue(
            "kk",
            (unsigned long)state->jit_start_size,
            (unsigned long)state->jit_max_size
        );
    }

    unsigned long start = 0;
    unsigned long max = 0;
    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }
    start = (unsigned long)global_jit_start_size;
    max = (unsigned long)global_jit_max_size;
    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }

    return Py_BuildValue("kk", start, max);
}

PyObject *
module_set_jit_stack_limits(PyObject *Py_UNUSED(module), PyObject *args)
{
    unsigned long start = 0;
    unsigned long max = 0;

    if (!PyArg_ParseTuple(args, "kk", &start, &max)) {
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

    if (cache_strategy == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheState *state = thread_cache_state_get_or_create();
        if (state == NULL) {
            return NULL;
        }
        state->jit_start_size = (size_t)start;
        state->jit_max_size = (size_t)max;
        thread_jit_stack_cache_free_all(state);
        Py_RETURN_NONE;
    }

    if (global_jit_lock != NULL) {
        PyThread_acquire_lock(global_jit_lock, 1);
    }

    global_jit_start_size = (size_t)start;
    global_jit_max_size = (size_t)max;
    global_jit_stack_cache_free_all_locked();

    if (global_jit_lock != NULL) {
        PyThread_release_lock(global_jit_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_get_cache_strategy(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    return PyUnicode_FromString(cache_strategy_name(cache_strategy));
}

PyObject *
module_set_cache_strategy(PyObject *Py_UNUSED(module), PyObject *args)
{
    const char *name = NULL;
    CacheStrategy desired = CACHE_STRATEGY_THREAD_LOCAL;

    if (!PyArg_ParseTuple(args, "s", &name)) {
        return NULL;
    }

    if (strcmp(name, "thread-local") == 0) {
        desired = CACHE_STRATEGY_THREAD_LOCAL;
    } else if (strcmp(name, "global") == 0) {
        desired = CACHE_STRATEGY_GLOBAL;
    } else {
        PyErr_Format(PyExc_ValueError, "unsupported cache strategy '%s'", name);
        return NULL;
    }

    if (cache_strategy_locked && desired != cache_strategy) {
        PyErr_Format(
            PyExc_RuntimeError,
            "cache strategy already locked to '%s'",
            cache_strategy_name(cache_strategy)
        );
        return NULL;
    }

    cache_strategy = desired;
    Py_RETURN_NONE;
}
