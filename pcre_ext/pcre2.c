// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"
#include <stdio.h>
#include <string.h>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

static int default_jit_enabled = 1;
static PyThread_type_lock default_jit_lock = NULL;
static PyThread_type_lock cpu_feature_lock = NULL;
#if defined(PCRE2_USE_OFFSET_LIMIT)
static int offset_limit_support = -1;
#endif

static void detect_offset_limit_support(void);

static inline int
default_jit_get(void)
{
    if (default_jit_lock == NULL) {
        return default_jit_enabled;
    }
    PyThread_acquire_lock(default_jit_lock, 1);
    int value = default_jit_enabled;
    PyThread_release_lock(default_jit_lock);
    return value;
}

static inline void
default_jit_set(int value)
{
    if (default_jit_lock == NULL) {
        default_jit_enabled = value;
        return;
    }
    PyThread_acquire_lock(default_jit_lock, 1);
    default_jit_enabled = value;
    PyThread_release_lock(default_jit_lock);
}

static inline int
pattern_jit_get(PatternObject *pattern)
{
    if (pattern->jit_lock != NULL) {
        PyThread_acquire_lock(pattern->jit_lock, 1);
        int value = pattern->jit_enabled;
        PyThread_release_lock(pattern->jit_lock);
        return value;
    }
    return pattern->jit_enabled;
}

static inline void
pattern_jit_set(PatternObject *pattern, int value)
{
    if (pattern->jit_lock != NULL) {
        PyThread_acquire_lock(pattern->jit_lock, 1);
        pattern->jit_enabled = value;
        PyThread_release_lock(pattern->jit_lock);
        return;
    }
    pattern->jit_enabled = value;
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
    static int cached = -1;
    if (cpu_feature_lock != NULL) {
        PyThread_acquire_lock(cpu_feature_lock, 1);
    }
    if (cached == -1) {
        cached = 0;
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
        if (__builtin_cpu_supports("avx512bw")) {
            cached = 3;
    } else if (__builtin_cpu_supports("avx2")) {
        cached = 2;
    } else if (__builtin_cpu_supports("sse2")) {
            cached = 1;
        }
#endif
    }
    int result = cached;
    if (cpu_feature_lock != NULL) {
        PyThread_release_lock(cpu_feature_lock);
    }
    return result;
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
        Py_BEGIN_ALLOW_THREADS
        rc = pcre2_jit_match(self->pattern->code,
                             (PCRE2_SPTR)buffer,
                             exec_length,
                             (PCRE2_SIZE)self->current_byte,
                             options,
                             self->match_data,
                             self->match_context);
        Py_END_ALLOW_THREADS

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

    Py_BEGIN_ALLOW_THREADS
    rc = pcre2_match(self->pattern->code,
                     (PCRE2_SPTR)buffer,
                     exec_length,
                     (PCRE2_SIZE)self->current_byte,
                     options,
                     self->match_data,
                     self->match_context);
    Py_END_ALLOW_THREADS

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
    } else if (PyUnicode_Check(subject_obj)) {
        if (PyUnicode_READY(subject_obj) < 0) {
            goto error;
        }
        Py_ssize_t utf8_length = 0;
        const char *utf8_data = PyUnicode_AsUTF8AndSize(subject_obj, &utf8_length);
        if (utf8_data == NULL) {
            goto error;
        }
        iter->subject_is_bytes = 0;
        iter->subject_length_bytes = utf8_length;
        iter->logical_length = PyUnicode_GET_LENGTH(subject_obj);
        iter->utf8_data = utf8_data;
        Py_INCREF(subject_obj);
        iter->utf8_owner = subject_obj;
        if (PyUnicode_IS_ASCII(subject_obj)) {
            iter->utf8_is_ascii = 1;
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
    if (self->jit_lock != NULL) {
        PyThread_free_lock(self->jit_lock);
        self->jit_lock = NULL;
    }
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
    } else if (PyUnicode_Check(subject_obj)) {
        if (PyUnicode_READY(subject_obj) < 0) {
            return NULL;
        }
        Py_ssize_t utf8_length = 0;
        const char *utf8_data = PyUnicode_AsUTF8AndSize(subject_obj, &utf8_length);
        if (utf8_data == NULL) {
            return NULL;
        }
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        buffer = utf8_data;
        subject_length_bytes = utf8_length;
        logical_length = PyUnicode_GET_LENGTH(subject_obj);
        if (PyUnicode_IS_ASCII(subject_obj)) {
            ascii_text = 1;
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

    pcre2_match_data *match_data = match_data_cache_acquire(self);
    if (match_data == NULL) {
        Py_DECREF(utf8_owner);
        PyErr_NoMemory();
        return NULL;
    }

    pcre2_match_context *match_context = NULL;
    pcre2_jit_stack *jit_stack = NULL;
    int using_jit_stack = 0;
    PCRE2_SIZE exec_length = (PCRE2_SIZE)subject_length_bytes;
    int need_offset_limit = (offset_limit != (PCRE2_SIZE)subject_length_bytes);
#if defined(PCRE2_USE_OFFSET_LIMIT)
    int use_offset_limit_option = need_offset_limit && offset_limit_option_enabled();
#else
    int use_offset_limit_option = 0;
#endif

    if (use_offset_limit_option) {
#if defined(PCRE2_USE_OFFSET_LIMIT)
        match_context = pcre2_match_context_create(NULL);
        if (match_context == NULL) {
            match_data_cache_release(match_data);
            Py_DECREF(utf8_owner);
            PyErr_NoMemory();
            return NULL;
        }
        int ctx_rc = pcre2_set_offset_limit(match_context, offset_limit);
        if (ctx_rc < 0) {
            pcre2_match_context_free(match_context);
            match_data_cache_release(match_data);
            Py_DECREF(utf8_owner);
            raise_pcre_error("set_offset_limit", ctx_rc, 0);
            return NULL;
        }
        match_options |= PCRE2_USE_OFFSET_LIMIT;
#endif
    } else if (need_offset_limit) {
        exec_length = offset_limit;
        if (exec_length < (PCRE2_SIZE)byte_start) {
            exec_length = (PCRE2_SIZE)byte_start;
        }
    }

    int rc = 0;

    int attempt_jit = pattern_jit_get(self);

    if (attempt_jit) {
        if (match_context == NULL) {
            match_context = pcre2_match_context_create(NULL);
            if (match_context == NULL) {
                match_data_cache_release(match_data);
                Py_DECREF(utf8_owner);
                PyErr_NoMemory();
                return NULL;
            }
        }

        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            pcre2_match_context_free(match_context);
            match_data_cache_release(match_data);
            Py_DECREF(utf8_owner);
            PyErr_NoMemory();
            return NULL;
        }

        pcre2_jit_stack_assign(match_context, NULL, jit_stack);
        using_jit_stack = 1;

        Py_BEGIN_ALLOW_THREADS
        rc = pcre2_jit_match(self->code,
                              (PCRE2_SPTR)buffer,
                              exec_length,
                              (PCRE2_SIZE)byte_start,
                              match_options,
                              match_data,
                              match_context);
        Py_END_ALLOW_THREADS

        if (using_jit_stack) {
            jit_stack_cache_release(jit_stack);
            jit_stack = NULL;
            using_jit_stack = 0;
        }

        if (rc == PCRE2_ERROR_JIT_BADOPTION || rc == PCRE2_ERROR_BADOPTION) {
            pattern_jit_set(self, 0);
        } else if (rc != PCRE2_ERROR_NOMATCH && rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
            match_data_cache_release(match_data);
            if (match_context != NULL) {
                pcre2_match_context_free(match_context);
            }
            Py_DECREF(utf8_owner);
            raise_pcre_error("jit_match", rc, error_offset);
            return NULL;
        }
    }

    if (!pattern_jit_get(self)) {
        Py_BEGIN_ALLOW_THREADS
        rc = pcre2_match(self->code,
                         (PCRE2_SPTR)buffer,
                         exec_length,
                         (PCRE2_SIZE)byte_start,
                         match_options,
                         match_data,
                         match_context);
        Py_END_ALLOW_THREADS
    }

    if (rc == PCRE2_ERROR_NOMATCH) {
        match_data_cache_release(match_data);
        if (match_context != NULL) {
            pcre2_match_context_free(match_context);
        }
        Py_DECREF(utf8_owner);
        Py_RETURN_NONE;
    }

    if (rc < 0) {
        PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
        match_data_cache_release(match_data);
        if (match_context != NULL) {
            pcre2_match_context_free(match_context);
        }
        Py_DECREF(utf8_owner);
        raise_pcre_error("match", rc, error_offset);
        return NULL;
    }

    uint32_t available_ovector_pairs = pcre2_get_ovector_count(match_data);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    if (ovector == NULL || available_ovector_pairs == 0) {
        match_data_cache_release(match_data);
        if (match_context != NULL) {
            pcre2_match_context_free(match_context);
        }
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
    match_data_cache_release(match_data);
    if (match_context != NULL) {
        pcre2_match_context_free(match_context);
    }
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
    pattern->jit_lock = NULL;
    pattern->jit_enabled = 0;

    pattern->jit_lock = PyThread_allocate_lock();
    if (pattern->jit_lock == NULL) {
        PyErr_NoMemory();
        PyObject_Del(pattern);
        pcre2_code_free(code);
        Py_DECREF(pattern_bytes);
        return NULL;
    }

    pattern->code = code;
    Py_INCREF(pattern_obj);
    pattern->pattern = pattern_obj;
    pattern->pattern_bytes = pattern_bytes;
    pattern->pattern_is_bytes = is_bytes;
    pattern->compile_options = compile_options;
    pattern_jit_set(pattern, 0);

    uint32_t capture_count = 0;
    if (pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &capture_count) != 0) {
        capture_count = 0;
    }
    pattern->capture_count = capture_count;

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

    return (PyObject *)Pattern_create(pattern, (uint32_t)flags, jit, jit_explicit);
}

static PyObject *
call_pattern_method(PatternObject *pattern, PyObject *callable, PyObject *subject)
{
    PyObject *args = PyTuple_Pack(1, subject);
    if (args == NULL) {
        return NULL;
    }
    PyObject *result = PyObject_Call(callable, args, NULL);
    Py_DECREF(args);
    return result;
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

    PatternObject *pattern = Pattern_create(pattern_obj, (uint32_t)flags, jit, jit_explicit);
    if (pattern == NULL) {
        return NULL;
    }

    PyObject *callable = PyObject_GetAttrString((PyObject *)pattern, "match");
    if (callable == NULL) {
        Py_DECREF(pattern);
        return NULL;
    }
    PyObject *result = call_pattern_method(pattern, callable, subject);
    Py_DECREF(callable);
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

    PatternObject *pattern = Pattern_create(pattern_obj, (uint32_t)flags, jit, jit_explicit);
    if (pattern == NULL) {
        return NULL;
    }

    PyObject *callable = PyObject_GetAttrString((PyObject *)pattern, "search");
    if (callable == NULL) {
        Py_DECREF(pattern);
        return NULL;
    }
    PyObject *result = call_pattern_method(pattern, callable, subject);
    Py_DECREF(callable);
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

    PatternObject *pattern = Pattern_create(pattern_obj, (uint32_t)flags, jit, jit_explicit);
    if (pattern == NULL) {
        return NULL;
    }

    PyObject *callable = PyObject_GetAttrString((PyObject *)pattern, "fullmatch");
    if (callable == NULL) {
        Py_DECREF(pattern);
        return NULL;
    }
    PyObject *result = call_pattern_method(pattern, callable, subject);
    Py_DECREF(callable);
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
    {"get_jit_stack_cache_size", (PyCFunction)module_get_jit_stack_cache_size, METH_NOARGS, PyDoc_STR("Return the capacity of the reusable JIT stack cache." )},
    {"set_jit_stack_cache_size", (PyCFunction)module_set_jit_stack_cache_size, METH_VARARGS, PyDoc_STR("Set the capacity of the reusable JIT stack cache." )},
    {"clear_jit_stack_cache", (PyCFunction)module_clear_jit_stack_cache, METH_NOARGS, PyDoc_STR("Release all cached PCRE2 JIT stacks." )},
    {"get_jit_stack_cache_count", (PyCFunction)module_get_jit_stack_cache_count, METH_NOARGS, PyDoc_STR("Return the number of cached JIT stacks currently stored." )},
    {"get_jit_stack_limits", (PyCFunction)module_get_jit_stack_limits, METH_NOARGS, PyDoc_STR("Return the configured (start, max) JIT stack sizes." )},
    {"set_jit_stack_limits", (PyCFunction)module_set_jit_stack_limits, METH_VARARGS, PyDoc_STR("Set the (start, max) sizes for newly created JIT stacks." )},
    {"get_allocator", (PyCFunction)module_memory_allocator, METH_NOARGS, PyDoc_STR("Return the name of the active heap allocator (tcmalloc/jemalloc/malloc)." )},
    {"_cpu_ascii_vector_mode", (PyCFunction)module_cpu_ascii_vector_mode, METH_NOARGS, PyDoc_STR("Return the active ASCII vector width (0=scalar,1=SSE2,2=AVX2,3=AVX512)." )},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pcre.cpcre2",
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
    if (offset_limit_support != -1) {
        return;
    }

    int error_code = 0;
    PCRE2_SIZE error_offset = 0;
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)".", 1, 0, &error_code, &error_offset, NULL);
    if (code == NULL) {
        offset_limit_support = 0;
        return;
    }

    pcre2_match_data *match_data = pcre2_match_data_create(2, NULL);
    if (match_data == NULL) {
        pcre2_code_free(code);
        offset_limit_support = 0;
        return;
    }

    pcre2_match_context *match_context = pcre2_match_context_create(NULL);
    if (match_context == NULL) {
        pcre2_match_data_free(match_data);
        pcre2_code_free(code);
        offset_limit_support = 0;
        return;
    }

    int rc = pcre2_set_offset_limit(match_context, 0);
    if (rc < 0) {
        offset_limit_support = 0;
    } else {
        rc = pcre2_match(code,
                         (PCRE2_SPTR)"a",
                         1,
                         0,
                         PCRE2_USE_OFFSET_LIMIT,
                         match_data,
                         match_context);
        if (rc == PCRE2_ERROR_BADOPTION) {
            offset_limit_support = 0;
        } else {
            offset_limit_support = 1;
        }
    }

    pcre2_match_context_free(match_context);
    pcre2_match_data_free(match_data);
    pcre2_code_free(code);
#endif
}

PyMODINIT_FUNC
PyInit_cpcre2(void)
{
    if (PyType_Ready(&PatternType) < 0) {
        return NULL;
    }
    if (PyType_Ready(&MatchType) < 0) {
        return NULL;
    }
    if (PyType_Ready(&FindIterType) < 0) {
        return NULL;
    }

    PyObject *module = PyModule_Create(&moduledef);
    if (module == NULL) {
        return NULL;
    }

    if (pcre_memory_initialize() < 0) {
        goto error;
    }

    if (pcre_error_init(module) < 0) {
        goto error;
    }

    if (cache_initialize() < 0) {
        goto error;
    }

    detect_offset_limit_support();

    if (default_jit_lock == NULL) {
        default_jit_lock = PyThread_allocate_lock();
        if (default_jit_lock == NULL) {
            PyErr_NoMemory();
            goto error;
        }
    }

    if (cpu_feature_lock == NULL) {
        cpu_feature_lock = PyThread_allocate_lock();
        if (cpu_feature_lock == NULL) {
            PyErr_NoMemory();
            goto error;
        }
    }

    Py_INCREF(&PatternType);
    if (PyModule_AddObject(module, "Pattern", (PyObject *)&PatternType) < 0) {
        Py_DECREF(&PatternType);
        goto error;
    }

    Py_INCREF(&MatchType);
    if (PyModule_AddObject(module, "Match", (PyObject *)&MatchType) < 0) {
        Py_DECREF(&MatchType);
        goto error;
    }

    if (pcre_flag_add_constants(module) < 0) {
        goto error;
    }

    if (PyModule_AddStringConstant(module, "__version__", "0.1.0") < 0) {
        goto error;
    }

    if (PyModule_AddIntConstant(module, "PCRE2_CODE_UNIT_WIDTH", PCRE2_CODE_UNIT_WIDTH) < 0) {
        goto error;
    }

#if defined(Py_GIL_DISABLED)
    if (PyUnstable_Module_SetGIL(module, Py_MOD_GIL_NOT_USED) < 0) {
        goto error;
    }
#endif

    return module;

error:
    pcre_memory_teardown();
    cache_teardown();
    pcre_error_teardown();
    if (cpu_feature_lock != NULL) {
        PyThread_free_lock(cpu_feature_lock);
        cpu_feature_lock = NULL;
    }
    if (default_jit_lock != NULL) {
        PyThread_free_lock(default_jit_lock);
        default_jit_lock = NULL;
    }
    Py_DECREF(module);
    return NULL;
}

static PyObject *
module_cpu_ascii_vector_mode(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
#if defined(__x86_64__) && defined(__GNUC__)
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
