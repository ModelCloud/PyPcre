// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"
#include <stdio.h>
#include <string.h>
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#include <immintrin.h>
#endif

static inline int
is_hex_digit(unsigned char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static inline unsigned int
hex_value(unsigned char value)
{
    if (value >= '0' && value <= '9') {
        return (unsigned int)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (unsigned int)(value - 'a' + 10);
    }
    return (unsigned int)(value - 'A' + 10);
}

#define STRINGIFY_DETAIL(value) #value
#define STRINGIFY(value) STRINGIFY_DETAIL(value)

static const char *
resolve_pcre2_prerelease(void)
{
    const char *raw = STRINGIFY(Z PCRE2_PRERELEASE);

    if (raw[1] == '\0') {
        return "";
    }

    raw += 1;
    while (*raw == ' ') {
        raw++;
    }

    return raw;
}

static char pcre2_library_version[64] = "unknown";
static ATOMIC_VAR(int) pcre2_version_initialized = 0;
#if defined(PCRE2_USE_OFFSET_LIMIT)
static ATOMIC_VAR(int) offset_limit_support = ATOMIC_VAR_INIT(-1);
#endif

static void detect_offset_limit_support(void);

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

static int pcre_force_jit_lock = 0;
static PyThread_type_lock jit_serial_lock = NULL;

#if defined(PCRE_EXT_HAVE_ATOMICS)
static ATOMIC_VAR(int) default_jit_enabled = 1;
#else
static int default_jit_enabled = 1;
static PyThread_type_lock default_jit_lock = NULL;
#endif

static int
env_flag_is_true(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    switch (value[0]) {
        case '0':
        case 'f':
        case 'F':
        case 'n':
        case 'N':
            return 0;
        default:
            return 1;
    }
}

#define PCRE2_CALL_WITHOUT_GIL(call)     \
    do {                                 \
        rc = (call);                     \
    } while (0)

static inline void
jit_guard_acquire(void)
{
    if (jit_serial_lock != NULL) {
        PyThread_acquire_lock(jit_serial_lock, 1);
    }
}

static inline void
jit_guard_release(void)
{
    if (jit_serial_lock != NULL) {
        PyThread_release_lock(jit_serial_lock);
    }
}

static inline pcre2_match_data *
pattern_match_data_acquire(PatternObject *pattern, int *from_pattern_cache)
{
    *from_pattern_cache = 0;
#if defined(PCRE_EXT_HAVE_ATOMICS)
    pcre2_match_data *cached = atomic_exchange_explicit(
        &pattern->cached_match_data,
        NULL,
        memory_order_acq_rel
    );
    if (cached != NULL) {
        *from_pattern_cache = 1;
        return cached;
    }
#else
    (void)pattern;
#endif
    return match_data_cache_acquire(pattern);
}

static inline void
pattern_match_data_release(PatternObject *pattern,
                           pcre2_match_data *match_data,
                           int from_pattern_cache)
{
    if (match_data == NULL) {
        return;
    }
#if defined(PCRE_EXT_HAVE_ATOMICS)
    if (from_pattern_cache) {
        pcre2_match_data *expected = NULL;
        if (!atomic_compare_exchange_strong_explicit(
                &pattern->cached_match_data,
                &expected,
                match_data,
                memory_order_release,
                memory_order_relaxed)) {
            match_data_cache_release(match_data);
        }
        return;
    }
#else
    (void)pattern;
    (void)from_pattern_cache;
#endif
    match_data_cache_release(match_data);
}

static inline pcre2_match_context *
pattern_match_context_acquire(PatternObject *pattern,
                              int use_offset_limit,
                              int *from_pattern_cache)
{
    *from_pattern_cache = 0;
#if defined(PCRE_EXT_HAVE_ATOMICS)
    pcre2_match_context *cached = atomic_exchange_explicit(
        &pattern->cached_match_context,
        NULL,
        memory_order_acq_rel
    );
    if (cached != NULL) {
        *from_pattern_cache = 1;
        return cached;
    }
#else
    (void)pattern;
#endif
    return match_context_cache_acquire(use_offset_limit);
}

static inline void
pattern_match_context_release(PatternObject *pattern,
                              pcre2_match_context *context,
                              int had_offset_limit,
                              int from_pattern_cache)
{
    if (context == NULL) {
        return;
    }
#if defined(PCRE_EXT_HAVE_ATOMICS)
    if (from_pattern_cache) {
        pcre2_jit_stack_assign(context, NULL, NULL);
#if defined(PCRE2_USE_OFFSET_LIMIT)
        if (had_offset_limit) {
            (void)pcre2_set_offset_limit(context, PCRE2_UNSET);
        }
#else
        (void)had_offset_limit;
#endif
        pcre2_match_context *expected = NULL;
        if (!atomic_compare_exchange_strong_explicit(
                &pattern->cached_match_context,
                &expected,
                context,
                memory_order_release,
                memory_order_relaxed)) {
            match_context_cache_release(context, 0);
        }
        return;
    }
#else
    (void)pattern;
#endif
    match_context_cache_release(context, had_offset_limit);
}

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

static int
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

static void
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

static PyObject *
module_clear_pattern_cache(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    if (pattern_cache_is_global()) {
        if (global_pattern_cache_lock != NULL) {
            PyThread_acquire_lock(global_pattern_cache_lock, 1);
        }
        pattern_cache_state_clear(&global_pattern_cache);
        if (global_pattern_cache_lock != NULL) {
            PyThread_release_lock(global_pattern_cache_lock);
        }
    } else if (atomic_load_explicit(&pattern_cache_tss_ready, memory_order_acquire)) {
        PatternCacheState *state = thread_pattern_cache_state_get();
        if (state != NULL) {
            pattern_cache_state_clear(state);
        }
    }

    Py_RETURN_NONE;
}

static inline int
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

static inline void
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

static inline int
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

static inline void
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

static inline int
offset_limit_option_enabled(void)
{
#if defined(PCRE2_USE_OFFSET_LIMIT)
    return offset_limit_support == 1;
#else
    return 0;
#endif
}


/* Match type */
static void
Match_dealloc(MatchObject *self)
{
    Py_XDECREF(self->pattern);
    Py_XDECREF(self->subject);
    Py_XDECREF(self->utf8_owner);
    pcre_free(self->ovector);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
Match_repr(MatchObject *self)
{
    Py_ssize_t start = self->ovector[0];
    Py_ssize_t end = self->ovector[1];
    return PyUnicode_FromFormat("<Match span=(%zd, %zd) pattern=%R>", start, end, self->pattern->pattern);
}

static int
resolve_group_key(MatchObject *self, PyObject *key, Py_ssize_t *index)
{
    if (key == NULL) {
        *index = 0;
        return 0;
    }
    if (PyLong_Check(key)) {
        Py_ssize_t value = PyLong_AsSsize_t(key);
        if (value == -1 && PyErr_Occurred()) {
            return -1;
        }
        *index = value;
        return 0;
    }
    if (PyUnicode_Check(key)) {
        PyObject *item = PyDict_GetItemWithError(self->pattern->groupindex, key);
        if (item == NULL) {
            if (!PyErr_Occurred()) {
                PyErr_Format(PyExc_IndexError, "no such group '%U'", key);
            }
            return -1;
        }
        Py_ssize_t value = PyLong_AsSsize_t(item);
        if (value == -1 && PyErr_Occurred()) {
            return -1;
        }
        *index = value;
        return 0;
    }
    PyErr_SetString(PyExc_TypeError, "group indices must be integers or strings");
    return -1;
}

static PyObject *
match_get_group_value(MatchObject *self, Py_ssize_t index)
{
    if (index < 0 || (uint32_t)index >= self->ovec_count) {
        PyErr_SetString(PyExc_IndexError, "group index out of range");
        return NULL;
    }
    Py_ssize_t start = self->ovector[index * 2];
    Py_ssize_t end = self->ovector[index * 2 + 1];
    if (start < 0 || end < 0) {
        Py_RETURN_NONE;
    }

    const char *data = self->utf8_data;
    Py_ssize_t length = end - start;
    if (self->subject_is_bytes) {
        return PyBytes_FromStringAndSize(data + start, length);
    }
    return PyUnicode_DecodeUTF8(data + start, length, "strict");
}

static PyObject *
Match_group(MatchObject *self, PyObject *args)
{
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    if (nargs == 0) {
        return match_get_group_value(self, 0);
    }
    if (nargs == 1) {
        PyObject *key = PyTuple_GET_ITEM(args, 0);
        Py_ssize_t index = 0;
        if (resolve_group_key(self, key, &index) < 0) {
            return NULL;
        }
        return match_get_group_value(self, index);
    }
    PyObject *result = PyTuple_New(nargs);
    if (result == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < nargs; ++i) {
        PyObject *key = PyTuple_GET_ITEM(args, i);
        Py_ssize_t index = 0;
        if (resolve_group_key(self, key, &index) < 0) {
            Py_DECREF(result);
            return NULL;
        }
        PyObject *value = match_get_group_value(self, index);
        if (value == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyTuple_SET_ITEM(result, i, value);
    }
    return result;
}

static PyObject *
Match_groups(MatchObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"default", NULL};
    PyObject *default_value = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &default_value)) {
        return NULL;
    }

    PyObject *result = PyTuple_New(self->ovec_count - 1);
    if (result == NULL) {
        return NULL;
    }

    for (uint32_t i = 1; i < self->ovec_count; ++i) {
        PyObject *value = match_get_group_value(self, (Py_ssize_t)i);
        if (value == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        if (value == Py_None && default_value != Py_None) {
            Py_DECREF(value);
            Py_INCREF(default_value);
            value = default_value;
        }
        PyTuple_SET_ITEM(result, i - 1, value);
    }

    return result;
}

static PyObject *
Match_span(MatchObject *self, PyObject *args)
{
    PyObject *key = NULL;
    if (!PyArg_ParseTuple(args, "|O", &key)) {
        return NULL;
    }
    Py_ssize_t index = 0;
    if (resolve_group_key(self, key, &index) < 0) {
        return NULL;
    }
    if (index < 0 || (uint32_t)index >= self->ovec_count) {
        PyErr_SetString(PyExc_IndexError, "group index out of range");
        return NULL;
    }
    Py_ssize_t start = self->ovector[index * 2];
    Py_ssize_t end = self->ovector[index * 2 + 1];
    if (start < 0 || end < 0) {
        Py_RETURN_NONE;
    }
    if (self->subject_is_bytes) {
        return Py_BuildValue("(nn)", start, end);
    }
    const char *data = self->utf8_data;
    Py_ssize_t start_index = utf8_offset_to_index(data, start);
    if (start_index < 0 && PyErr_Occurred()) {
        return NULL;
    }
    Py_ssize_t end_index = utf8_offset_to_index(data, end);
    if (end_index < 0 && PyErr_Occurred()) {
        return NULL;
    }
    return Py_BuildValue("(nn)", start_index, end_index);
}

static PyObject *
Match_start(MatchObject *self, PyObject *args)
{
    PyObject *span = Match_span(self, args);
    if (span == NULL || span == Py_None) {
        return span;
    }
    PyObject *value = PyTuple_GET_ITEM(span, 0);
    Py_INCREF(value);
    Py_DECREF(span);
    return value;
}

static PyObject *
Match_end(MatchObject *self, PyObject *args)
{
    PyObject *span = Match_span(self, args);
    if (span == NULL || span == Py_None) {
        return span;
    }
    PyObject *value = PyTuple_GET_ITEM(span, 1);
    Py_INCREF(value);
    Py_DECREF(span);
    return value;
}

static PyObject *
Match_groupdict(MatchObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"default", NULL};
    PyObject *default_value = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &default_value)) {
        return NULL;
    }

    PyObject *result = PyDict_New();
    if (result == NULL) {
        return NULL;
    }

    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(self->pattern->groupindex, &pos, &key, &value)) {
        Py_ssize_t index = PyLong_AsSsize_t(value);
        if (index == -1 && PyErr_Occurred()) {
            Py_DECREF(result);
            return NULL;
        }
        PyObject *group_args = Py_BuildValue("(n)", index);
        if (group_args == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyObject *group_value = Match_group(self, group_args);
        Py_DECREF(group_args);
        if (group_value == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        if (group_value == Py_None && default_value != Py_None) {
            Py_DECREF(group_value);
            Py_INCREF(default_value);
            group_value = default_value;
        }
        if (PyDict_SetItem(result, key, group_value) < 0) {
            Py_DECREF(group_value);
            Py_DECREF(result);
            return NULL;
        }
        Py_DECREF(group_value);
    }

    return result;
}

static PyObject *
Match_get_string(MatchObject *self, void *closure)
{
    Py_INCREF(self->subject);
    return self->subject;
}

static PyMethodDef Match_methods[] = {
    {"group", (PyCFunction)Match_group, METH_VARARGS, PyDoc_STR("Return one or more capture groups.")},
    {"groups", (PyCFunction)Match_groups, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return all capture groups as a tuple." )},
    {"groupdict", (PyCFunction)Match_groupdict, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return a dict for named capture groups." )},
    {"span", (PyCFunction)Match_span, METH_VARARGS, PyDoc_STR("Return the (start, end) span for a group." )},
    {"start", (PyCFunction)Match_start, METH_VARARGS, PyDoc_STR("Return the start index for a group." )},
    {"end", (PyCFunction)Match_end, METH_VARARGS, PyDoc_STR("Return the end index for a group." )},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Match_getset[] = {
    {"string", (getter)Match_get_string, NULL, PyDoc_STR("Original subject."), NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

PyTypeObject MatchType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pcre.Match",
    .tp_basicsize = sizeof(MatchObject),
    .tp_dealloc = (destructor)Match_dealloc,
    .tp_repr = (reprfunc)Match_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = Match_methods,
    .tp_getset = Match_getset,
    .tp_doc = "Match object returned by PCRE2 operations.",
};

typedef struct {
    PyObject_HEAD
    PatternObject *pattern;
    PyObject *subject;
    PyObject *utf8_owner;
    int subject_is_bytes;
    Py_ssize_t subject_length_bytes;
    Py_ssize_t logical_length;
    Py_ssize_t current_pos;
    Py_ssize_t current_byte;
    Py_ssize_t resolved_end;
    Py_ssize_t resolved_end_byte;
    int has_endpos;
    uint32_t base_options;
    int exhausted;
    pcre2_match_data *match_data;
    pcre2_match_context *match_context;
    pcre2_jit_stack *jit_stack;
    const char *utf8_data;
    Py_ssize_t byte_to_index_cached_byte;
    Py_ssize_t byte_to_index_cached_index;
    Py_ssize_t index_to_byte_cached_index;
    Py_ssize_t index_to_byte_cached_byte;
    int utf8_is_ascii;
} FindIterObject;

static MatchObject *create_match_object(PatternObject *pattern,
                                        PyObject *subject_obj,
                                        PyObject *utf8_owner,
                                        const char *utf8_data,
                                        Py_ssize_t utf8_length,
                                        uint32_t ovec_count,
                                        PCRE2_SIZE *ovector);

static inline Py_ssize_t
ascii_prefix_length_scalar(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    while (offset < max_len) {
        if ((data[offset] & 0x80) != 0) {
            break;
        }
        offset += 1;
    }
    return offset;
}

#if defined(__x86_64__) && defined(__GNUC__)

static inline int
ascii_vector_mode(void)
{
    static ATOMIC_VAR(int) cached = ATOMIC_VAR_INIT(-1);
    int value = atomic_load_explicit(&cached, memory_order_acquire);
    if (value != -1) {
        return value;
    }

    int detected = 0;
#if defined(__has_builtin)
#  if __has_builtin(__builtin_cpu_supports)
#    define PCRE_HAVE_CPU_SUPPORTS 1
#  endif
#elif defined(__GNUC__)
#  if (__GNUC__ > 4)
#    define PCRE_HAVE_CPU_SUPPORTS 1
#  endif
#endif
#if defined(PCRE_HAVE_CPU_SUPPORTS)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx512bw")) {
        detected = 3;
    } else if (__builtin_cpu_supports("avx2")) {
        detected = 2;
    } else if (__builtin_cpu_supports("sse2")) {
        detected = 1;
    }
#endif

    int expected = -1;
    if (!atomic_compare_exchange_strong_explicit(
            &cached,
            &expected,
            detected,
            memory_order_acq_rel,
            memory_order_acquire)) {
        detected = atomic_load_explicit(&cached, memory_order_acquire);
    }

    return detected;
}

#if defined(__GNUC__)
__attribute__((target("avx512bw")))
static Py_ssize_t
ascii_prefix_length_avx512(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    const Py_ssize_t step = 64;
    while (offset + step <= max_len) {
        __m512i chunk = _mm512_loadu_si512((const void *)(data + offset));
        __mmask64 mask = _mm512_movepi8_mask(chunk);
        if (mask != 0) {
            unsigned long idx = __builtin_ctzll(mask);
            return offset + (Py_ssize_t)idx;
        }
        offset += step;
    }
    return offset + ascii_prefix_length_scalar(data + offset, max_len - offset);
}

__attribute__((target("avx2")))
static Py_ssize_t
ascii_prefix_length_avx2(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    const Py_ssize_t step = 32;
    while (offset + step <= max_len) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)(data + offset));
        unsigned int mask = (unsigned int)_mm256_movemask_epi8(chunk);
        if (mask != 0) {
            unsigned int idx = __builtin_ctz(mask);
            return offset + (Py_ssize_t)idx;
        }
        offset += step;
    }
    return offset + ascii_prefix_length_scalar(data + offset, max_len - offset);
}

__attribute__((target("sse2")))
static Py_ssize_t
ascii_prefix_length_sse2(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    const Py_ssize_t step = 16;
    while (offset + step <= max_len) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(data + offset));
        unsigned int mask = (unsigned int)_mm_movemask_epi8(chunk);
        if (mask != 0) {
            unsigned int idx = __builtin_ctz(mask);
            return offset + (Py_ssize_t)idx;
        }
        offset += step;
    }
    return offset + ascii_prefix_length_scalar(data + offset, max_len - offset);
}
#endif

#elif defined(_M_X64) || defined(_M_AMD64)

static inline int
ascii_vector_mode(void)
{
    static ATOMIC_VAR(int) cached = ATOMIC_VAR_INIT(-1);
    int value = atomic_load_explicit(&cached, memory_order_acquire);
    if (value != -1) {
        return value;
    }

    int detected = 0;
    int cpu_info[4] = {0};

    __cpuid(cpu_info, 0);
    int max_leaf = cpu_info[0];

    if (max_leaf >= 1) {
        __cpuid(cpu_info, 1);

        const int has_sse2 = (cpu_info[3] & (1 << 26)) != 0;
        if (has_sse2) {
            detected = 1;
        }

        const int has_osxsave = (cpu_info[2] & (1 << 27)) != 0;
        const int has_avx = (cpu_info[2] & (1 << 28)) != 0;

        unsigned long long xcr0 = 0;
        if (has_osxsave) {
            xcr0 = _xgetbv(0);
        }

        const unsigned long long xcr0_avx_mask = (1ull << 1) | (1ull << 2);

        if (has_osxsave && has_avx && (xcr0 & xcr0_avx_mask) == xcr0_avx_mask && max_leaf >= 7) {
            __cpuidex(cpu_info, 7, 0);

            const int has_avx2 = (cpu_info[1] & (1 << 5)) != 0;
            if (has_avx2) {
                detected = 2;
            }

            const int has_avx512f = (cpu_info[1] & (1 << 16)) != 0;
            const int has_avx512bw = (cpu_info[1] & (1 << 30)) != 0;
            const unsigned long long xcr0_avx512_mask = (1ull << 1) | (1ull << 2) | (1ull << 5) | (1ull << 6) | (1ull << 7);
            if (has_avx512f && has_avx512bw && (xcr0 & xcr0_avx512_mask) == xcr0_avx512_mask) {
                detected = 3;
            }
        }
    }

    int expected = -1;
    if (!atomic_compare_exchange_strong_explicit(
            &cached,
            &expected,
            detected,
            memory_order_acq_rel,
            memory_order_acquire)) {
        detected = atomic_load_explicit(&cached, memory_order_acquire);
    }

    return detected;
}

#endif

static inline Py_ssize_t
ascii_prefix_length(const char *data, Py_ssize_t max_len)
{
#if defined(__x86_64__) && defined(__GNUC__)
    switch (ascii_vector_mode()) {
        case 3:
            return ascii_prefix_length_avx512(data, max_len);
        case 2:
            return ascii_prefix_length_avx2(data, max_len);
        case 1:
            return ascii_prefix_length_sse2(data, max_len);
        default:
            break;
    }
#endif
    return ascii_prefix_length_scalar(data, max_len);
}

static inline Py_ssize_t
utf8_index_to_offset_fast(const char *data, Py_ssize_t data_len, Py_ssize_t index)
{
    if (index <= 0) {
        return 0;
    }

    Py_ssize_t offset = 0;
    while (index > 0 && offset < data_len) {
        Py_ssize_t remaining_bytes = data_len - offset;
        Py_ssize_t ascii_run = ascii_prefix_length(data + offset, remaining_bytes);
        if (ascii_run > 0) {
            if (ascii_run > index) {
                ascii_run = index;
            }
            offset += ascii_run;
            index -= ascii_run;
            continue;
        }

        unsigned char lead = (unsigned char)data[offset];
        Py_ssize_t char_bytes = 1;
        if ((lead & 0xE0) == 0xC0) {
            char_bytes = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            char_bytes = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            char_bytes = 4;
        }

        if (char_bytes > remaining_bytes) {
            char_bytes = remaining_bytes;
        }

        offset += char_bytes;
        index -= 1;
    }

    if (offset > data_len) {
        offset = data_len;
    }
    return offset;
}

static int
ensure_valid_utf8_for_bytes_subject(PatternObject *pattern,
                                    int subject_is_bytes,
                                    const char *data,
                                    Py_ssize_t length)
{
    if (!subject_is_bytes) {
        return 0;
    }

    if ((pattern->compile_options & PCRE2_UTF) == 0) {
        return 0;
    }

    PyObject *utf8_check = PyUnicode_DecodeUTF8(data, length, NULL);
    if (utf8_check == NULL) {
        PyErr_Clear();
        PyErr_SetString(PcreError,
                        "bytes subject must be valid UTF-8 when pattern uses the UTF flag");
        return -1;
    }
    Py_DECREF(utf8_check);
    return 0;
}

static Py_ssize_t
finditer_byte_to_index(FindIterObject *self, Py_ssize_t target_byte)
{
    if (target_byte < 0) {
        self->byte_to_index_cached_index = 0;
        self->byte_to_index_cached_byte = 0;
        return 0;
    }

    if (target_byte > self->subject_length_bytes) {
        target_byte = self->subject_length_bytes;
    }

    if (self->subject_is_bytes || self->utf8_is_ascii) {
        self->byte_to_index_cached_index = target_byte;
        self->byte_to_index_cached_byte = target_byte;
        return target_byte;
    }

    if (target_byte <= self->byte_to_index_cached_byte) {
        self->byte_to_index_cached_index = 0;
        self->byte_to_index_cached_byte = 0;
    }

    Py_ssize_t index = self->byte_to_index_cached_index;
    Py_ssize_t byte_offset = self->byte_to_index_cached_byte;
    const char *ptr = self->utf8_data + byte_offset;

    while (byte_offset < target_byte) {
        Py_ssize_t remaining = target_byte - byte_offset;
        unsigned char lead = (unsigned char)*ptr;

        if (lead < 0x80) {
            Py_ssize_t ascii_run = ascii_prefix_length(ptr, remaining);
            if (ascii_run > 0) {
                byte_offset += ascii_run;
                index += ascii_run;
                ptr += ascii_run;
                continue;
            }
        }

        Py_ssize_t char_bytes = 1;
        if ((lead & 0xE0) == 0xC0) {
            char_bytes = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            char_bytes = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            char_bytes = 4;
        }

        if (byte_offset + char_bytes > target_byte) {
            byte_offset = target_byte;
            break;
        }

        ptr += char_bytes;
        byte_offset += char_bytes;
        index += 1;
    }

    self->byte_to_index_cached_byte = byte_offset;
    if (byte_offset == self->subject_length_bytes) {
        self->byte_to_index_cached_index = self->logical_length;
        return self->logical_length;
    }

    self->byte_to_index_cached_index = index;
    return index;
}

static Py_ssize_t
finditer_index_to_byte(FindIterObject *self, Py_ssize_t target_index)
{
    if (target_index < 0) {
        self->index_to_byte_cached_index = 0;
        self->index_to_byte_cached_byte = 0;
        return 0;
    }

    if (target_index > self->logical_length) {
        target_index = self->logical_length;
    }

    if (self->subject_is_bytes || self->utf8_is_ascii) {
        self->index_to_byte_cached_index = target_index;
        self->index_to_byte_cached_byte = target_index;
        return target_index;
    }

    if (target_index <= self->index_to_byte_cached_index) {
        self->index_to_byte_cached_index = 0;
        self->index_to_byte_cached_byte = 0;
    }

    Py_ssize_t index = self->index_to_byte_cached_index;
    Py_ssize_t byte_offset = self->index_to_byte_cached_byte;
    const char *ptr = self->utf8_data + byte_offset;

    while (index < target_index) {
        Py_ssize_t remaining_chars = target_index - index;
        Py_ssize_t remaining_bytes = self->subject_length_bytes - byte_offset;
        if (remaining_bytes <= 0) {
            break;
        }

        unsigned char lead = (unsigned char)*ptr;

        if (lead < 0x80) {
            Py_ssize_t ascii_run = ascii_prefix_length(ptr, remaining_bytes);
            if (ascii_run > 0) {
                if (ascii_run >= remaining_chars) {
                    byte_offset += remaining_chars;
                    index += remaining_chars;
                    ptr += remaining_chars;
                    break;
                }
                byte_offset += ascii_run;
                index += ascii_run;
                ptr += ascii_run;
                continue;
            }
        }

        Py_ssize_t char_bytes = 1;
        if ((lead & 0xE0) == 0xC0) {
            char_bytes = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            char_bytes = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            char_bytes = 4;
        }

        if (remaining_bytes < char_bytes) {
            byte_offset += remaining_bytes;
            break;
        }

        ptr += char_bytes;
        byte_offset += char_bytes;
        index += 1;
    }

    self->index_to_byte_cached_index = index;
    self->index_to_byte_cached_byte = byte_offset;
    return byte_offset;
}

static void
FindIter_dealloc(FindIterObject *self)
{
    if (self->match_data != NULL) {
        match_data_cache_release(self->match_data);
        self->match_data = NULL;
    }
    if (self->match_context != NULL) {
        pcre2_match_context_free(self->match_context);
        self->match_context = NULL;
    }
    if (self->jit_stack != NULL) {
        jit_stack_cache_release(self->jit_stack);
        self->jit_stack = NULL;
    }
    Py_XDECREF(self->pattern);
    Py_XDECREF(self->subject);
    Py_XDECREF(self->utf8_owner);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
FindIter_iter(PyObject *self)
{
    Py_INCREF(self);
    return self;
}

static PyObject *
FindIter_iternext(FindIterObject *self)
{
    if (self->exhausted) {
        return NULL;
    }

    if (self->current_pos > self->logical_length) {
        self->exhausted = 1;
        return NULL;
    }

    if (self->has_endpos && self->current_pos >= self->resolved_end) {
        self->exhausted = 1;
        return NULL;
    }

    if (self->current_byte > self->subject_length_bytes) {
        self->exhausted = 1;
        return NULL;
    }

    const char *buffer = self->utf8_data;
    uint32_t options = self->base_options;
    int rc = 0;
    PCRE2_SIZE exec_length = (PCRE2_SIZE)self->subject_length_bytes;
    uint32_t available_pairs = 0;
    PCRE2_SIZE *ovector = NULL;
    uint64_t expected_pairs = 0;

    if (self->has_endpos && self->resolved_end_byte < self->subject_length_bytes && !offset_limit_option_enabled()) {
        exec_length = (PCRE2_SIZE)self->resolved_end_byte;
        if (exec_length < (PCRE2_SIZE)self->current_byte) {
            exec_length = (PCRE2_SIZE)self->current_byte;
        }
    }

    if (pattern_jit_get(self->pattern)) {
        if (self->match_context == NULL) {
            self->match_context = pcre2_match_context_create(NULL);
            if (self->match_context == NULL) {
                PyErr_NoMemory();
                return NULL;
            }
            if (self->jit_stack == NULL) {
                self->jit_stack = jit_stack_cache_acquire();
                if (self->jit_stack == NULL) {
                    PyErr_NoMemory();
                    return NULL;
                }
            }
            pcre2_jit_stack_assign(self->match_context, NULL, self->jit_stack);
        }
        PCRE2_CALL_WITHOUT_GIL(pcre2_jit_match(self->pattern->code,
                                               (PCRE2_SPTR)buffer,
                                               exec_length,
                                               (PCRE2_SIZE)self->current_byte,
                                               options,
                                               self->match_data,
                                               self->match_context));

        if (rc == PCRE2_ERROR_JIT_BADOPTION || rc == PCRE2_ERROR_BADOPTION) {
            pattern_jit_set(self->pattern, 0);
            if (self->jit_stack != NULL) {
                if (self->match_context != NULL) {
                    pcre2_jit_stack_assign(self->match_context, NULL, NULL);
                }
                jit_stack_cache_release(self->jit_stack);
                self->jit_stack = NULL;
            }
        } else if (rc != PCRE2_ERROR_NOMATCH && rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(self->match_data);
            raise_pcre_error("jit_match", rc, error_offset);
            return NULL;
        } else if (rc >= 0) {
            goto matched;
        }
    }

    PCRE2_CALL_WITHOUT_GIL(pcre2_match(self->pattern->code,
                                       (PCRE2_SPTR)buffer,
                                       exec_length,
                                       (PCRE2_SIZE)self->current_byte,
                                       options,
                                       self->match_data,
                                       self->match_context));

    if (rc == PCRE2_ERROR_NOMATCH) {
        self->exhausted = 1;
        return NULL;
    }

    if (rc < 0) {
        PCRE2_SIZE error_offset = pcre2_get_startchar(self->match_data);
        raise_pcre_error("match", rc, error_offset);
        return NULL;
    }

matched:
    available_pairs = pcre2_get_ovector_count(self->match_data);
    ovector = pcre2_get_ovector_pointer(self->match_data);
    if (ovector == NULL || available_pairs == 0) {
        PyErr_SetString(PyExc_RuntimeError, "PCRE2 returned empty match data");
        return NULL;
    }

    expected_pairs = (uint64_t)self->pattern->capture_count + 1;
    if (expected_pairs == 0 || expected_pairs > available_pairs) {
        expected_pairs = available_pairs;
    }

    Py_ssize_t start_byte = (Py_ssize_t)ovector[0];
    Py_ssize_t end_byte = (Py_ssize_t)ovector[1];

    Py_ssize_t start_index = finditer_byte_to_index(self, start_byte);
    Py_ssize_t end_index = finditer_byte_to_index(self, end_byte);

    MatchObject *match = create_match_object(
        self->pattern,
        self->subject,
        self->utf8_owner,
        self->utf8_data,
        self->subject_length_bytes,
        (uint32_t)expected_pairs,
        ovector);
    if (match == NULL) {
        return NULL;
    }

    Py_ssize_t next_pos = end_index;
    if (self->has_endpos && end_index >= self->resolved_end) {
        next_pos = end_index;
    } else if (end_index == start_index) {
        next_pos = end_index + 1;
    }

    if (next_pos <= self->current_pos) {
        next_pos = self->current_pos + 1;
    }

    self->current_pos = next_pos;

    if (self->subject_is_bytes) {
        if (self->current_pos <= self->logical_length) {
            if (self->current_pos < 0) {
                self->current_pos = 0;
            }
            self->current_byte = self->current_pos;
        } else {
            self->current_byte = self->subject_length_bytes;
        }
        self->byte_to_index_cached_index = self->current_pos;
        self->byte_to_index_cached_byte = self->current_byte;
        self->index_to_byte_cached_index = self->current_pos;
        self->index_to_byte_cached_byte = self->current_byte;
    } else {
        if (self->current_pos <= self->logical_length) {
            Py_ssize_t next_byte = finditer_index_to_byte(self, self->current_pos);
            self->current_byte = next_byte;
            self->byte_to_index_cached_index = self->current_pos;
            self->byte_to_index_cached_byte = self->current_byte;
        } else {
            self->current_byte = self->subject_length_bytes;
            self->byte_to_index_cached_index = self->logical_length;
            self->byte_to_index_cached_byte = self->subject_length_bytes;
            self->index_to_byte_cached_index = self->logical_length;
            self->index_to_byte_cached_byte = self->subject_length_bytes;
        }
    }

    return (PyObject *)match;
}

static PyTypeObject FindIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pcre._FindIter",
    .tp_basicsize = sizeof(FindIterObject),
    .tp_dealloc = (destructor)FindIter_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_iter = FindIter_iter,
    .tp_iternext = (iternextfunc)FindIter_iternext,
    .tp_doc = "Iterator yielding successive PCRE2 matches.",
};

/* Pattern helpers */
static MatchObject *
create_match_object(PatternObject *pattern,
                    PyObject *subject_obj,
                    PyObject *utf8_owner,
                    const char *utf8_data,
                    Py_ssize_t utf8_length,
                    uint32_t ovec_count,
                    PCRE2_SIZE *ovector)
{
    MatchObject *match = PyObject_New(MatchObject, &MatchType);
    if (match == NULL) {
        return NULL;
    }

    match->ovector = pcre_malloc(sizeof(Py_ssize_t) * ovec_count * 2);
    if (match->ovector == NULL) {
        PyErr_NoMemory();
        PyObject_Del(match);
        return NULL;
    }

    for (uint32_t i = 0; i < ovec_count * 2; ++i) {
        match->ovector[i] = (Py_ssize_t)ovector[i];
    }
    match->ovec_count = ovec_count;

    Py_INCREF(pattern);
    match->pattern = pattern;

    Py_INCREF(subject_obj);
    match->subject = subject_obj;

    Py_INCREF(utf8_owner);
    match->utf8_owner = utf8_owner;
    match->utf8_data = utf8_data;
    match->utf8_length = utf8_length;
    match->subject_is_bytes = PyBytes_Check(subject_obj);

    return match;
}

static PyObject *
Pattern_create_finditer(PatternObject *pattern,
                        PyObject *subject_obj,
                        Py_ssize_t pos,
                        Py_ssize_t endpos,
                        uint32_t options)
{
    FindIterObject *iter = PyObject_New(FindIterObject, &FindIterType);
    if (iter == NULL) {
        return NULL;
    }

    pcre2_match_context *match_context = NULL;
    pcre2_jit_stack *jit_stack = NULL;

    iter->pattern = NULL;
    iter->subject = NULL;
    iter->subject_is_bytes = 0;
    iter->subject_length_bytes = 0;
    iter->logical_length = 0;
    iter->current_pos = 0;
    iter->current_byte = 0;
    iter->resolved_end = 0;
    iter->resolved_end_byte = 0;
    iter->has_endpos = 0;
    iter->base_options = options;
    iter->exhausted = 0;
    iter->match_data = NULL;
    iter->match_context = NULL;
    iter->jit_stack = NULL;
    iter->utf8_owner = NULL;
    iter->utf8_data = NULL;
    iter->byte_to_index_cached_byte = 0;
    iter->byte_to_index_cached_index = 0;
    iter->index_to_byte_cached_index = 0;
    iter->index_to_byte_cached_byte = 0;
    iter->utf8_is_ascii = 0;

    Py_INCREF(pattern);
    iter->pattern = pattern;

    Py_INCREF(subject_obj);
    iter->subject = subject_obj;

    if (PyBytes_Check(subject_obj)) {
        iter->subject_is_bytes = 1;
        iter->subject_length_bytes = PyBytes_GET_SIZE(subject_obj);
        iter->logical_length = iter->subject_length_bytes;
        iter->utf8_data = PyBytes_AS_STRING(subject_obj);
        Py_INCREF(subject_obj);
        iter->utf8_owner = subject_obj;
        if (ensure_valid_utf8_for_bytes_subject(pattern,
                                                iter->subject_is_bytes,
                                                iter->utf8_data,
                                                iter->subject_length_bytes) < 0) {
            goto error;
        }
    } else if (PyUnicode_Check(subject_obj)) {
        if (PyUnicode_READY(subject_obj) < 0) {
            goto error;
        }
        Py_INCREF(subject_obj);
        iter->subject_is_bytes = 0;
        iter->logical_length = PyUnicode_GET_LENGTH(subject_obj);
        iter->utf8_owner = subject_obj;
        if (PyUnicode_IS_ASCII(subject_obj)) {
            iter->subject_length_bytes = iter->logical_length;
            iter->utf8_data = (const char *)PyUnicode_1BYTE_DATA(subject_obj);
            iter->utf8_is_ascii = 1;
        } else {
            Py_ssize_t utf8_length = 0;
            const char *utf8_data = PyUnicode_AsUTF8AndSize(subject_obj, &utf8_length);
            if (utf8_data == NULL) {
                goto error;
            }
            iter->subject_length_bytes = utf8_length;
            iter->utf8_data = utf8_data;
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "subject must be str or bytes");
        goto error;
    }

    Py_ssize_t logical_length = iter->logical_length;

    if (pos < 0) {
        pos += logical_length;
        if (pos < 0) {
            pos = 0;
        }
    }
    if (pos > logical_length) {
        pos = logical_length;
    }

    Py_ssize_t resolved_end = logical_length;
    Py_ssize_t resolved_end_byte = iter->subject_length_bytes;
    int has_endpos = 0;

    if (endpos >= 0) {
        has_endpos = 1;
        if (endpos > logical_length) {
            endpos = logical_length;
        }
        if (endpos < pos) {
            PyErr_SetString(PyExc_ValueError, "endpos must be >= pos");
            goto error;
        }
        resolved_end = endpos;
    }

    Py_ssize_t current_byte = pos;
    if (iter->subject_is_bytes) {
        resolved_end_byte = resolved_end;
    } else {
        current_byte = pos == 0 ? 0 : utf8_index_to_offset_fast(iter->utf8_data, iter->subject_length_bytes, pos);
        resolved_end_byte = resolved_end == iter->logical_length
            ? iter->subject_length_bytes
            : utf8_index_to_offset_fast(iter->utf8_data, iter->subject_length_bytes, resolved_end);
    }

    iter->current_pos = pos;
    iter->current_byte = current_byte;
    iter->resolved_end = resolved_end;
    iter->resolved_end_byte = resolved_end_byte;
    iter->has_endpos = has_endpos;
    iter->exhausted = (has_endpos && pos >= resolved_end);

    iter->byte_to_index_cached_index = pos;
    iter->byte_to_index_cached_byte = current_byte;
    iter->index_to_byte_cached_index = pos;
    iter->index_to_byte_cached_byte = current_byte;

    iter->match_data = match_data_cache_acquire(pattern);
    if (iter->match_data == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    int pattern_jit_enabled = pattern_jit_get(pattern);
    int need_offset_limit = (has_endpos && resolved_end_byte != iter->subject_length_bytes);
    int use_offset_limit = need_offset_limit && offset_limit_option_enabled();

    if (pattern_jit_enabled || use_offset_limit) {
        match_context = pcre2_match_context_create(NULL);
        if (match_context == NULL) {
            PyErr_NoMemory();
            goto error;
        }
    }

#if defined(PCRE2_USE_OFFSET_LIMIT)
    if (use_offset_limit) {
        int ctx_rc = pcre2_set_offset_limit(match_context, (PCRE2_SIZE)resolved_end_byte);
        if (ctx_rc < 0) {
            pcre2_match_context_free(match_context);
            match_context = NULL;
            raise_pcre_error("set_offset_limit", ctx_rc, 0);
            goto error;
        }
        iter->base_options |= PCRE2_USE_OFFSET_LIMIT;
    }
#endif

    if (pattern_jit_enabled) {
        if (match_context == NULL) {
            match_context = pcre2_match_context_create(NULL);
            if (match_context == NULL) {
                PyErr_NoMemory();
                goto error;
            }
        }
        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            PyErr_NoMemory();
            goto error;
        }
        pcre2_jit_stack_assign(match_context, NULL, jit_stack);
    }

    iter->match_context = match_context;
    iter->jit_stack = jit_stack;
    if (!iter->subject_is_bytes) {
        iter->base_options |= PCRE2_NO_UTF_CHECK;
    }

    return (PyObject *)iter;

error:
    if (jit_stack != NULL) {
        jit_stack_cache_release(jit_stack);
    }
    if (match_context != NULL) {
        pcre2_match_context_free(match_context);
    }
    if (iter->match_data != NULL) {
        match_data_cache_release(iter->match_data);
    }
    if (iter->match_context != NULL) {
        pcre2_match_context_free(iter->match_context);
        iter->match_context = NULL;
    }
    Py_XDECREF(iter->utf8_owner);
    Py_XDECREF(iter->subject);
    Py_XDECREF(iter->pattern);
    PyObject_Del(iter);
    return NULL;
}


typedef enum {
    EXEC_MODE_MATCH,
    EXEC_MODE_SEARCH,
    EXEC_MODE_FULLMATCH
} execute_mode;

static void
Pattern_dealloc(PatternObject *self)
{
#if !defined(PCRE_EXT_HAVE_ATOMICS)
    if (self->jit_lock != NULL) {
        PyThread_free_lock(self->jit_lock);
        self->jit_lock = NULL;
    }
#endif
#if defined(PCRE_EXT_HAVE_ATOMICS)
    pcre2_match_data *cached_match = atomic_exchange_explicit(
        &self->cached_match_data,
        NULL,
        memory_order_acq_rel
    );
    if (cached_match != NULL) {
        pcre2_match_data_free(cached_match);
    }
    pcre2_match_context *cached_context = atomic_exchange_explicit(
        &self->cached_match_context,
        NULL,
        memory_order_acq_rel
    );
    if (cached_context != NULL) {
        pcre2_match_context_free(cached_context);
    }
#else
    if (self->cached_match_data != NULL) {
        pcre2_match_data_free(self->cached_match_data);
        self->cached_match_data = NULL;
    }
    if (self->cached_match_context != NULL) {
        pcre2_match_context_free(self->cached_match_context);
        self->cached_match_context = NULL;
    }
#endif
    if (self->code != NULL) {
        pcre2_code_free(self->code);
        self->code = NULL;
    }
    Py_XDECREF(self->pattern);
    Py_XDECREF(self->pattern_bytes);
    Py_XDECREF(self->groupindex);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
Pattern_repr(PatternObject *self)
{
    return PyUnicode_FromFormat("<Pattern pattern=%R flags=%u>", self->pattern, self->compile_options);
}

static PyObject *
Pattern_get_pattern(PatternObject *self, void *closure)
{
    Py_INCREF(self->pattern);
    return self->pattern;
}

static PyObject *
Pattern_get_pattern_bytes(PatternObject *self, void *closure)
{
    Py_INCREF(self->pattern_bytes);
    return self->pattern_bytes;
}

static PyObject *
Pattern_get_flags(PatternObject *self, void *closure)
{
    return PyLong_FromUnsignedLong(self->compile_options);
}

static PyObject *
Pattern_get_jit(PatternObject *self, void *closure)
{
    if (pattern_jit_get(self)) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject *
Pattern_get_groupindex(PatternObject *self, void *closure)
{
    Py_INCREF(self->groupindex);
    return self->groupindex;
}

static PyObject *
Pattern_get_capture_count(PatternObject *self, void *closure)
{
    return PyLong_FromUnsignedLong((unsigned long)self->capture_count);
}

static PyObject *
Pattern_finditer_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnk", kwlist,
                                     &subject, &pos, &endpos, &options)) {
        return NULL;
    }

    return Pattern_create_finditer(self, subject, pos, endpos, (uint32_t)options);
}

static PyObject *
Pattern_execute(PatternObject *self, PyObject *subject_obj, Py_ssize_t pos,
                Py_ssize_t endpos, uint32_t options, execute_mode mode)
{
    PyObject *utf8_owner = NULL;
    const char *buffer = NULL;
    Py_ssize_t subject_length_bytes = 0;
    Py_ssize_t logical_length = 0;
    int subject_is_bytes = PyBytes_Check(subject_obj);
    int ascii_text = 0;

    if (subject_is_bytes) {
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        buffer = PyBytes_AS_STRING(subject_obj);
        subject_length_bytes = PyBytes_GET_SIZE(subject_obj);
        logical_length = subject_length_bytes;
        if (ensure_valid_utf8_for_bytes_subject(self,
                                                subject_is_bytes,
                                                buffer,
                                                subject_length_bytes) < 0) {
            Py_DECREF(utf8_owner);
            return NULL;
        }
    } else if (PyUnicode_Check(subject_obj)) {
        if (PyUnicode_READY(subject_obj) < 0) {
            return NULL;
        }
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        logical_length = PyUnicode_GET_LENGTH(subject_obj);
        if (PyUnicode_IS_ASCII(subject_obj)) {
            buffer = (const char *)PyUnicode_1BYTE_DATA(subject_obj);
            subject_length_bytes = logical_length;
            ascii_text = 1;
        } else {
            Py_ssize_t utf8_length = 0;
            const char *utf8_data = PyUnicode_AsUTF8AndSize(subject_obj, &utf8_length);
            if (utf8_data == NULL) {
                Py_DECREF(utf8_owner);
                return NULL;
            }
            buffer = utf8_data;
            subject_length_bytes = utf8_length;
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "expected str or bytes");
        return NULL;
    }

    if (pos < 0) {
        pos += logical_length;
        if (pos < 0) {
            pos = 0;
        }
    }
    if (pos > logical_length) {
        Py_DECREF(utf8_owner);
        Py_RETURN_NONE;
    }

    Py_ssize_t adjusted_endpos = endpos;
    if (adjusted_endpos >= 0) {
        if (adjusted_endpos > logical_length) {
            adjusted_endpos = logical_length;
        }
        if (adjusted_endpos < pos) {
            Py_DECREF(utf8_owner);
            PyErr_SetString(PyExc_ValueError, "endpos must be >= pos");
            return NULL;
        }
    }

    int treat_as_bytes = subject_is_bytes || ascii_text;

    Py_ssize_t byte_start = pos;
    Py_ssize_t byte_end = subject_length_bytes;

    if (treat_as_bytes) {
        byte_start = pos;
        if (adjusted_endpos >= 0) {
            byte_end = adjusted_endpos;
        }
    } else {
        if (pos == 0) {
            byte_start = 0;
        } else if (pos == logical_length) {
            byte_start = subject_length_bytes;
        } else if (utf8_index_to_offset(subject_obj, pos, &byte_start) < 0) {
            Py_DECREF(utf8_owner);
            return NULL;
        }

        if (adjusted_endpos >= 0) {
            if (adjusted_endpos == logical_length) {
                byte_end = subject_length_bytes;
            } else if (utf8_index_to_offset(subject_obj, adjusted_endpos, &byte_end) < 0) {
                Py_DECREF(utf8_owner);
                return NULL;
            }
        }
    }

    if (byte_start > byte_end) {
        Py_DECREF(utf8_owner);
        PyErr_SetString(PyExc_ValueError, "byte offset mismatch for subject");
        return NULL;
    }

    if (mode == EXEC_MODE_SEARCH && self->has_first_literal) {
        if (byte_start >= byte_end) {
            Py_DECREF(utf8_owner);
            Py_RETURN_NONE;
        }
        const unsigned char *scan_start = (const unsigned char *)(buffer + byte_start);
        size_t span = (size_t)(byte_end - byte_start);
        if (memchr(scan_start, (unsigned char)self->first_literal, span) == NULL) {
            Py_DECREF(utf8_owner);
            Py_RETURN_NONE;
        }
    }

    if ((mode == EXEC_MODE_MATCH || mode == EXEC_MODE_FULLMATCH) &&
        self->has_first_literal) {
        if (byte_start >= byte_end) {
            Py_DECREF(utf8_owner);
            Py_RETURN_NONE;
        }
        unsigned char leading = (unsigned char)buffer[byte_start];
        if (leading != (unsigned char)self->first_literal) {
            Py_DECREF(utf8_owner);
            Py_RETURN_NONE;
        }
    }

    PCRE2_SIZE offset_limit = (PCRE2_SIZE)byte_end;

    uint32_t match_options = options;
    if (mode == EXEC_MODE_MATCH) {
        match_options |= PCRE2_ANCHORED;
    } else if (mode == EXEC_MODE_FULLMATCH) {
        match_options |= (PCRE2_ANCHORED | PCRE2_ENDANCHORED);
    }
    if (!subject_is_bytes) {
        match_options |= PCRE2_NO_UTF_CHECK;
    }

    int match_data_from_pattern = 0;
    pcre2_match_data *match_data = pattern_match_data_acquire(self, &match_data_from_pattern);
    if (match_data == NULL) {
        Py_DECREF(utf8_owner);
        PyErr_NoMemory();
        return NULL;
    }

    int rc = 0;
    int attempt_jit = pattern_jit_get(self);
    pcre2_match_context *match_context = NULL;
    int match_context_from_pattern = 0;
    int match_context_used_offset_limit = 0;
    pcre2_jit_stack *jit_stack = NULL;
    PCRE2_SIZE exec_length = (PCRE2_SIZE)subject_length_bytes;
    int need_offset_limit = (offset_limit != (PCRE2_SIZE)subject_length_bytes);
#if defined(PCRE2_USE_OFFSET_LIMIT)
    int use_offset_limit_option = need_offset_limit && offset_limit_option_enabled();
#else
    int use_offset_limit_option = 0;
#endif

    if (use_offset_limit_option || attempt_jit) {
        match_context = pattern_match_context_acquire(
            self,
            use_offset_limit_option,
            &match_context_from_pattern
        );
        if (match_context == NULL) {
            pattern_match_data_release(self, match_data, match_data_from_pattern);
            Py_DECREF(utf8_owner);
            PyErr_NoMemory();
            return NULL;
        }
    }

#if defined(PCRE2_USE_OFFSET_LIMIT)
    if (use_offset_limit_option) {
        int ctx_rc = pcre2_set_offset_limit(match_context, offset_limit);
        if (ctx_rc < 0) {
            pattern_match_context_release(
                self,
                match_context,
                /*had_offset_limit=*/0,
                match_context_from_pattern
            );
            pattern_match_data_release(self, match_data, match_data_from_pattern);
            Py_DECREF(utf8_owner);
            raise_pcre_error("set_offset_limit", ctx_rc, 0);
            return NULL;
        }
        match_options |= PCRE2_USE_OFFSET_LIMIT;
        match_context_used_offset_limit = 1;
    } else
#endif
    if (need_offset_limit) {
        exec_length = offset_limit;
        if (exec_length < (PCRE2_SIZE)byte_start) {
            exec_length = (PCRE2_SIZE)byte_start;
        }
    }

    if (attempt_jit) {
        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            if (match_context != NULL) {
                pattern_match_context_release(
                    self,
                    match_context,
                    match_context_used_offset_limit,
                    match_context_from_pattern
                );
            }
            pattern_match_data_release(self, match_data, match_data_from_pattern);
            Py_DECREF(utf8_owner);
            PyErr_NoMemory();
            return NULL;
        }

        pcre2_jit_stack_assign(match_context, NULL, jit_stack);

        PCRE2_CALL_WITHOUT_GIL(pcre2_jit_match(self->code,
                                               (PCRE2_SPTR)buffer,
                                               exec_length,
                                               (PCRE2_SIZE)byte_start,
                                               match_options,
                                               match_data,
                                               match_context));

        pcre2_jit_stack_assign(match_context, NULL, NULL);
        jit_stack_cache_release(jit_stack);
        jit_stack = NULL;

        if (rc == PCRE2_ERROR_JIT_BADOPTION || rc == PCRE2_ERROR_BADOPTION) {
            pattern_jit_set(self, 0);
        } else if (rc != PCRE2_ERROR_NOMATCH && rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
            pattern_match_context_release(
                self,
                match_context,
                match_context_used_offset_limit,
                match_context_from_pattern
            );
            pattern_match_data_release(self, match_data, match_data_from_pattern);
            Py_DECREF(utf8_owner);
            raise_pcre_error("jit_match", rc, error_offset);
            return NULL;
        }
    }

    if (!pattern_jit_get(self)) {
        PCRE2_CALL_WITHOUT_GIL(pcre2_match(self->code,
                                           (PCRE2_SPTR)buffer,
                                           exec_length,
                                           (PCRE2_SIZE)byte_start,
                                           match_options,
                                           match_data,
                                           match_context));
    }

    if (rc == PCRE2_ERROR_NOMATCH) {
        pattern_match_context_release(
            self,
            match_context,
            match_context_used_offset_limit,
            match_context_from_pattern
        );
        pattern_match_data_release(self, match_data, match_data_from_pattern);
        Py_DECREF(utf8_owner);
        Py_RETURN_NONE;
    }

    if (rc < 0) {
        PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
        pattern_match_context_release(
            self,
            match_context,
            match_context_used_offset_limit,
            match_context_from_pattern
        );
        pattern_match_data_release(self, match_data, match_data_from_pattern);
        Py_DECREF(utf8_owner);
        raise_pcre_error("match", rc, error_offset);
        return NULL;
    }

    uint32_t available_ovector_pairs = pcre2_get_ovector_count(match_data);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    if (ovector == NULL || available_ovector_pairs == 0) {
        pattern_match_context_release(
            self,
            match_context,
            match_context_used_offset_limit,
            match_context_from_pattern
        );
        pattern_match_data_release(self, match_data, match_data_from_pattern);
        Py_DECREF(utf8_owner);
        PyErr_SetString(PyExc_RuntimeError, "PCRE2 returned empty match data");
        return NULL;
    }

    uint64_t expected_pairs = (uint64_t)self->capture_count + 1;
    if (expected_pairs == 0 || expected_pairs > available_ovector_pairs) {
        expected_pairs = available_ovector_pairs;
    }

    MatchObject *match = create_match_object(
        self,
        subject_obj,
        utf8_owner,
        buffer,
        subject_length_bytes,
        (uint32_t)expected_pairs,
        ovector);

    pattern_match_context_release(
        self,
        match_context,
        match_context_used_offset_limit,
        match_context_from_pattern
    );
    pattern_match_data_release(self, match_data, match_data_from_pattern);

    if (match == NULL) {
        Py_DECREF(utf8_owner);
        return NULL;
    }

    Py_DECREF(utf8_owner);
    return (PyObject *)match;
}

static PyObject *
Pattern_match_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnk", kwlist,
                                     &subject, &pos, &endpos, &options)) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, (uint32_t)options, EXEC_MODE_MATCH);
}

static PyObject *
Pattern_search_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnk", kwlist,
                                     &subject, &pos, &endpos, &options)) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, (uint32_t)options, EXEC_MODE_SEARCH);
}

static PyObject *
Pattern_fullmatch_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnk", kwlist,
                                     &subject, &pos, &endpos, &options)) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, (uint32_t)options, EXEC_MODE_FULLMATCH);
}

static PyMethodDef Pattern_methods[] = {
    {"finditer", (PyCFunction)Pattern_finditer_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return an iterator over successive matches.")},
    {"match", (PyCFunction)Pattern_match_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Match the pattern at the start of the subject.")},
    {"search", (PyCFunction)Pattern_search_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Search the subject for the pattern." )},
    {"fullmatch", (PyCFunction)Pattern_fullmatch_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Require the pattern to match the entire subject." )},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Pattern_getset[] = {
    {"pattern", (getter)Pattern_get_pattern, NULL, PyDoc_STR("The original pattern."), NULL},
    {"pattern_bytes", (getter)Pattern_get_pattern_bytes, NULL, PyDoc_STR("UTF-8 encoded pattern."), NULL},
    {"flags", (getter)Pattern_get_flags, NULL, PyDoc_STR("Compile-time options."), NULL},
    {"jit", (getter)Pattern_get_jit, NULL, PyDoc_STR("Whether the pattern was JIT compiled."), NULL},
    {"groupindex", (getter)Pattern_get_groupindex, NULL, PyDoc_STR("Mapping of named capture groups."), NULL},
    {"capture_count", (getter)Pattern_get_capture_count, NULL, PyDoc_STR("Number of capturing groups."), NULL},
    {NULL, NULL, NULL, NULL, NULL},
};

PyTypeObject PatternType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pcre.Pattern",
    .tp_basicsize = sizeof(PatternObject),
    .tp_dealloc = (destructor)Pattern_dealloc,
    .tp_repr = (reprfunc)Pattern_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = Pattern_methods,
    .tp_getset = Pattern_getset,
    .tp_doc = "Compiled PCRE2 pattern.",
};

static PatternObject *
Pattern_create(PyObject *pattern_obj, uint32_t options, int jit, int jit_explicit)
{
    PyObject *pattern_bytes = bytes_from_text(pattern_obj);
    if (pattern_bytes == NULL) {
        return NULL;
    }

    Py_ssize_t pattern_length = PyBytes_GET_SIZE(pattern_bytes);
    int is_bytes = PyBytes_Check(pattern_obj);

    uint32_t compile_options = options;

    int error_code;
    PCRE2_SIZE error_offset;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)PyBytes_AS_STRING(pattern_bytes),
                                     (PCRE2_SIZE)pattern_length,
                                     compile_options,
                                     &error_code,
                                     &error_offset,
                                     NULL);
    if (code == NULL) {
        raise_pcre_error("compile", error_code, error_offset);
        Py_DECREF(pattern_bytes);
        return NULL;
    }

    PatternObject *pattern = PyObject_New(PatternObject, &PatternType);
    if (pattern == NULL) {
        pcre2_code_free(code);
        Py_DECREF(pattern_bytes);
        return NULL;
    }

    pattern->code = NULL;
    pattern->pattern = NULL;
    pattern->pattern_bytes = NULL;
    pattern->groupindex = NULL;
#if defined(PCRE_EXT_HAVE_ATOMICS)
    atomic_store_explicit(&pattern->jit_enabled, 0, memory_order_relaxed);
    atomic_store_explicit(&pattern->cached_match_data, NULL, memory_order_relaxed);
    atomic_store_explicit(&pattern->cached_match_context, NULL, memory_order_relaxed);
#else
    pattern->jit_lock = PyThread_allocate_lock();
    if (pattern->jit_lock == NULL) {
        PyErr_NoMemory();
        PyObject_Del(pattern);
        pcre2_code_free(code);
        Py_DECREF(pattern_bytes);
        return NULL;
    }
    pattern->jit_enabled = 0;
    pattern->cached_match_data = NULL;
    pattern->cached_match_context = NULL;
#endif

    pattern->code = code;
    Py_INCREF(pattern_obj);
    pattern->pattern = pattern_obj;
    pattern->pattern_bytes = pattern_bytes;
    pattern->pattern_is_bytes = is_bytes;
    pattern->compile_options = compile_options;
    pattern_jit_set(pattern, 0);
    pattern->has_first_literal = 0;
    pattern->first_literal = 0;
    pattern->first_literal_caseless = (compile_options & PCRE2_CASELESS) != 0;

    uint32_t capture_count = 0;
    if (pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &capture_count) != 0) {
        capture_count = 0;
    }
    pattern->capture_count = capture_count;

    uint32_t first_code_type = 0;
    if (!pattern->first_literal_caseless &&
        pcre2_pattern_info(code, PCRE2_INFO_FIRSTCODETYPE, &first_code_type) == 0 &&
        first_code_type == 1u) {
        uint32_t first_code_unit = 0;
        if (pcre2_pattern_info(code, PCRE2_INFO_FIRSTCODEUNIT, &first_code_unit) == 0) {
            pattern->has_first_literal = 1;
            pattern->first_literal = first_code_unit & 0xFFu;
        }
    }

    pattern->groupindex = create_groupindex_dict(code);
    if (pattern->groupindex == NULL) {
        Py_DECREF(pattern);
        return NULL;
    }

    if (jit) {
        int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
        if (jit_rc == 0) {
            pattern_jit_set(pattern, 1);
        } else if (jit_rc == PCRE2_ERROR_JIT_BADOPTION) {
            pattern_jit_set(pattern, 0);
#ifdef PCRE2_ERROR_JIT_UNSUPPORTED
        } else if (!jit_explicit && jit_rc == PCRE2_ERROR_JIT_UNSUPPORTED) {
            pattern_jit_set(pattern, 0);
#endif
        } else {
            Py_DECREF(pattern);
            raise_pcre_error("jit_compile", jit_rc, 0);
            return NULL;
        }
    }

    return pattern;
}

static PatternObject *
Pattern_compile_cached(PyObject *pattern_obj, uint32_t flags, int jit, int jit_explicit)
{
    PyObject *flags_obj = NULL;
    PyObject *jit_bool = NULL;
    PyObject *cache_key = NULL;
    int use_cache = 1;
    int cache_state_valid = 0;
    int lock_held = 0;
    PatternCacheState *cache_state = NULL;

    flags_obj = PyLong_FromUnsignedLong(flags);
    if (flags_obj == NULL) {
        return NULL;
    }
    jit_bool = PyBool_FromLong(jit != 0);
    if (jit_bool == NULL) {
        Py_DECREF(flags_obj);
        return NULL;
    }

    cache_key = PyTuple_Pack(3, pattern_obj, flags_obj, jit_bool);
    if (cache_key == NULL) {
        Py_DECREF(flags_obj);
        Py_DECREF(jit_bool);
        return NULL;
    }

    Py_DECREF(flags_obj);
    Py_DECREF(jit_bool);

    if (PyObject_Hash(cache_key) == -1) {
        PyErr_Clear();
        use_cache = 0;
    }

    if (use_cache) {
        if (pattern_cache_acquire_state(&cache_state, &lock_held) < 0) {
            PyErr_Clear();
            use_cache = 0;
            cache_state = NULL;
            lock_held = 0;
        } else {
            cache_state_valid = 1;
            PyObject *cached = PyDict_GetItem(cache_state->map, cache_key);
            if (cached != NULL) {
                Py_INCREF(cached);
                if (cache_state->order != NULL) {
                    Py_ssize_t idx = PySequence_Index(cache_state->order, cache_key);
                    if (idx >= 0) {
                        if (PySequence_DelItem(cache_state->order, idx) == 0) {
                            if (PyList_Append(cache_state->order, cache_key) < 0) {
                                PyErr_Clear();
                            }
                        } else {
                            PyErr_Clear();
                        }
                    } else {
                        PyErr_Clear();
                    }
                }
                pattern_cache_release_state(lock_held);
                Py_DECREF(cache_key);
                return (PatternObject *)cached;
            }
            pattern_cache_release_state(lock_held);
            lock_held = 0;
        }
    }

    PatternObject *compiled = Pattern_create(pattern_obj, flags, jit, jit_explicit);
    if (compiled == NULL) {
        Py_DECREF(cache_key);
        return NULL;
    }

    if (use_cache && cache_state_valid) {
        if (pattern_cache_acquire_state(&cache_state, &lock_held) < 0) {
            PyErr_Clear();
            use_cache = 0;
            cache_state = NULL;
            lock_held = 0;
        }
    }

    if (use_cache && cache_state_valid && cache_state != NULL) {
        if (PyDict_SetItem(cache_state->map, cache_key, (PyObject *)compiled) == 0) {
            if (cache_state->order != NULL) {
                if (PyList_Append(cache_state->order, cache_key) < 0) {
                    PyErr_Clear();
                } else if (cache_state->limit >= 0 && PyList_GET_SIZE(cache_state->order) > cache_state->limit) {
                    PyObject *old_key = PyList_GET_ITEM(cache_state->order, 0);
                    Py_INCREF(old_key);
                    if (PySequence_DelItem(cache_state->order, 0) < 0) {
                        PyErr_Clear();
                    } else if (PyDict_DelItem(cache_state->map, old_key) < 0) {
                        PyErr_Clear();
                    }
                    Py_DECREF(old_key);
                }
            }
        } else {
            PyErr_Clear();
        }
        pattern_cache_release_state(lock_held);
        lock_held = 0;
    }

    Py_DECREF(cache_key);
    return compiled;
}

static PyObject *
module_compile(PyObject *Py_UNUSED(module), PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"pattern", "flags", "jit", NULL};
    PyObject *pattern = NULL;
    unsigned long flags = 0;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|k$O", kwlist, &pattern, &flags, &jit_obj)) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *compiled = Pattern_compile_cached(pattern, (uint32_t)flags, jit, jit_explicit);
    return (PyObject *)compiled;
}

static PyObject *
module_match(PyObject *Py_UNUSED(module), PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"pattern", "string", "flags", "jit", NULL};
    PyObject *pattern_obj = NULL;
    PyObject *subject = NULL;
    unsigned long flags = 0;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|k$O", kwlist, &pattern_obj, &subject, &flags, &jit_obj)) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *pattern = Pattern_compile_cached(pattern_obj, (uint32_t)flags, jit, jit_explicit);
    if (pattern == NULL) {
        return NULL;
    }

    PyObject *result = Pattern_execute(pattern, subject, 0, -1, 0, EXEC_MODE_MATCH);
    Py_DECREF(pattern);
    return result;
}

static PyObject *
module_search(PyObject *Py_UNUSED(module), PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"pattern", "string", "flags", "jit", NULL};
    PyObject *pattern_obj = NULL;
    PyObject *subject = NULL;
    unsigned long flags = 0;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|k$O", kwlist, &pattern_obj, &subject, &flags, &jit_obj)) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *pattern = Pattern_compile_cached(pattern_obj, (uint32_t)flags, jit, jit_explicit);
    if (pattern == NULL) {
        return NULL;
    }

    PyObject *result = Pattern_execute(pattern, subject, 0, -1, 0, EXEC_MODE_SEARCH);
    Py_DECREF(pattern);
    return result;
}

static PyObject *
module_fullmatch(PyObject *Py_UNUSED(module), PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"pattern", "string", "flags", "jit", NULL};
    PyObject *pattern_obj = NULL;
    PyObject *subject = NULL;
    unsigned long flags = 0;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|k$O", kwlist, &pattern_obj, &subject, &flags, &jit_obj)) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *pattern = Pattern_compile_cached(pattern_obj, (uint32_t)flags, jit, jit_explicit);
    if (pattern == NULL) {
        return NULL;
    }

    PyObject *result = Pattern_execute(pattern, subject, 0, -1, 0, EXEC_MODE_FULLMATCH);
    Py_DECREF(pattern);
    return result;
}

static PyObject *
module_configure(PyObject *Py_UNUSED(module), PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"jit", NULL};
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &jit_obj)) {
        return NULL;
    }

    if (jit_obj != Py_None) {
        int jit = 0;
        int current_default = default_jit_get();
        if (coerce_jit_argument(jit_obj, current_default, &jit, NULL) < 0) {
            return NULL;
        }
        default_jit_set(jit);
    }

    if (default_jit_get()) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject *module_cpu_ascii_vector_mode(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args));
static PyObject *module_memory_allocator(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args));
static PyObject *module_get_pcre2_version(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args));
static void initialize_pcre2_version(void);

static PyObject *
module_translate_unicode_escapes(PyObject *Py_UNUSED(module), PyObject *arg)
{
    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "pattern must be str");
        return NULL;
    }

    Py_ssize_t byte_length = 0;
    const char *src = PyUnicode_AsUTF8AndSize(arg, &byte_length);
    if (src == NULL) {
        return NULL;
    }

    if (byte_length < 2) {
        return Py_NewRef(arg);
    }

    if (byte_length > (PY_SSIZE_T_MAX - 1) / 2) {
        PyErr_SetString(PyExc_OverflowError, "pattern too large to translate");
        return NULL;
    }

    Py_ssize_t capacity = (byte_length * 2) + 1;
    char *buffer = PyMem_Malloc((size_t)capacity);
    if (buffer == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    const char *p = src;
    const char *end = src + byte_length;
    char *out = buffer;
    int modified = 0;

    while (p < end) {
        if (p + 1 < end && p[0] == '\\' && (p[1] == 'u' || p[1] == 'U')) {
            int is_upper = (p[1] == 'U');
            int hex_len = is_upper ? 8 : 4;
            if (p + 2 + hex_len <= end) {
                unsigned int codepoint = 0;
                int valid = 1;
                for (int offset = 0; offset < hex_len; ++offset) {
                    unsigned char digit = (unsigned char)p[2 + offset];
                    if (!is_hex_digit(digit)) {
                        valid = 0;
                        break;
                    }
                    codepoint = (codepoint << 4) | hex_value(digit);
                }
                if (valid) {
                    if (codepoint > 0x10FFFFu) {
                        PyMem_Free(buffer);
                        PyErr_Format(
                            PcreError,
                            "Unicode escape \\%c%.*s exceeds 0x10FFFF",
                            p[1],
                            hex_len,
                            p + 2
                        );
                        return NULL;
                    }

                    *out++ = '\\';
                    *out++ = 'x';
                    *out++ = '{';
                    memcpy(out, p + 2, (size_t)hex_len);
                    out += hex_len;
                    *out++ = '}';
                    p += 2 + hex_len;
                    modified = 1;
                    continue;
                }
            }
        }

        *out++ = *p++;
    }

    if (!modified) {
        PyMem_Free(buffer);
        return Py_NewRef(arg);
    }

    Py_ssize_t result_length = out - buffer;
    PyObject *result = PyUnicode_DecodeUTF8(buffer, result_length, "strict");
    PyMem_Free(buffer);
    return result;
}

static PyMethodDef module_methods[] = {
    {"compile", (PyCFunction)module_compile, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Compile a pattern into a PCRE2 Pattern object." )},
    {"match", (PyCFunction)module_match, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Match a pattern against the beginning of a string." )},
    {"search", (PyCFunction)module_search, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Search a string for a pattern." )},
    {"fullmatch", (PyCFunction)module_fullmatch, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Match a pattern against the entire string." )},
    {"configure", (PyCFunction)module_configure, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Get or set module-wide defaults (currently only 'jit')." )},
    {"get_match_data_cache_size", (PyCFunction)module_get_match_data_cache_size, METH_NOARGS, PyDoc_STR("Return the capacity of the reusable match-data cache." )},
    {"set_match_data_cache_size", (PyCFunction)module_set_match_data_cache_size, METH_VARARGS, PyDoc_STR("Set the capacity of the reusable match-data cache." )},
    {"clear_match_data_cache", (PyCFunction)module_clear_match_data_cache, METH_NOARGS, PyDoc_STR("Release all cached PCRE2 match-data buffers." )},
    {"get_match_data_cache_count", (PyCFunction)module_get_match_data_cache_count, METH_NOARGS, PyDoc_STR("Return the number of cached match-data buffers currently stored." )},
    {"get_cache_strategy", (PyCFunction)module_get_cache_strategy, METH_NOARGS, PyDoc_STR("Return the active caching strategy ('thread-local' or 'global')." )},
    {"set_cache_strategy", (PyCFunction)module_set_cache_strategy, METH_VARARGS, PyDoc_STR("Set the caching strategy to 'thread-local' (default) or 'global'." )},
    {"clear_pattern_cache", (PyCFunction)module_clear_pattern_cache, METH_NOARGS, PyDoc_STR("Release cached compiled pattern objects." )},
    {"get_jit_stack_cache_size", (PyCFunction)module_get_jit_stack_cache_size, METH_NOARGS, PyDoc_STR("Return the capacity of the reusable JIT stack cache." )},
    {"set_jit_stack_cache_size", (PyCFunction)module_set_jit_stack_cache_size, METH_VARARGS, PyDoc_STR("Set the capacity of the reusable JIT stack cache." )},
    {"clear_jit_stack_cache", (PyCFunction)module_clear_jit_stack_cache, METH_NOARGS, PyDoc_STR("Release all cached PCRE2 JIT stacks." )},
    {"get_jit_stack_cache_count", (PyCFunction)module_get_jit_stack_cache_count, METH_NOARGS, PyDoc_STR("Return the number of cached JIT stacks currently stored." )},
    {"get_jit_stack_limits", (PyCFunction)module_get_jit_stack_limits, METH_NOARGS, PyDoc_STR("Return the configured (start, max) JIT stack sizes." )},
    {"set_jit_stack_limits", (PyCFunction)module_set_jit_stack_limits, METH_VARARGS, PyDoc_STR("Set the (start, max) sizes for newly created JIT stacks." )},
    {"get_library_version", (PyCFunction)module_get_pcre2_version, METH_NOARGS, PyDoc_STR("Return the PCRE2 library version string." )},
    {"get_allocator", (PyCFunction)module_memory_allocator, METH_NOARGS, PyDoc_STR("Return the name of the active heap allocator (tcmalloc/jemalloc/malloc)." )},
    {"_cpu_ascii_vector_mode", (PyCFunction)module_cpu_ascii_vector_mode, METH_NOARGS, PyDoc_STR("Return the active ASCII vector width (0=scalar,1=SSE2,2=AVX2,3=AVX512)." )},
    {"_debug_thread_cache_count", (PyCFunction)module_debug_thread_cache_count, METH_NOARGS, PyDoc_STR("Return the number of live thread cache states (requires PYPCRE_DEBUG=1)." )},
    {"translate_unicode_escapes", (PyCFunction)module_translate_unicode_escapes, METH_O, PyDoc_STR("Translate literal \\uXXXX/\\UXXXXXXXX escapes to PCRE2-compatible \\x{...} sequences." )},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pcre_ext_c",
    .m_doc = "Low-level bindings to the PCRE2 regular expression engine.",
    .m_size = -1,
    .m_methods = module_methods,
#if defined(Py_MOD_GIL_SAFE_FLAG)
    .m_flags = Py_MOD_GIL_SAFE_FLAG,
#endif
};

static void
detect_offset_limit_support(void)
{
#if defined(PCRE2_USE_OFFSET_LIMIT)
    int current = atomic_load_explicit(&offset_limit_support, memory_order_acquire);
    if (current != -1) {
        return;
    }

    int support = 0;
    int error_code = 0;
    PCRE2_SIZE error_offset = 0;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)".", 1, 0, &error_code, &error_offset, NULL);
    if (code == NULL) {
        atomic_store_explicit(&offset_limit_support, 0, memory_order_release);
        return;
    }

    pcre2_match_data *match_data = pcre2_match_data_create(2, NULL);
    if (match_data == NULL) {
        pcre2_code_free(code);
        atomic_store_explicit(&offset_limit_support, 0, memory_order_release);
        return;
    }

    pcre2_match_context *match_context = pcre2_match_context_create(NULL);
    if (match_context == NULL) {
        pcre2_match_data_free(match_data);
        pcre2_code_free(code);
        atomic_store_explicit(&offset_limit_support, 0, memory_order_release);
        return;
    }

    int rc = pcre2_set_offset_limit(match_context, 0);
    if (rc >= 0) {
        rc = pcre2_match(code,
                         (PCRE2_SPTR)"a",
                         1,
                         0,
                         PCRE2_USE_OFFSET_LIMIT,
                         match_data,
                         match_context);
        if (rc != PCRE2_ERROR_BADOPTION) {
            support = 1;
        }
    }

    pcre2_match_context_free(match_context);
    pcre2_match_data_free(match_data);
    pcre2_code_free(code);

    atomic_store_explicit(&offset_limit_support, support, memory_order_release);
#endif
}

PyMODINIT_FUNC
PyInit_pcre_ext_c(void)
{
    PyObject *module = NULL;
    const char *force_lock_env = NULL;
    const char *context_cache_env = NULL;
    const char *pattern_cache_env = NULL;
    int pattern_cache_global = 0;

#if !defined(PCRE_EXT_HAVE_ATOMICS)
    if (default_jit_lock == NULL) {
        default_jit_lock = PyThread_allocate_lock();
        if (default_jit_lock == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
    }
#endif

    force_lock_env = Py_GETENV("PYPCRE_FORCE_JIT_LOCK");
    if (force_lock_env == NULL) {
        force_lock_env = Py_GETENV("PCRE2_FORCE_JIT_LOCK");
    }
    if (env_flag_is_true(force_lock_env)) {
        pcre_force_jit_lock = 1;
        if (jit_serial_lock == NULL) {
            jit_serial_lock = PyThread_allocate_lock();
            if (jit_serial_lock == NULL) {
                PyErr_NoMemory();
                goto error_locks;
            }
        }
    } else {
        pcre_force_jit_lock = 0;
    }

    context_cache_env = Py_GETENV("PYPCRE_DISABLE_CONTEXT_CACHE");
    if (context_cache_env == NULL) {
        context_cache_env = Py_GETENV("PCRE2_DISABLE_CONTEXT_CACHE");
    }
    cache_set_context_cache_enabled(env_flag_is_true(context_cache_env) ? 0 : 1);

    pattern_cache_env = Py_GETENV("PYPCRE_CACHE_PATTERN_GLOBAL");
    if (pattern_cache_env == NULL) {
        pattern_cache_env = Py_GETENV("PCRE2_CACHE_PATTERN_GLOBAL");
    }
    pattern_cache_global = env_flag_is_true(pattern_cache_env);
    if (pattern_cache_initialize(pattern_cache_global) < 0) {
        goto error_locks;
    }

    if (PyType_Ready(&PatternType) < 0) {
        goto error_pattern_cache;
    }
    if (PyType_Ready(&MatchType) < 0) {
        goto error_pattern_cache;
    }
    if (PyType_Ready(&FindIterType) < 0) {
        goto error_pattern_cache;
    }

    module = PyModule_Create(&moduledef);
    if (module == NULL) {
        goto error_pattern_cache;
    }

    if (pcre_memory_initialize() < 0) {
        goto error_module;
    }

    if (pcre_error_init(module) < 0) {
        goto error_memory;
    }

    if (cache_initialize() < 0) {
        goto error_errors;
    }

    detect_offset_limit_support();

    Py_INCREF(&PatternType);
    if (PyModule_AddObject(module, "Pattern", (PyObject *)&PatternType) < 0) {
        Py_DECREF(&PatternType);
        goto error_cache;
    }

    Py_INCREF(&MatchType);
    if (PyModule_AddObject(module, "Match", (PyObject *)&MatchType) < 0) {
        Py_DECREF(&MatchType);
        goto error_cache;
    }

    if (pcre_flag_add_constants(module) < 0) {
        goto error_cache;
    }

    initialize_pcre2_version();

    if (PyModule_AddStringConstant(module, "PCRE2_VERSION", pcre2_library_version) < 0) {
        goto error_cache;
    }

    if (PyModule_AddStringConstant(module, "__version__", "0.1.0") < 0) {
        goto error_cache;
    }

    if (PyModule_AddIntConstant(module, "PCRE2_CODE_UNIT_WIDTH", PCRE2_CODE_UNIT_WIDTH) < 0) {
        goto error_cache;
    }

#if defined(Py_GIL_DISABLED)
    if (PyUnstable_Module_SetGIL(module, Py_MOD_GIL_NOT_USED) < 0) {
        goto error_cache;
    }
#endif

    return module;

error_cache:
    cache_teardown();
error_errors:
    pcre_error_teardown();
error_memory:
    pcre_memory_teardown();
error_module:
    Py_DECREF(module);
error_pattern_cache:
    pattern_cache_teardown();
error_locks:
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
    pcre_force_jit_lock = 0;
    return NULL;
}

static PyObject *
module_cpu_ascii_vector_mode(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
#if (defined(__x86_64__) && defined(__GNUC__)) || defined(_M_X64) || defined(_M_AMD64)
    return PyLong_FromLong(ascii_vector_mode());
#else
    return PyLong_FromLong(0);
#endif
}

static PyObject *
module_memory_allocator(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    const char *name = pcre_memory_allocator_name();
    return PyUnicode_FromString(name);
}

static PyObject *
module_get_pcre2_version(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    initialize_pcre2_version();
    return PyUnicode_FromString(pcre2_library_version);
}

static void
initialize_pcre2_version(void)
{
    if (atomic_load_explicit(&pcre2_version_initialized, memory_order_acquire)) {
        return;
    }

    char buffer[sizeof(pcre2_library_version)] = {0};
    if (pcre2_config(PCRE2_CONFIG_VERSION, buffer) == 0 && buffer[0] != '\0') {
        strncpy(pcre2_library_version, buffer, sizeof(pcre2_library_version) - 1);
        pcre2_library_version[sizeof(pcre2_library_version) - 1] = '\0';
    } else {
        const char *pre_release = resolve_pcre2_prerelease();
        if (pre_release[0] != '\0') {
            (void)snprintf(
                pcre2_library_version,
                sizeof(pcre2_library_version),
                "%d.%d-%s",
                PCRE2_MAJOR,
                PCRE2_MINOR,
                pre_release
            );
        } else {
            (void)snprintf(
                pcre2_library_version,
                sizeof(pcre2_library_version),
                "%d.%d",
                PCRE2_MAJOR,
                PCRE2_MINOR
            );
        }
    }
    atomic_store_explicit(&pcre2_version_initialized, 1, memory_order_release);
}
