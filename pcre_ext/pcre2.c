// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"
#include <stdio.h>
#include <string.h>

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

/* Process-wide library metadata cached once during module initialization. */
static char pcre2_library_version[64] = "unknown";
static ATOMIC_VAR(int) pcre2_version_initialized = 0;
#if defined(PCRE2_USE_OFFSET_LIMIT)
/* -1 unknown, 0 unsupported, 1 supported by the loaded PCRE2 runtime. */
static ATOMIC_VAR(int) offset_limit_support = ATOMIC_VAR_INIT(-1);
#endif
/* -1 unknown, 0 compliant, 1 JIT ignores ANCHORED/ENDANCHORED match-time options. */
static ATOMIC_VAR(int) jit_anchor_fixup_needed_state = ATOMIC_VAR_INIT(-1);

static void detect_offset_limit_support(void);
static int jit_anchor_fixup_needed(void);

/*
 * Releasing the GIL is only worthwhile when the PCRE2 call is expected to do
 * enough work to amortize the PyEval_{Save,Restore}Thread overhead.  For very
 * short matches the extra work is measurable, so only release for large inputs.
 */
#define PCRE2_GIL_RELEASE_THRESHOLD 262144ULL

#if defined(Py_GIL_DISABLED)
#define PCRE2_CALL_RELEASE_GIL(call) \
    do {                             \
        rc = (call);                 \
    } while (0)
#else
#define PCRE2_CALL_RELEASE_GIL(call)          \
    do {                                      \
        PyThreadState *_save = PyEval_SaveThread(); \
        rc = (call);                          \
        PyEval_RestoreThread(_save);          \
    } while (0)
#endif

#define PCRE2_CALL_MAYBE_RELEASE_GIL(call, length)     \
    do {                                               \
        if ((length) > PCRE2_GIL_RELEASE_THRESHOLD) { \
            PCRE2_CALL_RELEASE_GIL(call);              \
        } else {                                       \
            rc = (call);                               \
        }                                              \
    } while (0)

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
    Py_XDECREF(self->public_pattern);
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

static inline PyObject *
match_public_pattern(MatchObject *self)
{
    if (self->public_pattern != NULL) {
        return self->public_pattern;
    }
    return (PyObject *)self->pattern;
}

static int
match_resolve_span(MatchObject *self,
                   Py_ssize_t index,
                   Py_ssize_t *start_out,
                   Py_ssize_t *end_out,
                   int allow_missing)
{
    /*
     * Convert the raw byte-oriented ovector entry into the user-visible span.
     * For bytes subjects the PCRE2 offsets are already correct. For text
     * subjects we translate byte offsets back to Python code-point indexes.
     */
    if (index < 0 || (size_t)index >= self->ovec_count) {
        PyErr_SetString(PyExc_IndexError, "group index out of range");
        return -1;
    }

    Py_ssize_t start = self->ovector[(size_t)index * 2];
    Py_ssize_t end = self->ovector[(size_t)index * 2 + 1];
    if (start < 0 || end < 0) {
        if (allow_missing) {
            *start_out = -1;
            *end_out = -1;
            return 0;
        }
        return 1;
    }

    if (self->subject_is_bytes) {
        *start_out = start;
        *end_out = end;
        return 0;
    }

    const char *data = self->utf8_data;
    *start_out = utf8_offset_to_index(data, start);
    *end_out = utf8_offset_to_index(data, end);
    return 0;
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

static inline PyObject *
extract_value_from_offsets(PyObject *subject_obj,
                           const char *utf8_data,
                           int subject_is_bytes,
                           int subject_is_ascii,
                           Py_ssize_t start,
                           Py_ssize_t end)
{
    if (start < 0 || end < 0 || end < start) {
        Py_RETURN_NONE;
    }

    Py_ssize_t length = end - start;
    if (subject_is_bytes) {
        return PyBytes_FromStringAndSize(utf8_data + start, length);
    }

    if (subject_is_ascii) {
        PyObject *slice = PyUnicode_New(length, 127);
        if (slice == NULL) {
            return NULL;
        }
        memcpy(PyUnicode_1BYTE_DATA(slice), utf8_data + start, (size_t)length);
        return slice;
    }

    return PyUnicode_DecodeUTF8(utf8_data + start, length, "strict");
}

static PyObject *
match_get_group_value(MatchObject *self, Py_ssize_t index)
{
    if (index < 0 || (size_t)index >= self->ovec_count) {
        PyErr_SetString(PyExc_IndexError, "group index out of range");
        return NULL;
    }
    Py_ssize_t start = self->ovector[(size_t)index * 2];
    Py_ssize_t end = self->ovector[(size_t)index * 2 + 1];
    int subject_is_ascii = !self->subject_is_bytes && PyUnicode_IS_ASCII(self->subject);

    return extract_value_from_offsets(
        self->subject,
        self->utf8_data,
        self->subject_is_bytes,
        subject_is_ascii,
        start,
        end
    );
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
    Py_ssize_t start = 0;
    Py_ssize_t end = 0;
    int rc = match_resolve_span(self, index, &start, &end, 0);
    if (rc < 0) {
        return NULL;
    }
    if (rc > 0) {
        Py_RETURN_NONE;
    }
    return Py_BuildValue("(nn)", start, end);
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

static PyObject *
Match_get_re(MatchObject *self, void *closure)
{
    PyObject *pattern = match_public_pattern(self);
    Py_INCREF(pattern);
    return pattern;
}

static PyObject *
Match_get_pos(MatchObject *self, void *closure)
{
    return PyLong_FromSsize_t(self->public_pos);
}

static PyObject *
Match_get_endpos(MatchObject *self, void *closure)
{
    return PyLong_FromSsize_t(self->public_endpos);
}

static PyObject *
Match_get_lastindex(MatchObject *self, void *closure)
{
    if (self->ovec_count <= 1) {
        Py_RETURN_NONE;
    }

    for (Py_ssize_t index = (Py_ssize_t)self->ovec_count - 1; index >= 1; --index) {
        Py_ssize_t start = self->ovector[(size_t)index * 2];
        Py_ssize_t end = self->ovector[(size_t)index * 2 + 1];
        if (start >= 0 && end >= 0) {
            return PyLong_FromSsize_t(index);
        }
    }

    Py_RETURN_NONE;
}

static PyObject *
Match_get_lastgroup(MatchObject *self, void *closure)
{
    PyObject *lastindex_obj = Match_get_lastindex(self, closure);
    if (lastindex_obj == NULL || lastindex_obj == Py_None) {
        return lastindex_obj;
    }

    PyObject *key = NULL;
    PyObject *value = NULL;
    Py_ssize_t pos = 0;
    while (PyDict_Next(self->pattern->groupindex, &pos, &key, &value)) {
        int matches = PyObject_RichCompareBool(value, lastindex_obj, Py_EQ);
        if (matches < 0) {
            Py_DECREF(lastindex_obj);
            return NULL;
        }
        if (matches) {
            Py_INCREF(key);
            Py_DECREF(lastindex_obj);
            return key;
        }
    }

    Py_DECREF(lastindex_obj);
    Py_RETURN_NONE;
}

static PyObject *
Match_get_regs(MatchObject *self, void *closure)
{
    PyObject *result = PyTuple_New(self->ovec_count);
    if (result == NULL) {
        return NULL;
    }

    for (uint32_t index = 0; index < self->ovec_count; ++index) {
        Py_ssize_t start = 0;
        Py_ssize_t end = 0;
        if (match_resolve_span(self, (Py_ssize_t)index, &start, &end, 1) < 0) {
            Py_DECREF(result);
            return NULL;
        }
        PyObject *span = Py_BuildValue("(nn)", start, end);
        if (span == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyTuple_SET_ITEM(result, index, span);
    }

    return result;
}

static PyObject *
Match_expand(MatchObject *self, PyObject *template_obj)
{
    /* Delegate template parsing to the Python compatibility helper. */
    PyObject *module = PyImport_ImportModule("pcre.re_compat");
    if (module == NULL) {
        return NULL;
    }

    PyObject *helper = PyObject_GetAttrString(module, "expand_match_template");
    Py_DECREF(module);
    if (helper == NULL) {
        return NULL;
    }

    PyObject *result = PyObject_CallFunctionObjArgs(
        helper,
        (PyObject *)self,
        template_obj,
        NULL
    );
    Py_DECREF(helper);
    return result;
}

static int
match_set_public_pattern(MatchObject *self, PyObject *public_pattern)
{
    /* The high-level wrapper reuses this C object and swaps in its owner here. */
    if (public_pattern == NULL) {
        Py_XDECREF(self->public_pattern);
        self->public_pattern = NULL;
        return 0;
    }

    Py_INCREF(public_pattern);
    Py_XDECREF(self->public_pattern);
    self->public_pattern = public_pattern;
    return 0;
}

static PyMethodDef Match_methods[] = {
    {"group", (PyCFunction)Match_group, METH_VARARGS, PyDoc_STR("Return one or more capture groups.")},
    {"groups", (PyCFunction)Match_groups, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return all capture groups as a tuple." )},
    {"groupdict", (PyCFunction)Match_groupdict, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return a dict for named capture groups." )},
    {"span", (PyCFunction)Match_span, METH_VARARGS, PyDoc_STR("Return the (start, end) span for a group." )},
    {"start", (PyCFunction)Match_start, METH_VARARGS, PyDoc_STR("Return the start index for a group." )},
    {"end", (PyCFunction)Match_end, METH_VARARGS, PyDoc_STR("Return the end index for a group." )},
    {"expand", (PyCFunction)Match_expand, METH_O, PyDoc_STR("Apply a replacement template to the match." )},
    {NULL, NULL, 0, NULL},
};

static PyGetSetDef Match_getset[] = {
    {"re", (getter)Match_get_re, NULL, PyDoc_STR("Pattern object used for the match."), NULL},
    {"string", (getter)Match_get_string, NULL, PyDoc_STR("Original subject."), NULL},
    {"pos", (getter)Match_get_pos, NULL, PyDoc_STR("Original search start position."), NULL},
    {"endpos", (getter)Match_get_endpos, NULL, PyDoc_STR("Original search end position."), NULL},
    {"lastindex", (getter)Match_get_lastindex, NULL, PyDoc_STR("Index of the last matched capturing group."), NULL},
    {"lastgroup", (getter)Match_get_lastgroup, NULL, PyDoc_STR("Name of the last matched capturing group."), NULL},
    {"regs", (getter)Match_get_regs, NULL, PyDoc_STR("Tuple of span pairs for the whole match and each group."), NULL},
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
    Py_ssize_t origin_pos;
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
    PyObject *public_pattern;
} FindIterObject;

/*
 * Iteration over Unicode subjects frequently needs byte<->code-point
 * conversions. These caches keep the common forward-only scan cheap rather than
 * rescanning the full subject for every match.
 */
static MatchObject *create_match_object(PatternObject *pattern,
                                        PyObject *subject_obj,
                                        PyObject *utf8_owner,
                                        const char *utf8_data,
                                        Py_ssize_t utf8_length,
                                        Py_ssize_t pos,
                                        Py_ssize_t endpos,
                                        uint32_t ovec_count,
                                        PCRE2_SIZE *ovector);


static inline Py_ssize_t
utf8_index_to_offset_fast(const char *data, Py_ssize_t data_len, Py_ssize_t index)
{
    /* Walk UTF-8 once, collapsing ASCII runs so index->byte conversion stays cheap. */
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
    /* Convert a byte offset back to a code-point index using the forward cache. */
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
    /* Convert a code-point index to a byte offset using the forward cache. */
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
    Py_XDECREF(self->public_pattern);
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
        PCRE2_CALL_MAYBE_RELEASE_GIL(pcre2_jit_match(self->pattern->code,
                                                     (PCRE2_SPTR)buffer,
                                                     exec_length,
                                                     (PCRE2_SIZE)self->current_byte,
                                                     options,
                                                     self->match_data,
                                                     self->match_context),
                                                   exec_length);

        if (rc == PCRE2_ERROR_JIT_BADOPTION || rc == PCRE2_ERROR_BADOPTION) {
            pattern_jit_set(self->pattern, 0);
            if (self->jit_stack != NULL) {
                if (self->match_context != NULL) {
                    pcre2_jit_stack_assign(self->match_context, NULL, NULL);
                }
                jit_stack_cache_release(self->jit_stack);
                self->jit_stack = NULL;
            }
        } else if (rc == PCRE2_ERROR_NOMATCH) {
            self->exhausted = 1;
            return NULL;
        } else if (rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(self->match_data);
            raise_pcre_error("jit_match", rc, error_offset);
            return NULL;
        } else if (rc >= 0) {
            goto matched;
        }
    }

    if (!pattern_jit_get(self->pattern)) {
        PCRE2_CALL_MAYBE_RELEASE_GIL(pcre2_match(self->pattern->code,
                                                 (PCRE2_SPTR)buffer,
                                                 exec_length,
                                                 (PCRE2_SIZE)self->current_byte,
                                                 options,
                                                 self->match_data,
                                                 self->match_context),
                                               exec_length);

        if (rc == PCRE2_ERROR_NOMATCH) {
            self->exhausted = 1;
            return NULL;
        }

        if (rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(self->match_data);
            raise_pcre_error("match", rc, error_offset);
            return NULL;
        }
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
        self->origin_pos,
        self->resolved_end,
        (uint32_t)expected_pairs,
        ovector);
    if (match == NULL) {
        return NULL;
    }

    if (self->public_pattern != NULL) {
        if (match_set_public_pattern(match, self->public_pattern) < 0) {
            Py_DECREF(match);
            return NULL;
        }
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
                    Py_ssize_t pos,
                    Py_ssize_t endpos,
                    uint32_t ovec_count,
                    PCRE2_SIZE *ovector)
{
    /*
     * Materialize a standalone match snapshot. The ovector is copied because
     * PCRE2 reuses match-data buffers from caches across calls and threads.
     */
    MatchObject *match = PyObject_New(MatchObject, &MatchType);
    if (match == NULL) {
        return NULL;
    }

    if (ovec_count == 0) {
        ovec_count = 1;
    }
    if (ovec_count > (SIZE_MAX / sizeof(Py_ssize_t) / 2)) {
        PyErr_NoMemory();
        PyObject_Del(match);
        return NULL;
    }
    size_t alloc_pairs = (size_t)ovec_count * 2;
    match->ovector = pcre_malloc(alloc_pairs * sizeof(Py_ssize_t));
    if (match->ovector == NULL) {
        PyErr_NoMemory();
        PyObject_Del(match);
        return NULL;
    }
    if (ovector == NULL) {
        PyErr_NoMemory();
        pcre_free(match->ovector);
        PyObject_Del(match);
        return NULL;
    }

    for (size_t i = 0; i < alloc_pairs; ++i) {
        match->ovector[i] = (Py_ssize_t)ovector[i];
    }
    match->ovec_count = ovec_count;

    Py_INCREF(pattern);
    match->pattern = pattern;
    match->public_pattern = NULL;

    Py_INCREF(subject_obj);
    match->subject = subject_obj;

    Py_INCREF(utf8_owner);
    match->utf8_owner = utf8_owner;
    match->utf8_data = utf8_data;
    match->utf8_length = utf8_length;
    match->public_pos = pos;
    match->public_endpos = endpos;
    /* Anything that isn't str (bytes, or a buffer-protocol object such as
       mmap.mmap) is treated as raw byte data: offsets are byte offsets and
       group values are returned as bytes. */
    match->subject_is_bytes = !PyUnicode_Check(subject_obj);

    return match;
}

static PyObject *
Pattern_create_finditer(PatternObject *pattern,
                        PyObject *subject_obj,
                        Py_ssize_t pos,
                        Py_ssize_t endpos,
                        uint32_t options,
                        PyObject *public_pattern)
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
    iter->origin_pos = 0;
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
    iter->public_pattern = NULL;

    if (public_pattern != NULL && public_pattern != Py_None) {
        Py_INCREF(public_pattern);
        iter->public_pattern = public_pattern;
    }

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
    } else if (PyObject_CheckBuffer(subject_obj)) {
        /* Zero-copy path for e.g. mmap.mmap: get a pointer straight into the
           exporter's own storage instead of copying it into a new object. */
        const char *buf_data = NULL;
        Py_ssize_t buf_length = 0;
        PyObject *buf_view = buffer_view_from_object(subject_obj, &buf_data, &buf_length);
        if (buf_view == NULL) {
            goto error;
        }
        iter->subject_is_bytes = 1;
        iter->subject_length_bytes = buf_length;
        iter->logical_length = buf_length;
        iter->utf8_data = buf_data;
        iter->utf8_owner = buf_view;
        if (ensure_valid_utf8_for_bytes_subject(pattern,
                                                iter->subject_is_bytes,
                                                iter->utf8_data,
                                                iter->subject_length_bytes) < 0) {
            goto error;
        }
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "subject must be str, bytes, or a bytes-like buffer object (e.g. mmap.mmap)");
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
    iter->origin_pos = pos;
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
    /*
     * The UTF-8 validity of the subject has already been established either by
     * Python (str) or by ensure_valid_utf8_for_bytes_subject (bytes/buffer+UTF).
     * Only skip PCRE2's UTF-8 check for the full validated range so partial
     * byte ranges cannot end inside a multi-byte sequence.
     */
    if (!iter->subject_is_bytes ||
        (pos == 0 && resolved_end == iter->logical_length)) {
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
    Py_XDECREF(iter->public_pattern);
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
    static char *kwlist[] = {"subject", "pos", "endpos", "options", "owner", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnkO", kwlist,
                                     &subject, &pos, &endpos, &options, &owner)) {
        return NULL;
    }

    return Pattern_create_finditer(self, subject, pos, endpos, (uint32_t)options, owner);
}

static PyObject *
Pattern_execute(PatternObject *self, PyObject *subject_obj, Py_ssize_t pos,
                Py_ssize_t endpos, uint32_t options, execute_mode mode,
                PyObject *public_pattern)
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
    } else if (PyObject_CheckBuffer(subject_obj)) {
        /* Zero-copy path for e.g. mmap.mmap: get a pointer straight into the
           exporter's own storage instead of copying it into a new object. */
        const char *buf_data = NULL;
        Py_ssize_t buf_length = 0;
        PyObject *buf_view = buffer_view_from_object(subject_obj, &buf_data, &buf_length);
        if (buf_view == NULL) {
            return NULL;
        }
        utf8_owner = buf_view;
        buffer = buf_data;
        subject_length_bytes = buf_length;
        logical_length = buf_length;
        subject_is_bytes = 1;
        if (ensure_valid_utf8_for_bytes_subject(self,
                                                subject_is_bytes,
                                                buffer,
                                                subject_length_bytes) < 0) {
            Py_DECREF(utf8_owner);
            return NULL;
        }
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "expected str, bytes, or a bytes-like buffer object (e.g. mmap.mmap)");
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
    /*
     * For text subjects we already own a UTF-8 pointer that Python validated.
     * For bytes/buffer-protocol subjects with PCRE2_UTF we explicitly validated
     * UTF-8 above. Only skip the PCRE2 UTF-8 check when the entire validated
     * buffer is used; partial byte ranges may end inside a multi-byte sequence.
     */
    if (!subject_is_bytes ||
        (byte_start == 0 && byte_end == subject_length_bytes)) {
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
    int jit_endanchor_uncertain = 0;
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

        PCRE2_CALL_MAYBE_RELEASE_GIL(pcre2_jit_match(self->code,
                                                     (PCRE2_SPTR)buffer,
                                                     exec_length,
                                                     (PCRE2_SIZE)byte_start,
                                                     match_options,
                                                     match_data,
                                                     match_context),
                                                   exec_length);

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
        } else if (jit_anchor_fixup_needed() && rc >= 0 &&
                   (mode == EXEC_MODE_MATCH || mode == EXEC_MODE_FULLMATCH)) {
            /*
             * Some PCRE2 builds' pcre2_jit_match() silently ignore
             * PCRE2_ANCHORED and PCRE2_ENDANCHORED as match-time options.
             * Detected at module load; only apply the workaround when
             * the linked library is non-compliant.
             */
            PCRE2_SIZE *jit_ovector = pcre2_get_ovector_pointer(match_data);
            if (jit_ovector == NULL || jit_ovector[0] != (PCRE2_SIZE)byte_start) {
                rc = PCRE2_ERROR_NOMATCH;
            } else if (mode == EXEC_MODE_FULLMATCH && jit_ovector[1] != offset_limit) {
                jit_endanchor_uncertain = 1;
            }
        }
    }

    if (!pattern_jit_get(self) || jit_endanchor_uncertain) {
        /*
         * For the fullmatch JIT fallback, truncate the interpreter re-run
         * to the requested endpos (offset_limit). This guarantees that
         * PCRE2_ENDANCHORED anchors to the intended boundary even on PCRE2
         * builds where PCRE2_USE_OFFSET_LIMIT does not influence end
         * anchoring for the interpreter re-run.
         */
        PCRE2_SIZE interpreter_length = exec_length;
        if (jit_endanchor_uncertain) {
            interpreter_length = offset_limit;
            if (interpreter_length < (PCRE2_SIZE)byte_start) {
                interpreter_length = (PCRE2_SIZE)byte_start;
            }
        }
        PCRE2_CALL_MAYBE_RELEASE_GIL(pcre2_match(self->code,
                                                 (PCRE2_SPTR)buffer,
                                                 interpreter_length,
                                                 (PCRE2_SIZE)byte_start,
                                                 match_options,
                                                 match_data,
                                                 match_context),
                                               interpreter_length);
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
        pos,
        adjusted_endpos >= 0 ? adjusted_endpos : logical_length,
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

    if (public_pattern != NULL && public_pattern != Py_None) {
        if (match_set_public_pattern(match, public_pattern) < 0) {
            Py_DECREF(match);
            Py_DECREF(utf8_owner);
            return NULL;
        }
    }

    Py_DECREF(utf8_owner);
    return (PyObject *)match;
}

static PyObject *
Pattern_match_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", "owner", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnkO", kwlist,
                                     &subject, &pos, &endpos, &options, &owner)) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, (uint32_t)options, EXEC_MODE_MATCH, owner);
}

static PyObject *
Pattern_search_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", "owner", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnkO", kwlist,
                                     &subject, &pos, &endpos, &options, &owner)) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, (uint32_t)options, EXEC_MODE_SEARCH, owner);
}

static PyObject *
Pattern_fullmatch_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", "owner", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    unsigned long options = 0;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnkO", kwlist,
                                     &subject, &pos, &endpos, &options, &owner)) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, (uint32_t)options, EXEC_MODE_FULLMATCH, owner);
}

static inline PyObject *
findall_build_value_from_ovector(PyObject *subject_obj,
                                 const char *utf8_data,
                                 int subject_is_bytes,
                                 int subject_is_ascii,
                                 PCRE2_SIZE *ovector,
                                 uint32_t ovec_count)
{
    /*
     * Reproduce Python findall() semantics from raw PCRE2 ovector:
     *   no groups        -> full match
     *   one group        -> value of group(1)
     *   multiple groups  -> tuple of groups
     */
    if (ovec_count <= 1) {
        Py_ssize_t start = (Py_ssize_t)ovector[0];
        Py_ssize_t end = (Py_ssize_t)ovector[1];
        return extract_value_from_offsets(subject_obj, utf8_data, subject_is_bytes,
                                            subject_is_ascii, start, end);
    }
    if (ovec_count == 2) {
        Py_ssize_t start = (Py_ssize_t)ovector[2];
        Py_ssize_t end = (Py_ssize_t)ovector[3];
        return extract_value_from_offsets(subject_obj, utf8_data, subject_is_bytes,
                                            subject_is_ascii, start, end);
    }

    PyObject *tuple = PyTuple_New((Py_ssize_t)ovec_count - 1);
    if (tuple == NULL) {
        return NULL;
    }
    for (uint32_t i = 1; i < ovec_count; ++i) {
        Py_ssize_t start = (Py_ssize_t)ovector[(size_t)i * 2];
        Py_ssize_t end = (Py_ssize_t)ovector[(size_t)i * 2 + 1];
        PyObject *value = extract_value_from_offsets(subject_obj, utf8_data, subject_is_bytes,
                                                      subject_is_ascii, start, end);
        if (value == NULL) {
            Py_DECREF(tuple);
            return NULL;
        }
        PyTuple_SET_ITEM(tuple, (Py_ssize_t)i - 1, value);
    }
    return tuple;
}

static PyObject *
Pattern_findall(PatternObject *self,
                PyObject *subject_obj,
                Py_ssize_t pos,
                Py_ssize_t endpos,
                uint32_t options)
{
    PyObject *result = NULL;
    pcre2_match_data *match_data = NULL;
    pcre2_match_context *match_context = NULL;
    pcre2_jit_stack *jit_stack = NULL;
    int match_data_from_pattern = 0;
    int match_context_from_pattern = 0;
    int match_context_used_offset_limit = 0;

    PyObject *utf8_owner = NULL;
    const char *utf8_data = NULL;
    Py_ssize_t subject_length_bytes = 0;
    Py_ssize_t logical_length = 0;
    int subject_is_bytes = PyBytes_Check(subject_obj);
    int subject_is_ascii = 0;

    if (subject_is_bytes) {
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        utf8_data = PyBytes_AS_STRING(subject_obj);
        subject_length_bytes = PyBytes_GET_SIZE(subject_obj);
        logical_length = subject_length_bytes;
        if (ensure_valid_utf8_for_bytes_subject(self,
                                                subject_is_bytes,
                                                utf8_data,
                                                subject_length_bytes) < 0) {
            goto error;
        }
    } else if (PyUnicode_Check(subject_obj)) {
        if (PyUnicode_READY(subject_obj) < 0) {
            goto error;
        }
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        logical_length = PyUnicode_GET_LENGTH(subject_obj);
        if (PyUnicode_IS_ASCII(subject_obj)) {
            subject_is_ascii = 1;
            utf8_data = (const char *)PyUnicode_1BYTE_DATA(subject_obj);
            subject_length_bytes = logical_length;
        } else {
            const char *data = PyUnicode_AsUTF8AndSize(subject_obj, &subject_length_bytes);
            if (data == NULL) {
                goto error;
            }
            utf8_data = data;
        }
    } else if (PyObject_CheckBuffer(subject_obj)) {
        const char *buf_data = NULL;
        Py_ssize_t buf_length = 0;
        PyObject *buf_view = buffer_view_from_object(subject_obj, &buf_data, &buf_length);
        if (buf_view == NULL) {
            goto error;
        }
        utf8_owner = buf_view;
        utf8_data = buf_data;
        subject_length_bytes = buf_length;
        logical_length = buf_length;
        subject_is_bytes = 1;
        if (ensure_valid_utf8_for_bytes_subject(self,
                                                subject_is_bytes,
                                                utf8_data,
                                                subject_length_bytes) < 0) {
            goto error;
        }
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "subject must be str, bytes, or a bytes-like buffer object (e.g. mmap.mmap)");
        goto error;
    }

    if (pos < 0) {
        pos += logical_length;
        if (pos < 0) {
            pos = 0;
        }
    }
    if (pos > logical_length) {
        pos = logical_length;
    }

    int has_endpos = 0;
    Py_ssize_t adjusted_endpos = -1;
    if (endpos >= 0) {
        has_endpos = 1;
        adjusted_endpos = endpos;
        if (adjusted_endpos > logical_length) {
            adjusted_endpos = logical_length;
        }
        if (adjusted_endpos < pos) {
            PyErr_SetString(PyExc_ValueError, "endpos must be >= pos");
            goto error;
        }
    }

    int treat_as_bytes = subject_is_bytes || subject_is_ascii;
    Py_ssize_t byte_start = 0;
    Py_ssize_t byte_end = subject_length_bytes;

    if (treat_as_bytes) {
        byte_start = pos;
        if (has_endpos) {
            byte_end = adjusted_endpos;
        }
    } else {
        if (pos == 0) {
            byte_start = 0;
        } else if (pos == logical_length) {
            byte_start = subject_length_bytes;
        } else if (utf8_index_to_offset(subject_obj, pos, &byte_start) < 0) {
            goto error;
        }

        if (has_endpos) {
            if (adjusted_endpos == logical_length) {
                byte_end = subject_length_bytes;
            } else if (utf8_index_to_offset(subject_obj, adjusted_endpos, &byte_end) < 0) {
                goto error;
            }
        }
    }

    if (byte_start > byte_end || byte_start < 0 || byte_end < 0 || byte_end > subject_length_bytes) {
        PyErr_SetString(PyExc_ValueError, "byte offset mismatch for subject");
        goto error;
    }

    match_data = pattern_match_data_acquire(self, &match_data_from_pattern);
    if (match_data == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    int attempt_jit = pattern_jit_get(self);
    int need_offset_limit = (has_endpos && byte_end != subject_length_bytes);
#if defined(PCRE2_USE_OFFSET_LIMIT)
    int use_offset_limit = need_offset_limit && offset_limit_option_enabled();
#else
    int use_offset_limit = 0;
#endif
    PCRE2_SIZE offset_limit = (PCRE2_SIZE)byte_end;

    if (attempt_jit || use_offset_limit) {
        match_context = pattern_match_context_acquire(self, use_offset_limit, &match_context_from_pattern);
        if (match_context == NULL) {
            PyErr_NoMemory();
            goto error;
        }
    }

#if defined(PCRE2_USE_OFFSET_LIMIT)
    if (use_offset_limit) {
        int ctx_rc = pcre2_set_offset_limit(match_context, offset_limit);
        if (ctx_rc < 0) {
            raise_pcre_error("set_offset_limit", ctx_rc, 0);
            goto error;
        }
        options |= PCRE2_USE_OFFSET_LIMIT;
        match_context_used_offset_limit = 1;
    } else
#endif
    if (need_offset_limit) {
        if (offset_limit < (PCRE2_SIZE)byte_start) {
            offset_limit = (PCRE2_SIZE)byte_start;
        }
    }

    if (attempt_jit) {
        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            PyErr_NoMemory();
            goto error;
        }
        pcre2_jit_stack_assign(match_context, NULL, jit_stack);
    }

    uint32_t match_options = options;
    if (!subject_is_bytes || (byte_start == 0 && byte_end == subject_length_bytes)) {
        match_options |= PCRE2_NO_UTF_CHECK;
    }

    result = PyList_New(0);
    if (result == NULL) {
        goto error;
    }

    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    if (ovector == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "PCRE2 returned empty match data");
        goto error;
    }
    uint32_t available_pairs = pcre2_get_ovector_count(match_data);
    uint64_t expected_pairs = (uint64_t)self->capture_count + 1;
    if (expected_pairs == 0 || expected_pairs > available_pairs) {
        expected_pairs = available_pairs;
    }
    if (expected_pairs == 0) {
        PyErr_SetString(PyExc_RuntimeError, "PCRE2 returned empty match data");
        goto error;
    }

    PCRE2_SIZE exec_length = (PCRE2_SIZE)subject_length_bytes;
    if (need_offset_limit && !use_offset_limit) {
        exec_length = offset_limit;
        if (exec_length < (PCRE2_SIZE)byte_start) {
            exec_length = (PCRE2_SIZE)byte_start;
        }
    }

    Py_ssize_t current_byte = byte_start;
    Py_ssize_t current_pos = pos;

    while (1) {
        if (current_byte > subject_length_bytes) {
            break;
        }
        if (has_endpos && current_pos >= adjusted_endpos) {
            break;
        }
        if (!has_endpos && current_pos > logical_length) {
            break;
        }
        if (has_endpos && current_byte >= byte_end) {
            break;
        }

        int rc = 0;
        int use_jit = attempt_jit;

        if (use_jit) {
            rc = pcre2_jit_match(self->code,
                                 (PCRE2_SPTR)utf8_data,
                                 exec_length,
                                 (PCRE2_SIZE)current_byte,
                                 match_options,
                                 match_data,
                                 match_context);

            if (rc == PCRE2_ERROR_JIT_BADOPTION || rc == PCRE2_ERROR_BADOPTION) {
                pattern_jit_set(self, 0);
                attempt_jit = 0;
                if (match_context != NULL) {
                    pcre2_jit_stack_assign(match_context, NULL, NULL);
                }
                if (jit_stack != NULL) {
                    jit_stack_cache_release(jit_stack);
                    jit_stack = NULL;
                }
                use_jit = 0;
            } else if (rc == PCRE2_ERROR_NOMATCH) {
                break;
            } else if (rc < 0) {
                PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
                raise_pcre_error("jit_match", rc, error_offset);
                goto error;
            }
        }

        if (!use_jit) {
            rc = pcre2_match(self->code,
                             (PCRE2_SPTR)utf8_data,
                             exec_length,
                             (PCRE2_SIZE)current_byte,
                             match_options,
                             match_data,
                             match_context);

            if (rc == PCRE2_ERROR_NOMATCH) {
                break;
            }
            if (rc < 0) {
                PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
                raise_pcre_error("match", rc, error_offset);
                goto error;
            }
        }

        Py_ssize_t start_byte = (Py_ssize_t)ovector[0];
        Py_ssize_t end_byte = (Py_ssize_t)ovector[1];

        PyObject *value = findall_build_value_from_ovector(
            subject_obj,
            utf8_data,
            subject_is_bytes,
            subject_is_ascii,
            ovector,
            (uint32_t)expected_pairs);
        if (value == NULL) {
            goto error;
        }
        if (PyList_Append(result, value) < 0) {
            Py_DECREF(value);
            goto error;
        }
        Py_DECREF(value);

        Py_ssize_t end_index;
        if (treat_as_bytes) {
            end_index = end_byte;
        } else {
            Py_ssize_t matched_len = end_byte - current_byte;
            if (matched_len < 0) {
                matched_len = 0;
            }
            end_index = current_pos + utf8_offset_to_index(utf8_data + current_byte, matched_len);
        }

        Py_ssize_t next_pos = end_index;
        if (has_endpos && next_pos >= adjusted_endpos) {
            next_pos = end_index;
        } else if (start_byte == end_byte) {
            next_pos = end_index + 1;
        }

        if (next_pos <= current_pos) {
            next_pos = current_pos + 1;
        }

        Py_ssize_t next_byte = current_byte;
        if (treat_as_bytes) {
            next_byte = next_pos;
            if (next_byte > subject_length_bytes) {
                next_byte = subject_length_bytes;
            }
            if (next_byte < 0) {
                next_byte = 0;
            }
        } else {
            if (next_pos >= logical_length) {
                next_byte = subject_length_bytes;
            } else if (next_pos <= 0) {
                next_byte = 0;
            } else if (start_byte != end_byte) {
                next_byte = end_byte;
            } else {
                next_byte = end_byte;
                if (next_byte < subject_length_bytes) {
                    unsigned char lead = (unsigned char)utf8_data[next_byte];
                    Py_ssize_t char_bytes = 1;
                    if ((lead & 0xE0) == 0xC0) {
                        char_bytes = 2;
                    } else if ((lead & 0xF0) == 0xE0) {
                        char_bytes = 3;
                    } else if ((lead & 0xF8) == 0xF0) {
                        char_bytes = 4;
                    }
                    if (next_byte + char_bytes > subject_length_bytes) {
                        char_bytes = subject_length_bytes - next_byte;
                    }
                    next_byte += char_bytes;
                }
            }
        }

        current_pos = next_pos;
        current_byte = next_byte;
    }

    goto cleanup;

error:
    Py_XDECREF(result);
    result = NULL;

cleanup:
    if (jit_stack != NULL) {
        if (match_context != NULL) {
            pcre2_jit_stack_assign(match_context, NULL, NULL);
        }
        jit_stack_cache_release(jit_stack);
    }
    if (match_context != NULL) {
        pattern_match_context_release(self, match_context, match_context_used_offset_limit, match_context_from_pattern);
    }
    if (match_data != NULL) {
        pattern_match_data_release(self, match_data, match_data_from_pattern);
    }
    Py_XDECREF(utf8_owner);
    return result;
}

static PyObject *
Pattern_findall_method(PatternObject *self, PyObject *args, PyObject *kwargs)
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

    return Pattern_findall(self, subject, pos, endpos, (uint32_t)options);
}

static PyObject *
Pattern_substitute(PatternObject *self,
                   PyObject *subject_obj,
                   PyObject *repl_obj,
                   Py_ssize_t count)
{
    PyObject *result = NULL;
    PyObject *result_tuple = NULL;
    PyObject *utf8_owner = NULL;
    const char *subject_data = NULL;
    Py_ssize_t subject_length = 0;
    int subject_is_bytes = 0;
    int subject_is_ascii = 0;

    const char *repl_data = NULL;
    Py_ssize_t repl_length = 0;
    int repl_is_bytes = 0;

    pcre2_match_data *match_data = NULL;
    pcre2_match_context *match_context = NULL;
    pcre2_jit_stack *jit_stack = NULL;
    int match_data_from_pattern = 0;
    int match_context_from_pattern = 0;

    if (count != 0) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (PyBytes_Check(repl_obj)) {
        repl_is_bytes = 1;
        repl_data = PyBytes_AS_STRING(repl_obj);
        repl_length = PyBytes_GET_SIZE(repl_obj);
    } else if (PyUnicode_Check(repl_obj)) {
        if (PyUnicode_READY(repl_obj) < 0) {
            goto error;
        }
        if (PyUnicode_IS_ASCII(repl_obj)) {
            repl_data = (const char *)PyUnicode_1BYTE_DATA(repl_obj);
            repl_length = PyUnicode_GET_LENGTH(repl_obj);
        } else {
            repl_data = PyUnicode_AsUTF8AndSize(repl_obj, &repl_length);
            if (repl_data == NULL) {
                goto error;
            }
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "replacement must be str or bytes");
        return NULL;
    }

    if (PyBytes_Check(subject_obj)) {
        subject_is_bytes = 1;
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        subject_data = PyBytes_AS_STRING(subject_obj);
        subject_length = PyBytes_GET_SIZE(subject_obj);
        if (ensure_valid_utf8_for_bytes_subject(self,
                                              subject_is_bytes,
                                              subject_data,
                                              subject_length) < 0) {
            goto error;
        }
    } else if (PyObject_CheckBuffer(subject_obj)) {
        const char *buf_data = NULL;
        Py_ssize_t buf_length = 0;
        PyObject *buf_view = buffer_view_from_object(subject_obj, &buf_data, &buf_length);
        if (buf_view == NULL) {
            return NULL;
        }
        subject_is_bytes = 1;
        utf8_owner = buf_view;
        subject_data = buf_data;
        subject_length = buf_length;
        if (ensure_valid_utf8_for_bytes_subject(self,
                                              subject_is_bytes,
                                              subject_data,
                                              subject_length) < 0) {
            goto error;
        }
    } else if (PyUnicode_Check(subject_obj)) {
        if (PyUnicode_READY(subject_obj) < 0) {
            goto error;
        }
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        if (PyUnicode_IS_ASCII(subject_obj)) {
            subject_is_ascii = 1;
            subject_data = (const char *)PyUnicode_1BYTE_DATA(subject_obj);
            subject_length = PyUnicode_GET_LENGTH(subject_obj);
        } else {
            subject_data = PyUnicode_AsUTF8AndSize(subject_obj, &subject_length);
            if (subject_data == NULL) {
                goto error;
            }
        }
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "subject must be str, bytes, or a bytes-like buffer object (e.g. mmap.mmap)");
        return NULL;
    }

    if (subject_is_bytes != repl_is_bytes) {
        PyErr_SetString(PyExc_TypeError,
                        "replacement must be the same type as the subject");
        goto error;
    }

    match_data = pattern_match_data_acquire(self, &match_data_from_pattern);
    if (match_data == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    if (pattern_jit_get(self)) {
        match_context = pattern_match_context_acquire(self, 0, &match_context_from_pattern);
        if (match_context == NULL) {
            PyErr_NoMemory();
            goto error;
        }
        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            PyErr_NoMemory();
            goto error;
        }
        pcre2_jit_stack_assign(match_context, NULL, jit_stack);
    }

    uint32_t sub_options = PCRE2_SUBSTITUTE_GLOBAL
                         | PCRE2_SUBSTITUTE_EXTENDED
                         | PCRE2_SUBSTITUTE_UNSET_EMPTY
                         | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
    if (!subject_is_bytes) {
        sub_options |= PCRE2_NO_UTF_CHECK;
    }

    PCRE2_SIZE outlen = (PCRE2_SIZE)(subject_length + repl_length + 16);
    if (outlen < (PCRE2_SIZE)subject_length) {
        PyErr_NoMemory();
        goto error;
    }
    PCRE2_UCHAR *out = (PCRE2_UCHAR *)PyMem_Malloc(outlen);
    if (out == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (int attempts = 0; attempts < 5; ++attempts) {
        int rc = pcre2_substitute(self->code,
                                  (PCRE2_SPTR)subject_data,
                                  (PCRE2_SIZE)subject_length,
                                  0,
                                  sub_options,
                                  match_data,
                                  match_context,
                                  (PCRE2_SPTR)repl_data,
                                  (PCRE2_SIZE)repl_length,
                                  out,
                                  &outlen);
        if (rc == PCRE2_ERROR_NOMEMORY) {
            PCRE2_SIZE required = outlen;
            if (required == (PCRE2_SIZE)-1) {
                required = (PCRE2_SIZE)(subject_length + repl_length + 16) + (PCRE2_SIZE)subject_length;
            }
            if (required < (PCRE2_SIZE)(subject_length + repl_length + 16)) {
                required = (PCRE2_SIZE)(subject_length + repl_length + 16);
            }
            void *new_out = PyMem_Realloc(out, required);
            if (new_out == NULL) {
                PyMem_Free(out);
                PyErr_NoMemory();
                goto error;
            }
            out = (PCRE2_UCHAR *)new_out;
            outlen = required;
            continue;
        }
        if (rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
            PyMem_Free(out);
            raise_pcre_error("substitute", rc, error_offset);
            goto error;
        }

        PyObject *out_obj = NULL;
        if (subject_is_bytes) {
            out_obj = PyBytes_FromStringAndSize((const char *)out, (Py_ssize_t)outlen);
        } else if (subject_is_ascii) {
            out_obj = PyUnicode_New((Py_ssize_t)outlen, 127);
            if (out_obj != NULL) {
                memcpy(PyUnicode_1BYTE_DATA(out_obj), out, (size_t)outlen);
            }
        } else {
            out_obj = PyUnicode_DecodeUTF8((const char *)out, (Py_ssize_t)outlen, "strict");
        }
        PyMem_Free(out);
        if (out_obj == NULL) {
            goto error;
        }

        result_tuple = PyTuple_New(2);
        if (result_tuple == NULL) {
            Py_DECREF(out_obj);
            goto error;
        }
        PyObject *count_obj = PyLong_FromLong((long)rc);
        if (count_obj == NULL) {
            Py_DECREF(out_obj);
            Py_DECREF(result_tuple);
            goto error;
        }
        PyTuple_SET_ITEM(result_tuple, 0, out_obj);
        PyTuple_SET_ITEM(result_tuple, 1, count_obj);
        result = result_tuple;
        goto cleanup;
    }

    PyMem_Free(out);
    PyErr_NoMemory();

error:
    Py_XDECREF(result);
    result = NULL;

cleanup:
    if (jit_stack != NULL) {
        if (match_context != NULL) {
            pcre2_jit_stack_assign(match_context, NULL, NULL);
        }
        jit_stack_cache_release(jit_stack);
    }
    if (match_context != NULL) {
        pattern_match_context_release(self, match_context, 0, match_context_from_pattern);
    }
    if (match_data != NULL) {
        pattern_match_data_release(self, match_data, match_data_from_pattern);
    }
    Py_XDECREF(utf8_owner);
    return result;
}

static PyObject *
Pattern_substitute_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "replacement", "count", NULL};
    PyObject *subject = NULL;
    PyObject *replacement = NULL;
    Py_ssize_t count = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOn", kwlist,
                                     &subject, &replacement, &count)) {
        return NULL;
    }

    return Pattern_substitute(self, subject, replacement, count);
}

static PyObject *
Pattern_split(PatternObject *self,
              PyObject *subject_obj,
              Py_ssize_t maxsplit)
{
    PyObject *result = NULL;
    pcre2_match_data *match_data = NULL;
    pcre2_match_context *match_context = NULL;
    pcre2_jit_stack *jit_stack = NULL;
    int match_data_from_pattern = 0;
    int match_context_from_pattern = 0;
    int match_context_used_offset_limit = 0;

    PyObject *utf8_owner = NULL;
    const char *utf8_data = NULL;
    Py_ssize_t subject_length_bytes = 0;
    Py_ssize_t logical_length = 0;
    int subject_is_bytes = PyBytes_Check(subject_obj);
    int subject_is_ascii = 0;

    if (subject_is_bytes) {
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        utf8_data = PyBytes_AS_STRING(subject_obj);
        subject_length_bytes = PyBytes_GET_SIZE(subject_obj);
        logical_length = subject_length_bytes;
        if (ensure_valid_utf8_for_bytes_subject(self,
                                                subject_is_bytes,
                                                utf8_data,
                                                subject_length_bytes) < 0) {
            goto error;
        }
    } else if (PyUnicode_Check(subject_obj)) {
        if (PyUnicode_READY(subject_obj) < 0) {
            goto error;
        }
        Py_INCREF(subject_obj);
        utf8_owner = subject_obj;
        logical_length = PyUnicode_GET_LENGTH(subject_obj);
        if (PyUnicode_IS_ASCII(subject_obj)) {
            subject_is_ascii = 1;
            utf8_data = (const char *)PyUnicode_1BYTE_DATA(subject_obj);
            subject_length_bytes = logical_length;
        } else {
            const char *data = PyUnicode_AsUTF8AndSize(subject_obj, &subject_length_bytes);
            if (data == NULL) {
                goto error;
            }
            utf8_data = data;
        }
    } else if (PyObject_CheckBuffer(subject_obj)) {
        const char *buf_data = NULL;
        Py_ssize_t buf_length = 0;
        PyObject *buf_view = buffer_view_from_object(subject_obj, &buf_data, &buf_length);
        if (buf_view == NULL) {
            goto error;
        }
        utf8_owner = buf_view;
        utf8_data = buf_data;
        subject_length_bytes = buf_length;
        logical_length = buf_length;
        subject_is_bytes = 1;
        if (ensure_valid_utf8_for_bytes_subject(self,
                                                subject_is_bytes,
                                                utf8_data,
                                                subject_length_bytes) < 0) {
            goto error;
        }
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "subject must be str, bytes, or a bytes-like buffer object (e.g. mmap.mmap)");
        goto error;
    }

    uint32_t capture_count = 0;
    int info_rc = pcre2_pattern_info(self->code, PCRE2_INFO_CAPTURECOUNT, &capture_count);
    if (info_rc < 0) {
        raise_pcre_error("pattern_info", info_rc, 0);
        goto error;
    }

    match_data = pattern_match_data_acquire(self, &match_data_from_pattern);
    if (match_data == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    int attempt_jit = pattern_jit_get(self);
    if (attempt_jit) {
        match_context = pattern_match_context_acquire(self, 0, &match_context_from_pattern);
        if (match_context == NULL) {
            PyErr_NoMemory();
            goto error;
        }
        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            PyErr_NoMemory();
            goto error;
        }
        pcre2_jit_stack_assign(match_context, NULL, jit_stack);
    }

    uint32_t options = 0;
    if (!subject_is_bytes) {
        options |= PCRE2_NO_UTF_CHECK;
    }

    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    if (ovector == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "PCRE2 returned empty match data");
        goto error;
    }
    uint32_t available_pairs = pcre2_get_ovector_count(match_data);

    result = PyList_New(0);
    if (result == NULL) {
        goto error;
    }
    Py_ssize_t last_end = 0;
    Py_ssize_t current_byte = 0;
    Py_ssize_t splits_done = 0;

    while (1) {
        if (maxsplit > 0 && splits_done >= maxsplit) {
            break;
        }
        if (current_byte > subject_length_bytes) {
            break;
        }

        int rc = 0;
        int use_jit = attempt_jit;
        if (use_jit) {
            rc = pcre2_jit_match(self->code,
                                 (PCRE2_SPTR)utf8_data,
                                 (PCRE2_SIZE)subject_length_bytes,
                                 (PCRE2_SIZE)current_byte,
                                 options,
                                 match_data,
                                 match_context);
            if (rc == PCRE2_ERROR_JIT_BADOPTION || rc == PCRE2_ERROR_BADOPTION) {
                pattern_jit_set(self, 0);
                attempt_jit = 0;
                if (match_context != NULL) {
                    pcre2_jit_stack_assign(match_context, NULL, NULL);
                }
                if (jit_stack != NULL) {
                    jit_stack_cache_release(jit_stack);
                    jit_stack = NULL;
                }
                use_jit = 0;
            } else if (rc == PCRE2_ERROR_NOMATCH) {
                break;
            } else if (rc < 0) {
                PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
                raise_pcre_error("jit_match", rc, error_offset);
                goto error;
            }
        }

        if (!use_jit) {
            rc = pcre2_match(self->code,
                             (PCRE2_SPTR)utf8_data,
                             (PCRE2_SIZE)subject_length_bytes,
                             (PCRE2_SIZE)current_byte,
                             options,
                             match_data,
                             match_context);
            if (rc == PCRE2_ERROR_NOMATCH) {
                break;
            }
            if (rc < 0) {
                PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
                raise_pcre_error("match", rc, error_offset);
                goto error;
            }
        }

        Py_ssize_t start_byte = (Py_ssize_t)ovector[0];
        Py_ssize_t end_byte = (Py_ssize_t)ovector[1];

        PyObject *piece = extract_value_from_offsets(subject_obj, utf8_data, subject_is_bytes,
                                                     subject_is_ascii, last_end, start_byte);
        if (piece == NULL) {
            goto error;
        }
        if (PyList_Append(result, piece) < 0) {
            Py_DECREF(piece);
            goto error;
        }
        Py_DECREF(piece);

        if (capture_count > 0) {
            uint32_t group_limit = capture_count;
            if (group_limit > available_pairs - 1) {
                group_limit = available_pairs - 1;
            }
            for (uint32_t i = 1; i <= group_limit; ++i) {
                Py_ssize_t g_start = (Py_ssize_t)ovector[(size_t)i * 2];
                Py_ssize_t g_end = (Py_ssize_t)ovector[(size_t)i * 2 + 1];
                PyObject *group_value = extract_value_from_offsets(subject_obj, utf8_data,
                                                                    subject_is_bytes,
                                                                    subject_is_ascii,
                                                                    g_start, g_end);
                if (group_value == NULL) {
                    goto error;
                }
                if (PyList_Append(result, group_value) < 0) {
                    Py_DECREF(group_value);
                    goto error;
                }
                Py_DECREF(group_value);
            }
        }

        last_end = end_byte;
        splits_done += 1;

        if (start_byte == end_byte) {
            if (end_byte >= subject_length_bytes) {
                current_byte = subject_length_bytes + 1;
            } else if (subject_is_bytes || subject_is_ascii) {
                current_byte = end_byte + 1;
            } else {
                current_byte = end_byte;
                unsigned char lead = (unsigned char)utf8_data[current_byte];
                Py_ssize_t char_bytes = 1;
                if ((lead & 0xE0) == 0xC0) {
                    char_bytes = 2;
                } else if ((lead & 0xF0) == 0xE0) {
                    char_bytes = 3;
                } else if ((lead & 0xF8) == 0xF0) {
                    char_bytes = 4;
                }
                if (current_byte + char_bytes > subject_length_bytes) {
                    char_bytes = subject_length_bytes - current_byte;
                }
                current_byte += char_bytes;
            }
        } else {
            current_byte = end_byte;
        }
    }

    PyObject *tail = extract_value_from_offsets(subject_obj, utf8_data, subject_is_bytes,
                                                subject_is_ascii, last_end, subject_length_bytes);
    if (tail == NULL) {
        goto error;
    }
    if (PyList_Append(result, tail) < 0) {
        Py_DECREF(tail);
        goto error;
    }
    Py_DECREF(tail);

    goto cleanup;

error:
    Py_XDECREF(result);
    result = NULL;

cleanup:
    if (jit_stack != NULL) {
        if (match_context != NULL) {
            pcre2_jit_stack_assign(match_context, NULL, NULL);
        }
        jit_stack_cache_release(jit_stack);
    }
    if (match_context != NULL) {
        pattern_match_context_release(self, match_context, match_context_used_offset_limit, match_context_from_pattern);
    }
    if (match_data != NULL) {
        pattern_match_data_release(self, match_data, match_data_from_pattern);
    }
    Py_XDECREF(utf8_owner);
    return result;
}

static PyObject *
Pattern_split_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "maxsplit", NULL};
    PyObject *subject = NULL;
    Py_ssize_t maxsplit = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|n", kwlist,
                                     &subject, &maxsplit)) {
        return NULL;
    }

    return Pattern_split(self, subject, maxsplit);
}

static PyMethodDef Pattern_methods[] = {
    {"findall", (PyCFunction)Pattern_findall_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return a list of all non-overlapping matches.")},
    {"substitute", (PyCFunction)Pattern_substitute_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Fast substitution using pcre2_substitute.")},
    {"finditer", (PyCFunction)Pattern_finditer_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return an iterator over successive matches.")},
    {"split", (PyCFunction)Pattern_split_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Split the subject by occurrences of the pattern.")},
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
    PatternObject *result = NULL;

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
    Py_DECREF(flags_obj);
    Py_DECREF(jit_bool);
    if (cache_key == NULL) {
        return NULL;
    }

    if (use_cache) {
        PatternObject *cached = NULL;
        if (pattern_cache_lookup(cache_key, &cached) == 0) {
            if (cached != NULL) {
                Py_DECREF(cache_key);
                return cached;
            }
        } else {
            PyErr_Clear();
            use_cache = 0;
        }
    }

    result = Pattern_create(pattern_obj, flags, jit, jit_explicit);
    if (result == NULL) {
        Py_DECREF(cache_key);
        return NULL;
    }

    if (use_cache) {
        if (pattern_cache_store(cache_key, result) < 0) {
            PyErr_Clear();
        }
    }

    Py_DECREF(cache_key);
    return result;
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

    PyObject *result = Pattern_execute(pattern, subject, 0, -1, 0, EXEC_MODE_MATCH, NULL);
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

    PyObject *result = Pattern_execute(pattern, subject, 0, -1, 0, EXEC_MODE_SEARCH, NULL);
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

    PyObject *result = Pattern_execute(pattern, subject, 0, -1, 0, EXEC_MODE_FULLMATCH, NULL);
    Py_DECREF(pattern);
    return result;
}

static PyObject *
module_findall(PyObject *Py_UNUSED(module), PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"pattern", "string", "flags", "jit", NULL};
    PyObject *pattern_obj = NULL;
    PyObject *subject = NULL;
    unsigned long flags = 0;
    PyObject *jit_obj = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|k$O", kwlist,
                                     &pattern_obj, &subject, &flags, &jit_obj)) {
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

    PyObject *result = Pattern_findall(pattern, subject, 0, -1, 0);
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

static PyObject *
module_attach_match(PyObject *Py_UNUSED(module), PyObject *args)
{
    /*
     * The public Python wrapper keeps using the same C MatchObject and only
     * stamps in the high-level owner here. That avoids a second Python object
     * allocation on every successful search/match call.
     */
    PyObject *match_obj = NULL;
    PyObject *pattern_obj = NULL;
    if (!PyArg_ParseTuple(args, "OO", &match_obj, &pattern_obj)) {
        return NULL;
    }

    if (!PyObject_TypeCheck(match_obj, &MatchType)) {
        PyErr_SetString(PyExc_TypeError, "expected pcre.Match instance");
        return NULL;
    }

    if (match_set_public_pattern((MatchObject *)match_obj, pattern_obj) < 0) {
        return NULL;
    }

    Py_INCREF(match_obj);
    return match_obj;
}

static PyObject *module_memory_allocator(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args));
static PyObject *module_get_pcre2_version(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args));
static PyObject *module_jit_anchor_fixup_needed(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args));
static void initialize_pcre2_version(void);


static PyMethodDef module_methods[] = {
    {"compile", (PyCFunction)module_compile, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Compile a pattern into a PCRE2 Pattern object." )},
    {"match", (PyCFunction)module_match, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Match a pattern against the beginning of a string." )},
    {"search", (PyCFunction)module_search, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Search a string for a pattern." )},
    {"fullmatch", (PyCFunction)module_fullmatch, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Match a pattern against the entire string." )},
    {"findall", (PyCFunction)module_findall, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return a list of all non-overlapping matches." )},
    {"configure", (PyCFunction)module_configure, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Get or set module-wide defaults (currently only 'jit')." )},
    {"_attach_match", (PyCFunction)module_attach_match, METH_VARARGS, PyDoc_STR("Attach a public pattern owner to a low-level match object." )},
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
    {"_jit_anchor_fixup_needed", (PyCFunction)module_jit_anchor_fixup_needed, METH_NOARGS, PyDoc_STR("Return 1 if the JIT anchoring workaround is active for this PCRE2 build." )},
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

static int
jit_anchor_fixup_needed(void)
{
    int current = atomic_load_explicit(&jit_anchor_fixup_needed_state, memory_order_acquire);
    if (current != -1) {
        return current;
    }

    int needed = 0;
    int error_code = 0;
    PCRE2_SIZE error_offset = 0;

    /*
     * Probe 1: PCRE2_ANCHORED at match time must force a start-at-offset match.
     * A compliant JIT run on "X2025-10-08" with pattern \\d+ should return
     * PCRE2_ERROR_NOMATCH because the subject does not start with a digit.
     */
    pcre2_code *code = pcre2_compile((PCRE2_SPTR)"\\d+", 3, 0, &error_code, &error_offset, NULL);
    if (code != NULL) {
        int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
        if (jit_rc >= 0) {
            pcre2_match_data *match_data = pcre2_match_data_create(2, NULL);
            if (match_data != NULL) {
                int rc = pcre2_jit_match(code,
                                         (PCRE2_SPTR)"X2025-10-08",
                                         11,
                                         0,
                                         PCRE2_ANCHORED,
                                         match_data,
                                         NULL);
                if (rc >= 0) {
                    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
                    if (ovector != NULL && ovector[0] != 0) {
                        needed = 1;
                    }
                }
                pcre2_match_data_free(match_data);
            }
        }
        pcre2_code_free(code);
    }

    if (!needed) {
        /*
         * Probe 2: PCRE2_ENDANCHORED at match time must force the match to end
         * at the end of the subject. Pattern "a|ab" on "ab" must match "ab",
         * not just "a". A non-compliant JIT may return the shorter match.
         */
        code = pcre2_compile((PCRE2_SPTR)"a|ab", 4, 0, &error_code, &error_offset, NULL);
        if (code != NULL) {
            int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
            if (jit_rc >= 0) {
                pcre2_match_data *match_data = pcre2_match_data_create(2, NULL);
                if (match_data != NULL) {
                    int rc = pcre2_jit_match(code,
                                             (PCRE2_SPTR)"ab",
                                             2,
                                             0,
                                             PCRE2_ANCHORED | PCRE2_ENDANCHORED,
                                             match_data,
                                             NULL);
                    if (rc >= 0) {
                        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
                        if (ovector == NULL || ovector[0] != 0 || ovector[1] != 2) {
                            needed = 1;
                        }
                    }
                    pcre2_match_data_free(match_data);
                }
            }
            pcre2_code_free(code);
        }
    }

    atomic_store_explicit(&jit_anchor_fixup_needed_state, needed, memory_order_release);
    return needed;
}

PyMODINIT_FUNC
PyInit_pcre_ext_c(void)
{
    PyObject *module = NULL;
    const char *force_lock_env = NULL;
    const char *context_cache_env = NULL;
    const char *pattern_cache_env = NULL;
    int pattern_cache_global = 0;
    int force_jit_lock = 0;

    force_lock_env = Py_GETENV("PYPCRE_FORCE_JIT_LOCK");
    if (force_lock_env == NULL) {
        force_lock_env = Py_GETENV("PCRE2_FORCE_JIT_LOCK");
    }
    force_jit_lock = env_flag_is_true(force_lock_env);
    if (jit_support_initialize(force_jit_lock) < 0) {
        goto error_jit_support;
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
        goto error_jit_support;
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
    (void)jit_anchor_fixup_needed();

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

    if (PyModule_AddStringConstant(module, "__version__", "0.6.0") < 0) {
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
error_jit_support:
    jit_support_teardown();
    return NULL;
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

static PyObject *
module_jit_anchor_fixup_needed(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
    return PyLong_FromLong(jit_anchor_fixup_needed());
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
