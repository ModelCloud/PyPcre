// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

typedef struct MatchDataCacheEntry {
    pcre2_match_data *match_data;
    uint32_t ovec_count;
    struct MatchDataCacheEntry *next;
} MatchDataCacheEntry;

static MatchDataCacheEntry *match_data_cache_head = NULL;
static uint32_t match_data_cache_capacity = 128;
static uint32_t match_data_cache_count = 0;
static PyThread_type_lock match_data_cache_lock = NULL;

typedef struct JitStackCacheEntry {
    pcre2_jit_stack *jit_stack;
    struct JitStackCacheEntry *next;
} JitStackCacheEntry;

static JitStackCacheEntry *jit_stack_cache_head = NULL;
static uint32_t jit_stack_cache_capacity = 32;
static uint32_t jit_stack_cache_count = 0;
static PyThread_type_lock jit_stack_cache_lock = NULL;
static size_t jit_stack_start_size = 64 * 1024;
static size_t jit_stack_max_size = 1024 * 1024;

static void
match_data_cache_free_all_locked(void)
{
    MatchDataCacheEntry *node = match_data_cache_head;
    match_data_cache_head = NULL;
    match_data_cache_count = 0;

    while (node != NULL) {
        MatchDataCacheEntry *next = node->next;
        pcre2_match_data_free(node->match_data);
        PyMem_Free(node);
        node = next;
    }
}

static void
jit_stack_cache_free_all_locked(void)
{
    JitStackCacheEntry *node = jit_stack_cache_head;
    jit_stack_cache_head = NULL;
    jit_stack_cache_count = 0;

    while (node != NULL) {
        JitStackCacheEntry *next = node->next;
        pcre2_jit_stack_free(node->jit_stack);
        PyMem_Free(node);
        node = next;
    }
}

int
cache_initialize(void)
{
    int match_created = 0;

    if (match_data_cache_lock == NULL) {
        match_data_cache_lock = PyThread_allocate_lock();
        if (match_data_cache_lock == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        match_created = 1;
    }

    if (jit_stack_cache_lock == NULL) {
        jit_stack_cache_lock = PyThread_allocate_lock();
        if (jit_stack_cache_lock == NULL) {
            PyErr_NoMemory();
            if (match_created) {
                PyThread_free_lock(match_data_cache_lock);
                match_data_cache_lock = NULL;
            }
            return -1;
        }
    }

    return 0;
}

void
cache_teardown(void)
{
    if (match_data_cache_lock != NULL) {
        PyThread_acquire_lock(match_data_cache_lock, 1);
        match_data_cache_free_all_locked();
        PyThread_release_lock(match_data_cache_lock);
        PyThread_free_lock(match_data_cache_lock);
        match_data_cache_lock = NULL;
    } else {
        match_data_cache_free_all_locked();
    }

    if (jit_stack_cache_lock != NULL) {
        PyThread_acquire_lock(jit_stack_cache_lock, 1);
        jit_stack_cache_free_all_locked();
        PyThread_release_lock(jit_stack_cache_lock);
        PyThread_free_lock(jit_stack_cache_lock);
        jit_stack_cache_lock = NULL;
    } else {
        jit_stack_cache_free_all_locked();
    }

    match_data_cache_capacity = 128;
    match_data_cache_count = 0;
    jit_stack_cache_capacity = 32;
    jit_stack_cache_count = 0;
    jit_stack_start_size = 64 * 1024;
    jit_stack_max_size = 1024 * 1024;
}

static void
match_data_cache_evict_tail_locked(void)
{
    if (match_data_cache_head == NULL) {
        return;
    }

    MatchDataCacheEntry *prev = NULL;
    MatchDataCacheEntry *node = match_data_cache_head;
    while (node->next != NULL) {
        prev = node;
        node = node->next;
    }

    if (prev != NULL) {
        prev->next = NULL;
    } else {
        match_data_cache_head = NULL;
    }

    if (match_data_cache_count > 0) {
        match_data_cache_count--;
    }

    pcre2_match_data_free(node->match_data);
    PyMem_Free(node);
}

pcre2_match_data *
match_data_cache_acquire(PatternObject *self)
{
    uint32_t required_pairs = self->capture_count + 1;
    if (required_pairs == 0) {
        required_pairs = 1;
    }

    pcre2_match_data *cached = NULL;

    if (match_data_cache_lock != NULL) {
        PyThread_acquire_lock(match_data_cache_lock, 1);
    }

    if (match_data_cache_capacity != 0) {
        MatchDataCacheEntry **link = &match_data_cache_head;
        MatchDataCacheEntry *entry = match_data_cache_head;
        while (entry != NULL) {
            if (entry->ovec_count >= required_pairs) {
                *link = entry->next;
                if (match_data_cache_count > 0) {
                    match_data_cache_count--;
                }
                cached = entry->match_data;
                PyMem_Free(entry);
                break;
            }
            link = &entry->next;
            entry = entry->next;
        }

        if (cached == NULL && match_data_cache_count >= match_data_cache_capacity) {
            match_data_cache_evict_tail_locked();
        }
    }

    if (match_data_cache_lock != NULL) {
        PyThread_release_lock(match_data_cache_lock);
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

void
match_data_cache_release(pcre2_match_data *match_data)
{
    if (match_data == NULL) {
        return;
    }

    if (match_data_cache_lock != NULL) {
        PyThread_acquire_lock(match_data_cache_lock, 1);
    }

    if (match_data_cache_capacity == 0) {
        if (match_data_cache_lock != NULL) {
            PyThread_release_lock(match_data_cache_lock);
        }
        pcre2_match_data_free(match_data);
        return;
    }

    MatchDataCacheEntry *entry = PyMem_Malloc(sizeof(*entry));
    if (entry == NULL) {
        if (match_data_cache_lock != NULL) {
            PyThread_release_lock(match_data_cache_lock);
        }
        pcre2_match_data_free(match_data);
        return;
    }

    entry->match_data = match_data;
    entry->ovec_count = pcre2_get_ovector_count(match_data);
    entry->next = match_data_cache_head;
    match_data_cache_head = entry;
    match_data_cache_count++;

    while (match_data_cache_count > match_data_cache_capacity) {
        match_data_cache_evict_tail_locked();
    }

    if (match_data_cache_lock != NULL) {
        PyThread_release_lock(match_data_cache_lock);
    }
}

static void
jit_stack_cache_evict_tail_locked(void)
{
    if (jit_stack_cache_head == NULL) {
        return;
    }

    JitStackCacheEntry *prev = NULL;
    JitStackCacheEntry *node = jit_stack_cache_head;
    while (node->next != NULL) {
        prev = node;
        node = node->next;
    }

    if (prev != NULL) {
        prev->next = NULL;
    } else {
        jit_stack_cache_head = NULL;
    }

    if (jit_stack_cache_count > 0) {
        jit_stack_cache_count--;
    }

    pcre2_jit_stack_free(node->jit_stack);
    PyMem_Free(node);
}

pcre2_jit_stack *
jit_stack_cache_acquire(void)
{
    pcre2_jit_stack *stack = NULL;

    if (jit_stack_cache_lock != NULL) {
        PyThread_acquire_lock(jit_stack_cache_lock, 1);
    }

    if (jit_stack_cache_head != NULL) {
        JitStackCacheEntry *entry = jit_stack_cache_head;
        jit_stack_cache_head = entry->next;
        if (jit_stack_cache_count > 0) {
            jit_stack_cache_count--;
        }
        stack = entry->jit_stack;
        PyMem_Free(entry);
    }

    if (jit_stack_cache_lock != NULL) {
        PyThread_release_lock(jit_stack_cache_lock);
    }

    if (stack != NULL) {
        return stack;
    }

    return pcre2_jit_stack_create(jit_stack_start_size, jit_stack_max_size, NULL);
}

void
jit_stack_cache_release(pcre2_jit_stack *jit_stack)
{
    if (jit_stack == NULL) {
        return;
    }

    if (jit_stack_cache_lock != NULL) {
        PyThread_acquire_lock(jit_stack_cache_lock, 1);
    }

    if (jit_stack_cache_capacity == 0) {
        if (jit_stack_cache_lock != NULL) {
            PyThread_release_lock(jit_stack_cache_lock);
        }
        pcre2_jit_stack_free(jit_stack);
        return;
    }

    JitStackCacheEntry *entry = PyMem_Malloc(sizeof(*entry));
    if (entry == NULL) {
        if (jit_stack_cache_lock != NULL) {
            PyThread_release_lock(jit_stack_cache_lock);
        }
        pcre2_jit_stack_free(jit_stack);
        return;
    }

    entry->jit_stack = jit_stack;
    entry->next = jit_stack_cache_head;
    jit_stack_cache_head = entry;
    jit_stack_cache_count++;

    while (jit_stack_cache_count > jit_stack_cache_capacity) {
        jit_stack_cache_evict_tail_locked();
    }

    if (jit_stack_cache_lock != NULL) {
        PyThread_release_lock(jit_stack_cache_lock);
    }
}

PyObject *
module_get_match_data_cache_size(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    return PyLong_FromUnsignedLong((unsigned long)match_data_cache_capacity);
}

PyObject *
module_set_match_data_cache_size(PyObject *Py_UNUSED(module), PyObject *args)
{
    unsigned long size = 0;
    if (!PyArg_ParseTuple(args, "k", &size)) {
        return NULL;
    }

    if (match_data_cache_lock != NULL) {
        PyThread_acquire_lock(match_data_cache_lock, 1);
    }

    match_data_cache_capacity = (uint32_t)size;

    while (match_data_cache_count > match_data_cache_capacity) {
        match_data_cache_evict_tail_locked();
    }

    if (match_data_cache_lock != NULL) {
        PyThread_release_lock(match_data_cache_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_clear_match_data_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (match_data_cache_lock != NULL) {
        PyThread_acquire_lock(match_data_cache_lock, 1);
    }

    match_data_cache_free_all_locked();

    if (match_data_cache_lock != NULL) {
        PyThread_release_lock(match_data_cache_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_get_match_data_cache_count(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    unsigned long count = 0;

    if (match_data_cache_lock != NULL) {
        PyThread_acquire_lock(match_data_cache_lock, 1);
    }

    count = match_data_cache_count;

    if (match_data_cache_lock != NULL) {
        PyThread_release_lock(match_data_cache_lock);
    }

    return PyLong_FromUnsignedLong(count);
}

PyObject *
module_get_jit_stack_cache_size(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    return PyLong_FromUnsignedLong((unsigned long)jit_stack_cache_capacity);
}

PyObject *
module_set_jit_stack_cache_size(PyObject *Py_UNUSED(module), PyObject *args)
{
    unsigned long size = 0;
    if (!PyArg_ParseTuple(args, "k", &size)) {
        return NULL;
    }

    if (jit_stack_cache_lock != NULL) {
        PyThread_acquire_lock(jit_stack_cache_lock, 1);
    }

    jit_stack_cache_capacity = (uint32_t)size;

    while (jit_stack_cache_count > jit_stack_cache_capacity) {
        jit_stack_cache_evict_tail_locked();
    }

    if (jit_stack_cache_lock != NULL) {
        PyThread_release_lock(jit_stack_cache_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_clear_jit_stack_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (jit_stack_cache_lock != NULL) {
        PyThread_acquire_lock(jit_stack_cache_lock, 1);
    }

    jit_stack_cache_free_all_locked();

    if (jit_stack_cache_lock != NULL) {
        PyThread_release_lock(jit_stack_cache_lock);
    }

    Py_RETURN_NONE;
}

PyObject *
module_get_jit_stack_cache_count(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    unsigned long count = 0;

    if (jit_stack_cache_lock != NULL) {
        PyThread_acquire_lock(jit_stack_cache_lock, 1);
    }

    count = jit_stack_cache_count;

    if (jit_stack_cache_lock != NULL) {
        PyThread_release_lock(jit_stack_cache_lock);
    }

    return PyLong_FromUnsignedLong(count);
}

PyObject *
module_get_jit_stack_limits(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    return Py_BuildValue("kk", (unsigned long)jit_stack_start_size, (unsigned long)jit_stack_max_size);
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

    if (jit_stack_cache_lock != NULL) {
        PyThread_acquire_lock(jit_stack_cache_lock, 1);
    }

    jit_stack_start_size = (size_t)start;
    jit_stack_max_size = (size_t)max;
    jit_stack_cache_free_all_locked();

    if (jit_stack_cache_lock != NULL) {
        PyThread_release_lock(jit_stack_cache_lock);
    }

    Py_RETURN_NONE;
}
