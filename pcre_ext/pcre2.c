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
/* Module execution is limited to one interpreter because these are process globals. */
static ATOMIC_VAR(PyInterpreterState *) primary_interpreter = ATOMIC_VAR_INIT(NULL);
#if defined(PCRE2_USE_OFFSET_LIMIT)
/* -1 unknown, 0 unsupported, 1 supported by the loaded PCRE2 runtime. */
static ATOMIC_VAR(int) offset_limit_support = ATOMIC_VAR_INIT(-1);
#endif
/* -1 unknown, 0 compliant, 1 JIT ignores ANCHORED/ENDANCHORED match-time options. */
static ATOMIC_VAR(int) jit_anchor_fixup_needed_state = ATOMIC_VAR_INIT(-1);

static void detect_offset_limit_support(void);
static int jit_anchor_fixup_needed(void);

static int
coerce_uint32_argument(PyObject *value, const char *name, uint32_t *out)
{
    if (value == NULL) {
        *out = 0;
        return 0;
    }
    PyObject *index = PyNumber_Index(value);
    if (index == NULL) {
        return -1;
    }
    unsigned long long parsed = PyLong_AsUnsignedLongLong(index);
    Py_DECREF(index);
    if (parsed == (unsigned long long)-1 && PyErr_Occurred()) {
        return -1;
    }
    if (parsed > UINT32_MAX) {
        PyErr_Format(PyExc_OverflowError, "%s must fit in uint32_t", name);
        return -1;
    }
    *out = (uint32_t)parsed;
    return 0;
}

/*
 * Releasing the GIL is only worthwhile when the PCRE2 call is expected to do
 * enough work to amortize the PyEval_{Save,Restore}Thread overhead.  For very
 * short matches the extra work is measurable, so only release for large inputs.
 */
#define PCRE2_GIL_RELEASE_THRESHOLD 262144ULL
#define PCRE_PATTERN_CACHE_INPUT_LIMIT (64 * 1024)

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

#if defined(Py_GIL_DISABLED)
#define PCRE2_JIT_CALL_MAYBE_RELEASE_GIL(call, length) \
    do {                                                \
        (void)(length);                                 \
        jit_guard_acquire();                            \
        rc = (call);                                    \
        jit_guard_release();                            \
    } while (0)
#else
#define PCRE2_JIT_CALL_MAYBE_RELEASE_GIL(call, length)       \
    do {                                                     \
        if ((length) > PCRE2_GIL_RELEASE_THRESHOLD) {       \
            PyThreadState *_save = PyEval_SaveThread();     \
            jit_guard_acquire();                            \
            rc = (call);                                    \
            jit_guard_release();                            \
            PyEval_RestoreThread(_save);                    \
        } else {                                             \
            jit_guard_acquire();                            \
            rc = (call);                                    \
            jit_guard_release();                            \
        }                                                    \
    } while (0)
#endif

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
static int match_resolve_span(MatchObject *self,
                              Py_ssize_t index,
                              Py_ssize_t *start_out,
                              Py_ssize_t *end_out,
                              int allow_missing);

static void
Match_dealloc(MatchObject *self)
{
    Py_XDECREF(self->pattern);
    Py_XDECREF(self->public_pattern);
    Py_XDECREF(self->subject);
    Py_XDECREF(self->utf8_owner);
    Py_XDECREF(self->regs_cache);
    pcre_free(self->ovector);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
Match_repr(MatchObject *self)
{
    Py_ssize_t start = 0;
    Py_ssize_t end = 0;
    if (match_resolve_span(self, 0, &start, &end, 1) < 0) {
        return NULL;
    }
    return PyUnicode_FromFormat("<Match span=(%zd, %zd) pattern=%R>", start, end, self->pattern->pattern);
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

    /* UTF-8 offsets are identical to Python indexes for ASCII subjects.  This
       is the hot path for log/token matching: avoid rescanning the prefix for
       every span/start/end accessor (which otherwise makes a late match in a
       large ASCII subject O(subject length) per accessor).  The subject is an
       owned immutable reference for the lifetime of this match, so its ASCII
       kind cannot change while this snapshot is queried, including GIL=0. */
    if (PyUnicode_IS_ASCII(self->subject)) {
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
        Py_ssize_t key_length = 0;
        const char *key_text = PyUnicode_AsUTF8AndSize(key, &key_length);
        if (key_text == NULL) {
            return -1;
        }

        /* The immutable groupindex mapping handles the overwhelmingly common
           unique-name case in O(1).  If the selected entry did not
           participate, retain the PCRE2 name-table walk below so DUPNAMES
           still selects the participating capture exactly like ``re``. */
        if (PyUnicode_CheckExact(key)) {
            PyObject *mapped = PyDict_GetItemWithError(self->pattern->groupindex, key);
            if (mapped != NULL) {
                Py_ssize_t candidate = PyLong_AsSsize_t(mapped);
                if (candidate == -1 && PyErr_Occurred()) {
                    return -1;
                }
                if (candidate >= 0 && (size_t)candidate < self->ovec_count) {
                    Py_ssize_t start = self->ovector[(size_t)candidate * 2];
                    Py_ssize_t end = self->ovector[(size_t)candidate * 2 + 1];
                    if (start >= 0 && end >= 0) {
                        *index = candidate;
                        return 0;
                    }
                }
            } else if (PyErr_Occurred()) {
                return -1;
            }
        }

        uint32_t name_count = 0;
        uint32_t entry_size = 0;
        PCRE2_SPTR name_table = NULL;
        if (pcre2_pattern_info(self->pattern->code, PCRE2_INFO_NAMECOUNT, &name_count) != 0 ||
            pcre2_pattern_info(self->pattern->code, PCRE2_INFO_NAMEENTRYSIZE, &entry_size) != 0 ||
            pcre2_pattern_info(self->pattern->code, PCRE2_INFO_NAMETABLE, &name_table) != 0 ||
            name_table == NULL || entry_size < 3) {
            PyErr_Format(PyExc_IndexError, "no such group '%U'", key);
            return -1;
        }

        Py_ssize_t first_match = -1;
        size_t name_max = (size_t)entry_size - 2;
        for (uint32_t i = 0; i < name_count; ++i) {
            const unsigned char *entry = (const unsigned char *)(
                name_table + (size_t)i * entry_size
            );
            const char *name = (const char *)(entry + 2);
            size_t name_length = strnlen(name, name_max);
            if ((Py_ssize_t)name_length != key_length ||
                memcmp(name, key_text, name_length) != 0) {
                continue;
            }

            Py_ssize_t candidate = (Py_ssize_t)((entry[0] << 8) | entry[1]);
            if (first_match < 0) {
                first_match = candidate;
            }
            if (candidate >= 0 && (size_t)candidate < self->ovec_count) {
                Py_ssize_t start = self->ovector[(size_t)candidate * 2];
                Py_ssize_t end = self->ovector[(size_t)candidate * 2 + 1];
                if (start >= 0 && end >= 0) {
                    *index = candidate;
                    return 0;
                }
            }
        }
        if (first_match >= 0) {
            *index = first_match;
            return 0;
        }
        PyErr_Format(PyExc_IndexError, "no such group '%U'", key);
        return -1;
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

static inline PyObject *
extract_findall_group_from_offsets(const char *utf8_data,
                                   int subject_is_bytes,
                                   int subject_is_ascii,
                                   Py_ssize_t start,
                                   Py_ssize_t end)
{
    /* ``re.findall`` represents unmatched captures as empty strings. */
    if (start < 0 || end < 0 || end < start) {
        if (subject_is_bytes) {
            return PyBytes_FromStringAndSize("", 0);
        }
        return PyUnicode_New(0, 127);
    }
    return extract_value_from_offsets(NULL, utf8_data, subject_is_bytes,
                                      subject_is_ascii, start, end);
}

static inline Py_ssize_t
advance_one_character(const char *utf8_data,
                      Py_ssize_t subject_length_bytes,
                      Py_ssize_t byte_offset,
                      int single_byte_subject)
{
    if (byte_offset >= subject_length_bytes) {
        return subject_length_bytes;
    }
    if (single_byte_subject) {
        return byte_offset + 1;
    }

    unsigned char lead = (unsigned char)utf8_data[byte_offset];
    Py_ssize_t char_bytes = 1;
    if ((lead & 0xE0) == 0xC0) {
        char_bytes = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        char_bytes = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        char_bytes = 4;
    }
    if (char_bytes > subject_length_bytes - byte_offset) {
        char_bytes = subject_length_bytes - byte_offset;
    }
    return byte_offset + char_bytes;
}

static int
ensure_subject_type_compatible(PatternObject *pattern, int subject_is_bytes)
{
    if (pattern->pattern_is_bytes == subject_is_bytes) {
        return 0;
    }
    if (pattern->pattern_is_bytes) {
        PyErr_SetString(
            PyExc_TypeError,
            "cannot use a bytes pattern on a string-like object"
        );
    } else {
        PyErr_SetString(
            PyExc_TypeError,
            "cannot use a string pattern on a bytes-like object"
        );
    }
    return -1;
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
Match_group_fast(MatchObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs == 0) {
        return match_get_group_value(self, 0);
    }
    if (nargs == 1) {
        PyObject *key = args[0];
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
        PyObject *key = args[i];
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
Match_span_fast(MatchObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs > 1) {
        PyErr_Format(PyExc_TypeError, "span expected at most 1 argument, got %zd", nargs);
        return NULL;
    }
    PyObject *key = nargs == 0 ? NULL : args[0];
    Py_ssize_t index = 0;
    if (resolve_group_key(self, key, &index) < 0) {
        return NULL;
    }
    Py_ssize_t start = 0;
    Py_ssize_t end = 0;
    int rc = match_resolve_span(self, index, &start, &end, 1);
    if (rc < 0) {
        return NULL;
    }
    if (rc > 0) {
        Py_RETURN_NONE;
    }
    return Py_BuildValue("(nn)", start, end);
}

static PyObject *
Match_start_fast(MatchObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs > 1) {
        PyErr_Format(PyExc_TypeError, "start expected at most 1 argument, got %zd", nargs);
        return NULL;
    }
    PyObject *key = nargs == 0 ? NULL : args[0];
    Py_ssize_t index = 0;
    if (resolve_group_key(self, key, &index) < 0) {
        return NULL;
    }
    Py_ssize_t start = 0;
    Py_ssize_t end = 0;
    int rc = match_resolve_span(self, index, &start, &end, 1);
    if (rc < 0) {
        return NULL;
    }
    if (rc > 0) {
        Py_RETURN_NONE;
    }
    return PyLong_FromSsize_t(start);
}

static PyObject *
Match_end_fast(MatchObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs > 1) {
        PyErr_Format(PyExc_TypeError, "end expected at most 1 argument, got %zd", nargs);
        return NULL;
    }
    PyObject *key = nargs == 0 ? NULL : args[0];
    Py_ssize_t index = 0;
    if (resolve_group_key(self, key, &index) < 0) {
        return NULL;
    }
    Py_ssize_t start = 0;
    Py_ssize_t end = 0;
    int rc = match_resolve_span(self, index, &start, &end, 1);
    if (rc < 0) {
        return NULL;
    }
    if (rc > 0) {
        Py_RETURN_NONE;
    }
    return PyLong_FromSsize_t(end);
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
        Py_ssize_t index = 0;
        if (resolve_group_key(self, key, &index) < 0) {
            Py_DECREF(result);
            return NULL;
        }
        PyObject *group_value = match_get_group_value(self, index);
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
    PyObject *pattern = NULL;
    Py_BEGIN_CRITICAL_SECTION(self);
    pattern = self->public_pattern != NULL
        ? self->public_pattern
        : (PyObject *)self->pattern;
    Py_INCREF(pattern);
    Py_END_CRITICAL_SECTION();
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

typedef struct {
    uint32_t capture_last;
    int seen;
} LastIndexReplayState;

static pcre2_code *
pattern_get_lastindex_replay_code(PatternObject *pattern)
{
#if defined(PCRE_EXT_HAVE_ATOMICS)
    pcre2_code *cached = atomic_load_explicit(
        &pattern->lastindex_replay_code,
        memory_order_acquire
    );
    if (cached != NULL) {
        return cached;
    }
#else
    PyThread_acquire_lock(pattern->jit_lock, WAIT_LOCK);
    if (pattern->lastindex_replay_code != NULL) {
        pcre2_code *cached = pattern->lastindex_replay_code;
        PyThread_release_lock(pattern->jit_lock);
        return cached;
    }
#endif

    Py_ssize_t pattern_length = PyBytes_GET_SIZE(pattern->pattern_bytes);
    const char *pattern_data = PyBytes_AS_STRING(pattern->pattern_bytes);
    uint32_t compile_options = pattern->original_compile_options |
                               PCRE2_AUTO_CALLOUT;
    int error_code = 0;
    PCRE2_SIZE error_offset = 0;
    pcre2_code *compiled = pcre2_compile(
        (PCRE2_SPTR)pattern_data,
        (PCRE2_SIZE)pattern_length,
        compile_options,
        &error_code,
        &error_offset,
        NULL
    );
    if (compiled == NULL) {
#if !defined(PCRE_EXT_HAVE_ATOMICS)
        PyThread_release_lock(pattern->jit_lock);
#endif
        raise_pcre_error("lastindex compile", error_code, error_offset);
        return NULL;
    }

#if defined(PCRE_EXT_HAVE_ATOMICS)
    pcre2_code *expected = NULL;
    if (!atomic_compare_exchange_strong_explicit(
            &pattern->lastindex_replay_code,
            &expected,
            compiled,
            memory_order_release,
            memory_order_acquire)) {
        pcre2_code_free(compiled);
        return expected;
    }
#else
    pattern->lastindex_replay_code = compiled;
    PyThread_release_lock(pattern->jit_lock);
#endif
    return compiled;
}

static int
lastindex_replay_callout(pcre2_callout_block *block, void *data)
{
    LastIndexReplayState *state = (LastIndexReplayState *)data;
    /*
     * PCRE2_AUTO_CALLOUT exposes capture_last at every pattern item and at the
     * successful end of ordinary patterns. Keeping the most recent value also
     * covers an early (*ACCEPT), which succeeds before the terminal callout.
     */
    state->capture_last = block->capture_last;
    state->seen = 1;
    return 0;
}

static int
match_replay_lastindex(MatchObject *self, int *lastindex_out)
{
    pcre2_code *replay_code = pattern_get_lastindex_replay_code(self->pattern);
    if (replay_code == NULL) {
        return -1;
    }

    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(
        replay_code,
        NULL
    );
    if (match_data == NULL) {
        PyErr_NoMemory();
        return -1;
    }

    pcre2_match_context *match_context = pcre2_match_context_create(NULL);
    if (match_context == NULL) {
        pcre2_match_data_free(match_data);
        PyErr_NoMemory();
        return -1;
    }

    LastIndexReplayState state = {0, 0};
    int ctx_rc = pcre2_set_callout(
        match_context,
        lastindex_replay_callout,
        &state
    );
    if (ctx_rc < 0) {
        pcre2_match_context_free(match_context);
        pcre2_match_data_free(match_data);
        raise_pcre_error("lastindex set_callout", ctx_rc, 0);
        return -1;
    }

    Py_ssize_t replay_length = self->utf8_length;
    if (self->subject_is_bytes) {
        replay_length = self->public_endpos;
    } else if (self->public_endpos < PyUnicode_GET_LENGTH(self->subject)) {
        if (utf8_index_to_offset(self->subject,
                                 self->public_endpos,
                                 &replay_length) < 0) {
            pcre2_match_context_free(match_context);
            pcre2_match_data_free(match_data);
            return -1;
        }
    }

    uint32_t replay_options = self->replay_options;
#if defined(PCRE2_USE_OFFSET_LIMIT)
    replay_options &= ~PCRE2_USE_OFFSET_LIMIT;
#endif
    replay_options |= PCRE2_ANCHORED;
    if (!self->subject_is_bytes ||
        (replay_length == self->utf8_length && self->ovector[0] == 0)) {
        replay_options |= PCRE2_NO_UTF_CHECK;
    } else {
        replay_options &= ~PCRE2_NO_UTF_CHECK;
    }

    int rc = pcre2_match(
        replay_code,
        (PCRE2_SPTR)self->utf8_data,
        (PCRE2_SIZE)replay_length,
        (PCRE2_SIZE)self->ovector[0],
        replay_options,
        match_data,
        match_context
    );

    PCRE2_SIZE *replay_ovector = rc >= 0
        ? pcre2_get_ovector_pointer(match_data)
        : NULL;
    uint32_t replay_ovec_count = rc >= 0
        ? pcre2_get_ovector_count(match_data)
        : 0;
    int replay_matches = replay_ovector != NULL &&
                         replay_ovec_count >= self->ovec_count;
    if (replay_matches) {
        size_t offset_count = (size_t)self->ovec_count * 2;
        for (size_t i = 0; i < offset_count; ++i) {
            if ((Py_ssize_t)replay_ovector[i] != self->ovector[i]) {
                replay_matches = 0;
                break;
            }
        }
    }

    pcre2_match_context_free(match_context);
    pcre2_match_data_free(match_data);

    if (rc < 0) {
        raise_pcre_error("lastindex replay", rc, 0);
        return -1;
    }
    if (!replay_matches || !state.seen) {
        PyErr_SetString(
            PyExc_RuntimeError,
            "lastindex replay did not reproduce the original match"
        );
        return -1;
    }
    if (state.capture_last > self->pattern->capture_count) {
        PyErr_SetString(PyExc_RuntimeError, "PCRE2 returned an invalid capture_last");
        return -1;
    }

    *lastindex_out = (int)state.capture_last;
    return 0;
}

static PyObject *
Match_get_lastindex(MatchObject *self, void *closure)
{
    if (self->ovec_count <= 1) {
        Py_RETURN_NONE;
    }

    int cached_lastindex = -2;
    Py_BEGIN_CRITICAL_SECTION(self);
    cached_lastindex = self->lastindex_cache;
    Py_END_CRITICAL_SECTION();
    if (cached_lastindex != -2) {
        if (cached_lastindex < 0) {
            Py_RETURN_NONE;
        }
        return PyLong_FromLong(cached_lastindex);
    }

    /* The expensive AUTO_CALLOUT replay is only needed to order two or more
     * participating captures.  With zero or one set ovector pair, the exact
     * Python lastindex is already known from the immutable match snapshot. */
    int sole_participant = -1;
    for (uint32_t index = 1; index < self->ovec_count; ++index) {
        Py_ssize_t start = self->ovector[(size_t)index * 2];
        Py_ssize_t end = self->ovector[(size_t)index * 2 + 1];
        if (start < 0 || end < 0) {
            continue;
        }
        if (sole_participant >= 0) {
            sole_participant = -2;
            break;
        }
        sole_participant = (int)index;
    }
    if (sole_participant >= -1) {
        int lastindex = sole_participant;
        Py_BEGIN_CRITICAL_SECTION(self);
        if (self->lastindex_cache == -2) {
            self->lastindex_cache = lastindex;
        }
        lastindex = self->lastindex_cache;
        Py_END_CRITICAL_SECTION();
        if (lastindex < 0) {
            Py_RETURN_NONE;
        }
        return PyLong_FromLong(lastindex);
    }

    int lastindex = -1;
    int replay_ok = 0;
    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->lastindex_cache == -2) {
        int replayed = 0;
        if (match_replay_lastindex(self, &replayed) == 0) {
            self->lastindex_cache = replayed == 0 ? -1 : replayed;
            replay_ok = 1;
        }
    } else {
        replay_ok = 1;
    }
    lastindex = self->lastindex_cache;
    Py_END_CRITICAL_SECTION();

    if (!replay_ok) {
        return NULL;
    }
    if (lastindex < 0) {
        Py_RETURN_NONE;
    }
    return PyLong_FromLong(lastindex);
}

static PyObject *
Match_get_lastgroup(MatchObject *self, void *closure)
{
    PyObject *lastindex_obj = Match_get_lastindex(self, closure);
    if (lastindex_obj == NULL || lastindex_obj == Py_None) {
        return lastindex_obj;
    }

    unsigned long lastindex = PyLong_AsUnsignedLong(lastindex_obj);
    Py_DECREF(lastindex_obj);
    if (lastindex == (unsigned long)-1 && PyErr_Occurred()) {
        return NULL;
    }

    uint32_t name_count = 0;
    uint32_t entry_size = 0;
    PCRE2_SPTR name_table = NULL;
    if (pcre2_pattern_info(self->pattern->code,
                           PCRE2_INFO_NAMECOUNT,
                           &name_count) != 0 ||
        name_count == 0 ||
        pcre2_pattern_info(self->pattern->code,
                           PCRE2_INFO_NAMEENTRYSIZE,
                           &entry_size) != 0 ||
        entry_size < 3 ||
        pcre2_pattern_info(self->pattern->code,
                           PCRE2_INFO_NAMETABLE,
                           &name_table) != 0 ||
        name_table == NULL) {
        Py_RETURN_NONE;
    }

    size_t name_max = (size_t)entry_size - 2;
    for (uint32_t i = 0; i < name_count; ++i) {
        const unsigned char *entry = (const unsigned char *)(
            name_table + (size_t)i * entry_size
        );
        uint32_t group_number = ((uint32_t)entry[0] << 8) | entry[1];
        if (group_number == lastindex) {
            const char *name = (const char *)(entry + 2);
            size_t name_length = strnlen(name, name_max);
            return PyUnicode_DecodeUTF8(name, (Py_ssize_t)name_length, "strict");
        }
    }

    Py_RETURN_NONE;
}

static PyObject *
Match_get_regs(MatchObject *self, void *closure)
{
    PyObject *cached = NULL;
    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->regs_cache != NULL) {
        cached = self->regs_cache;
        Py_INCREF(cached);
    }
    Py_END_CRITICAL_SECTION();
    if (cached != NULL) {
        return cached;
    }

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

    /* Match snapshots are immutable. Keep one tuple so repeated ``regs``
       reads avoid rebuilding every span; the critical section makes the
       first publication safe on free-threaded CPython. */
    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->regs_cache == NULL) {
        self->regs_cache = result;
        Py_INCREF(result);
    }
    cached = self->regs_cache;
    Py_INCREF(cached);
    Py_END_CRITICAL_SECTION();
    Py_DECREF(result);
    return cached;
}

static PyObject *
match_expand_render_reference(MatchObject *self,
                              PyObject *template_obj,
                              Py_ssize_t prefix_length,
                              Py_ssize_t suffix_start,
                              Py_ssize_t group_index,
                              int *handled)
{
    *handled = 0;
    Py_ssize_t template_length = self->subject_is_bytes
        ? PyBytes_GET_SIZE(template_obj)
        : PyUnicode_GET_LENGTH(template_obj);
    if (prefix_length < 0 || suffix_start < prefix_length ||
        suffix_start > template_length || group_index < 0 ||
        (size_t)group_index >= self->ovec_count) {
        return NULL;
    }

    PyObject *group = match_get_group_value(self, group_index);
    if (group == NULL) {
        return NULL;
    }
    *handled = 1;

    if (group == Py_None) {
        Py_DECREF(group);
        group = NULL;
    }

    Py_ssize_t suffix_length = template_length - suffix_start;
    if (prefix_length == 0 && suffix_length == 0) {
        if (group != NULL) {
            return group;
        }
        return self->subject_is_bytes
            ? PyBytes_FromStringAndSize("", 0)
            : PyUnicode_New(0, 127);
    }

    Py_ssize_t group_length = group == NULL ? 0 : PyObject_Length(group);
    if (group_length < 0) {
        Py_XDECREF(group);
        return NULL;
    }
    if (group_length > PY_SSIZE_T_MAX - prefix_length - suffix_length) {
        Py_XDECREF(group);
        PyErr_NoMemory();
        return NULL;
    }
    Py_ssize_t result_length = prefix_length + group_length + suffix_length;

    if (self->subject_is_bytes) {
        PyObject *result = PyBytes_FromStringAndSize(NULL, result_length);
        if (result == NULL) {
            Py_XDECREF(group);
            return NULL;
        }
        char *output = PyBytes_AS_STRING(result);
        const char *template_data = PyBytes_AS_STRING(template_obj);
        memcpy(output, template_data, (size_t)prefix_length);
        if (group != NULL && group_length > 0) {
            memcpy(output + prefix_length,
                   PyBytes_AS_STRING(group),
                   (size_t)group_length);
        }
        memcpy(output + prefix_length + group_length,
               template_data + suffix_start,
               (size_t)suffix_length);
        Py_XDECREF(group);
        return result;
    }

    Py_UCS4 max_character = PyUnicode_MAX_CHAR_VALUE(template_obj);
    if (group != NULL) {
        Py_UCS4 group_max = PyUnicode_MAX_CHAR_VALUE(group);
        if (group_max > max_character) {
            max_character = group_max;
        }
    }
    PyObject *result = PyUnicode_New(result_length, max_character);
    if (result == NULL) {
        Py_XDECREF(group);
        return NULL;
    }
    if (prefix_length > 0 &&
        PyUnicode_CopyCharacters(result, 0, template_obj, 0, prefix_length) < 0) {
        Py_DECREF(result);
        Py_XDECREF(group);
        return NULL;
    }
    if (group != NULL && group_length > 0 &&
        PyUnicode_CopyCharacters(result, prefix_length, group, 0, group_length) < 0) {
        Py_DECREF(result);
        Py_DECREF(group);
        return NULL;
    }
    if (suffix_length > 0 &&
        PyUnicode_CopyCharacters(result,
                                 prefix_length + group_length,
                                 template_obj,
                                 suffix_start,
                                 suffix_length) < 0) {
        Py_DECREF(result);
        Py_XDECREF(group);
        return NULL;
    }
    Py_XDECREF(group);
    return result;
}

static PyObject *
match_expand_simple_numeric(MatchObject *self,
                            PyObject *template_obj,
                            Py_ssize_t slash_index,
                            Py_ssize_t template_length,
                            int *handled)
{
    *handled = 0;
    if (slash_index < 0 || slash_index + 1 >= template_length) {
        return NULL;
    }

    int digit = -1;
    if (!self->subject_is_bytes && PyUnicode_CheckExact(template_obj)) {
        Py_UCS4 character = PyUnicode_ReadChar(template_obj, slash_index + 1);
        if (character == (Py_UCS4)-1 && PyErr_Occurred()) {
            return NULL;
        }
        if (character < '1' || character > '9') {
            return NULL;
        }
        if (slash_index + 2 < template_length) {
            Py_UCS4 following = PyUnicode_ReadChar(template_obj, slash_index + 2);
            if (following == (Py_UCS4)-1 && PyErr_Occurred()) {
                return NULL;
            }
            if (following >= '0' && following <= '9') {
                return NULL;
            }
            Py_ssize_t next_slash = PyUnicode_FindChar(
                template_obj,
                '\\',
                slash_index + 2,
                template_length,
                1
            );
            if (next_slash >= 0) {
                return NULL;
            }
            if (PyErr_Occurred()) {
                return NULL;
            }
        }
        digit = (int)(character - '0');
    } else if (self->subject_is_bytes && PyBytes_CheckExact(template_obj)) {
        const unsigned char *template_data = (const unsigned char *)PyBytes_AS_STRING(template_obj);
        unsigned char character = template_data[slash_index + 1];
        if (character < '1' || character > '9') {
            return NULL;
        }
        if (slash_index + 2 < template_length) {
            unsigned char following = template_data[slash_index + 2];
            if (following >= '0' && following <= '9') {
                return NULL;
            }
            if (memchr(template_data + slash_index + 2,
                       '\\',
                       (size_t)(template_length - slash_index - 2)) != NULL) {
                return NULL;
            }
        }
        digit = (int)(character - '0');
    } else {
        return NULL;
    }

    if (digit <= 0 || (size_t)digit >= self->ovec_count) {
        return NULL;
    }
    return match_expand_render_reference(
        self, template_obj, slash_index, slash_index + 2, digit, handled
    );
}

static PyObject *
match_expand_explicit_numeric(MatchObject *self,
                              PyObject *template_obj,
                              Py_ssize_t slash_index,
                              Py_ssize_t template_length,
                              int *handled)
{
    *handled = 0;
    if (slash_index < 0 || template_length - slash_index < 5) {
        return NULL;
    }

    Py_ssize_t cursor = slash_index + 1;
    Py_ssize_t group_index = 0;
    if (!self->subject_is_bytes && PyUnicode_CheckExact(template_obj)) {
        if (PyUnicode_ReadChar(template_obj, cursor) != 'g' ||
            PyUnicode_ReadChar(template_obj, cursor + 1) != '<') {
            return NULL;
        }
        cursor += 2;
        Py_ssize_t digit_start = cursor;
        while (cursor < template_length) {
            Py_UCS4 character = PyUnicode_ReadChar(template_obj, cursor);
            if (character == '>') {
                break;
            }
            if (character < '0' || character > '9' ||
                group_index > (PY_SSIZE_T_MAX - (character - '0')) / 10) {
                return NULL;
            }
            group_index = group_index * 10 + (Py_ssize_t)(character - '0');
            cursor += 1;
        }
        if (cursor == digit_start || cursor >= template_length) {
            return NULL;
        }
        if (cursor + 1 < template_length) {
            Py_ssize_t next_slash = PyUnicode_FindChar(
                template_obj, '\\', cursor + 1, template_length, 1
            );
            if (next_slash >= 0) {
                return NULL;
            }
            if (PyErr_Occurred()) {
                return NULL;
            }
        }
    } else if (self->subject_is_bytes && PyBytes_CheckExact(template_obj)) {
        const unsigned char *template_data = (const unsigned char *)PyBytes_AS_STRING(template_obj);
        if (template_data[cursor] != 'g' || template_data[cursor + 1] != '<') {
            return NULL;
        }
        cursor += 2;
        Py_ssize_t digit_start = cursor;
        while (cursor < template_length) {
            unsigned char character = template_data[cursor];
            if (character == '>') {
                break;
            }
            if (character < '0' || character > '9' ||
                group_index > (PY_SSIZE_T_MAX - (character - '0')) / 10) {
                return NULL;
            }
            group_index = group_index * 10 + (Py_ssize_t)(character - '0');
            cursor += 1;
        }
        if (cursor == digit_start || cursor >= template_length) {
            return NULL;
        }
        if (cursor + 1 < template_length &&
            memchr(template_data + cursor + 1,
                   '\\',
                   (size_t)(template_length - cursor - 1)) != NULL) {
            return NULL;
        }
    } else {
        return NULL;
    }

    if ((size_t)group_index >= self->ovec_count) {
        return NULL;
    }
    return match_expand_render_reference(
        self, template_obj, slash_index, cursor + 1, group_index, handled
    );
}

static int
match_expand_resolve_ascii_name(MatchObject *self,
                                const char *name,
                                Py_ssize_t name_length,
                                Py_ssize_t *group_index)
{
    uint32_t name_count = 0;
    uint32_t entry_size = 0;
    PCRE2_SPTR name_table = NULL;
    if (name_length <= 0 ||
        pcre2_pattern_info(self->pattern->code,
                           PCRE2_INFO_NAMECOUNT,
                           &name_count) != 0 ||
        name_count == 0 ||
        pcre2_pattern_info(self->pattern->code,
                           PCRE2_INFO_NAMEENTRYSIZE,
                           &entry_size) != 0 ||
        pcre2_pattern_info(self->pattern->code,
                           PCRE2_INFO_NAMETABLE,
                           &name_table) != 0 ||
        name_table == NULL || entry_size < 3) {
        return 0;
    }

    Py_ssize_t first_match = -1;
    size_t name_max = (size_t)entry_size - 2;
    for (uint32_t i = 0; i < name_count; ++i) {
        const unsigned char *entry = (const unsigned char *)(
            name_table + (size_t)i * entry_size
        );
        const char *entry_name = (const char *)(entry + 2);
        size_t entry_length = strnlen(entry_name, name_max);
        if (entry_length != (size_t)name_length ||
            memcmp(entry_name, name, entry_length) != 0) {
            continue;
        }

        Py_ssize_t candidate = (Py_ssize_t)((entry[0] << 8) | entry[1]);
        if (first_match < 0) {
            first_match = candidate;
        }
        if (candidate >= 0 && (size_t)candidate < self->ovec_count) {
            Py_ssize_t start = self->ovector[(size_t)candidate * 2];
            Py_ssize_t end = self->ovector[(size_t)candidate * 2 + 1];
            if (start >= 0 && end >= 0) {
                *group_index = candidate;
                return 1;
            }
        }
    }
    if (first_match >= 0) {
        *group_index = first_match;
        return 1;
    }
    return 0;
}

static PyObject *
match_expand_explicit_named(MatchObject *self,
                            PyObject *template_obj,
                            Py_ssize_t slash_index,
                            Py_ssize_t template_length,
                            int *handled)
{
    *handled = 0;
    if (slash_index < 0 || template_length - slash_index < 5) {
        return NULL;
    }

    char name[129];
    Py_ssize_t cursor = slash_index + 1;
    Py_ssize_t name_length = 0;
    if (!self->subject_is_bytes && PyUnicode_CheckExact(template_obj)) {
        if (PyUnicode_ReadChar(template_obj, cursor) != 'g' ||
            PyUnicode_ReadChar(template_obj, cursor + 1) != '<') {
            return NULL;
        }
        cursor += 2;
        while (cursor < template_length) {
            Py_UCS4 character = PyUnicode_ReadChar(template_obj, cursor);
            if (character == '>') {
                break;
            }
            if (character > 0x7f || name_length >= 128) {
                return NULL;
            }
            name[name_length++] = (char)character;
            cursor += 1;
        }
        if (name_length == 0 || cursor >= template_length) {
            return NULL;
        }
        if (cursor + 1 < template_length) {
            Py_ssize_t next_slash = PyUnicode_FindChar(
                template_obj, '\\', cursor + 1, template_length, 1
            );
            if (next_slash >= 0) {
                return NULL;
            }
            if (PyErr_Occurred()) {
                return NULL;
            }
        }
    } else if (self->subject_is_bytes && PyBytes_CheckExact(template_obj)) {
        const unsigned char *template_data = (const unsigned char *)PyBytes_AS_STRING(template_obj);
        if (template_data[cursor] != 'g' || template_data[cursor + 1] != '<') {
            return NULL;
        }
        cursor += 2;
        while (cursor < template_length) {
            unsigned char character = template_data[cursor];
            if (character == '>') {
                break;
            }
            if (character > 0x7f || name_length >= 128) {
                return NULL;
            }
            name[name_length++] = (char)character;
            cursor += 1;
        }
        if (name_length == 0 || cursor >= template_length) {
            return NULL;
        }
        if (cursor + 1 < template_length &&
            memchr(template_data + cursor + 1,
                   '\\',
                   (size_t)(template_length - cursor - 1)) != NULL) {
            return NULL;
        }
    } else {
        return NULL;
    }

    Py_ssize_t group_index = -1;
    if (!match_expand_resolve_ascii_name(
            self, name, name_length, &group_index)) {
        return NULL;
    }
    return match_expand_render_reference(
        self, template_obj, slash_index, cursor + 1, group_index, handled
    );
}

typedef struct {
    Py_ssize_t slash_index;
    Py_ssize_t reference_end;
    Py_ssize_t group_index;
    int literal_backslash;
} MatchExpandToken;

static int
match_expand_parse_reference(MatchObject *self,
                             PyObject *template_obj,
                             Py_ssize_t slash_index,
                             Py_ssize_t template_length,
                             MatchExpandToken *reference)
{
    if (slash_index < 0 || slash_index + 1 >= template_length) {
        return 0;
    }

    Py_UCS4 following = self->subject_is_bytes
        ? (unsigned char)PyBytes_AS_STRING(template_obj)[slash_index + 1]
        : PyUnicode_ReadChar(template_obj, slash_index + 1);
    if (following == '\\') {
        reference->slash_index = slash_index;
        reference->reference_end = slash_index + 2;
        reference->group_index = -1;
        reference->literal_backslash = 1;
        return 1;
    }
    if (following >= '1' && following <= '9') {
        if (slash_index + 2 < template_length) {
            Py_UCS4 next = self->subject_is_bytes
                ? (unsigned char)PyBytes_AS_STRING(template_obj)[slash_index + 2]
                : PyUnicode_ReadChar(template_obj, slash_index + 2);
            if (next >= '0' && next <= '9') {
                return 0;
            }
        }
        Py_ssize_t group_index = (Py_ssize_t)(following - '0');
        if ((size_t)group_index >= self->ovec_count) {
            return 0;
        }
        reference->slash_index = slash_index;
        reference->reference_end = slash_index + 2;
        reference->group_index = group_index;
        reference->literal_backslash = 0;
        return 1;
    }

    if (following != 'g' || slash_index + 4 >= template_length) {
        return 0;
    }
    Py_UCS4 opener = self->subject_is_bytes
        ? (unsigned char)PyBytes_AS_STRING(template_obj)[slash_index + 2]
        : PyUnicode_ReadChar(template_obj, slash_index + 2);
    if (opener != '<') {
        return 0;
    }

    char name[129];
    Py_ssize_t name_length = 0;
    Py_ssize_t numeric_index = 0;
    int is_numeric = 1;
    Py_ssize_t cursor = slash_index + 3;
    while (cursor < template_length) {
        Py_UCS4 character = self->subject_is_bytes
            ? (unsigned char)PyBytes_AS_STRING(template_obj)[cursor]
            : PyUnicode_ReadChar(template_obj, cursor);
        if (character == '>') {
            break;
        }
        if (character > 0x7f || name_length >= 128) {
            return 0;
        }
        name[name_length++] = (char)character;
        if (character < '0' || character > '9') {
            is_numeric = 0;
        } else if (is_numeric) {
            if (numeric_index >
                (PY_SSIZE_T_MAX - (Py_ssize_t)(character - '0')) / 10) {
                return 0;
            }
            numeric_index = numeric_index * 10 +
                (Py_ssize_t)(character - '0');
        }
        cursor += 1;
    }
    if (name_length == 0 || cursor >= template_length) {
        return 0;
    }

    Py_ssize_t group_index = -1;
    if (is_numeric) {
        if ((size_t)numeric_index >= self->ovec_count) {
            return 0;
        }
        group_index = numeric_index;
    } else if (!match_expand_resolve_ascii_name(
                   self, name, name_length, &group_index)) {
        return 0;
    }
    reference->slash_index = slash_index;
    reference->reference_end = cursor + 1;
    reference->group_index = group_index;
    reference->literal_backslash = 0;
    return 1;
}

static int
match_expand_checked_add(Py_ssize_t *total, Py_ssize_t value)
{
    if (value < 0 || *total > PY_SSIZE_T_MAX - value) {
        PyErr_NoMemory();
        return -1;
    }
    *total += value;
    return 0;
}

#define MATCH_EXPAND_MAX_TOKENS 8

static PyObject *
match_expand_multiple_tokens(MatchObject *self,
                             PyObject *template_obj,
                             Py_ssize_t first_slash,
                             Py_ssize_t template_length,
                             int *handled)
{
    *handled = 0;
    MatchExpandToken references[MATCH_EXPAND_MAX_TOKENS];
    PyObject *groups[MATCH_EXPAND_MAX_TOKENS] = {NULL};
    Py_ssize_t group_offsets[MATCH_EXPAND_MAX_TOKENS] = {0};
    Py_ssize_t group_lengths[MATCH_EXPAND_MAX_TOKENS] = {0};
    Py_ssize_t reference_count = 0;
    Py_ssize_t slash_index = first_slash;

    while (slash_index >= 0) {
        if (reference_count >= MATCH_EXPAND_MAX_TOKENS ||
            !match_expand_parse_reference(
                self,
                template_obj,
                slash_index,
                template_length,
                &references[reference_count])) {
            return NULL;
        }
        Py_ssize_t search_start =
            references[reference_count].reference_end;
        reference_count += 1;
        if (self->subject_is_bytes) {
            const char *input = PyBytes_AS_STRING(template_obj);
            const char *found = memchr(
                input + search_start,
                '\\',
                (size_t)(template_length - search_start)
            );
            slash_index = found == NULL
                ? -1 : (Py_ssize_t)(found - input);
        } else {
            slash_index = PyUnicode_FindChar(
                template_obj, '\\', search_start, template_length, 1
            );
            if (slash_index < 0 && PyErr_Occurred()) {
                return NULL;
            }
        }
    }
    if (reference_count < 2 && !references[0].literal_backslash) {
        return NULL;
    }

    Py_ssize_t result_length = 0;
    Py_ssize_t literal_start = 0;
    int direct_groups = self->subject_is_bytes || PyUnicode_IS_ASCII(self->subject);
    Py_UCS4 max_character = self->subject_is_bytes
        ? 0 : PyUnicode_MAX_CHAR_VALUE(template_obj);
    for (Py_ssize_t i = 0; i < reference_count; ++i) {
        if (references[i].literal_backslash) {
            group_offsets[i] = 0;
            group_lengths[i] = 1;
        } else {
            size_t offset_index = (size_t)references[i].group_index * 2;
            Py_ssize_t group_start = self->ovector[offset_index];
            Py_ssize_t group_end = self->ovector[offset_index + 1];
            if (group_start < 0 || group_end < 0) {
                group_offsets[i] = 0;
                group_lengths[i] = 0;
            } else if (group_end < group_start ||
                       group_end > self->utf8_length) {
                PyErr_SetString(PyExc_RuntimeError, "invalid capture offsets");
                goto error;
            } else if (direct_groups) {
                group_offsets[i] = group_start;
                group_lengths[i] = group_end - group_start;
            } else {
                groups[i] = match_get_group_value(
                    self, references[i].group_index
                );
                if (groups[i] == NULL) {
                    goto error;
                }
                group_lengths[i] = PyObject_Length(groups[i]);
                if (group_lengths[i] < 0) {
                    goto error;
                }
                if (!self->subject_is_bytes &&
                    PyUnicode_MAX_CHAR_VALUE(groups[i]) > max_character) {
                    max_character = PyUnicode_MAX_CHAR_VALUE(groups[i]);
                }
            }
        }
        Py_ssize_t literal_length = references[i].slash_index - literal_start;
        if (match_expand_checked_add(&result_length, literal_length) < 0 ||
            match_expand_checked_add(&result_length, group_lengths[i]) < 0) {
            goto error;
        }
        literal_start = references[i].reference_end;
    }
    if (match_expand_checked_add(
            &result_length, template_length - literal_start) < 0) {
        goto error;
    }
    *handled = 1;

    if (self->subject_is_bytes) {
        PyObject *result = PyBytes_FromStringAndSize(NULL, result_length);
        if (result == NULL) {
            goto error;
        }
        char *output = PyBytes_AS_STRING(result);
        const char *input = PyBytes_AS_STRING(template_obj);
        Py_ssize_t output_offset = 0;
        literal_start = 0;
        for (Py_ssize_t i = 0; i < reference_count; ++i) {
            Py_ssize_t literal_length =
                references[i].slash_index - literal_start;
            if (literal_length > 0) {
                memcpy(output + output_offset,
                       input + literal_start,
                       (size_t)literal_length);
                output_offset += literal_length;
            }
            if (group_lengths[i] > 0) {
                if (references[i].literal_backslash) {
                    output[output_offset] = '\\';
                } else {
                    const char *group_data = direct_groups
                        ? self->utf8_data + group_offsets[i]
                        : PyBytes_AS_STRING(groups[i]);
                    memcpy(output + output_offset,
                           group_data,
                           (size_t)group_lengths[i]);
                }
                output_offset += group_lengths[i];
            }
            literal_start = references[i].reference_end;
        }
        Py_ssize_t suffix_length = template_length - literal_start;
        if (suffix_length > 0) {
            memcpy(output + output_offset,
                   input + literal_start,
                   (size_t)suffix_length);
        }
        for (Py_ssize_t i = 0; i < reference_count; ++i) {
            Py_XDECREF(groups[i]);
        }
        return result;
    }

    PyObject *result = PyUnicode_New(result_length, max_character);
    if (result == NULL) {
        goto error;
    }
    Py_ssize_t output_offset = 0;
    literal_start = 0;
    for (Py_ssize_t i = 0; i < reference_count; ++i) {
        Py_ssize_t literal_length = references[i].slash_index - literal_start;
        if (literal_length > 0) {
            if (PyUnicode_CopyCharacters(result,
                                         output_offset,
                                         template_obj,
                                         literal_start,
                                         literal_length) < 0) {
                Py_DECREF(result);
                goto error;
            }
            output_offset += literal_length;
        }
        if (group_lengths[i] > 0) {
            if (references[i].literal_backslash) {
                if (PyUnicode_WriteChar(result, output_offset, '\\') < 0) {
                    Py_DECREF(result);
                    goto error;
                }
            } else {
                PyObject *group_source = direct_groups
                    ? self->subject : groups[i];
                if (PyUnicode_CopyCharacters(result,
                                             output_offset,
                                             group_source,
                                             group_offsets[i],
                                             group_lengths[i]) < 0) {
                    Py_DECREF(result);
                    goto error;
                }
            }
            output_offset += group_lengths[i];
        }
        literal_start = references[i].reference_end;
    }
    Py_ssize_t suffix_length = template_length - literal_start;
    if (suffix_length > 0 &&
        PyUnicode_CopyCharacters(result,
                                 output_offset,
                                 template_obj,
                                 literal_start,
                                 suffix_length) < 0) {
        Py_DECREF(result);
        goto error;
    }
    for (Py_ssize_t i = 0; i < reference_count; ++i) {
        Py_XDECREF(groups[i]);
    }
    return result;

error:
    for (Py_ssize_t i = 0; i < reference_count; ++i) {
        Py_XDECREF(groups[i]);
    }
    return NULL;
}

static PyObject *
Match_expand(MatchObject *self, PyObject *template_obj)
{
    /* A template without a backslash has no group references or escapes.
       Return its validated text directly instead of importing the Python
       parser and allocating its intermediate representation.  This mirrors
       ``re.Match.expand`` for literal text while keeping subclass conversion
       in PyUnicode_FromObject. */
    if (!self->subject_is_bytes && PyUnicode_Check(template_obj)) {
        Py_ssize_t template_length = PyUnicode_GET_LENGTH(template_obj);
        Py_ssize_t slash_index = PyUnicode_FindChar(
            template_obj, '\\', 0, template_length, 1
        );
        if (slash_index < 0 && !PyErr_Occurred()) {
            return PyUnicode_FromObject(template_obj);
        }
        if (slash_index >= 0 && PyUnicode_CheckExact(template_obj)) {
            int handled = 0;
            PyObject *result = match_expand_simple_numeric(
                self, template_obj, slash_index, template_length, &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
            result = match_expand_explicit_numeric(
                self, template_obj, slash_index, template_length, &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
            result = match_expand_explicit_named(
                self, template_obj, slash_index, template_length, &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
            result = match_expand_multiple_tokens(
                self, template_obj, slash_index, template_length, &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
        }
        PyErr_Clear();
    }
    if (self->subject_is_bytes &&
        (PyBytes_Check(template_obj) || PyByteArray_Check(template_obj))) {
        const char *template_data = PyBytes_Check(template_obj)
            ? PyBytes_AS_STRING(template_obj)
            : (const char *)PyByteArray_AS_STRING(template_obj);
        Py_ssize_t template_length = PyBytes_Check(template_obj)
            ? PyBytes_GET_SIZE(template_obj)
            : PyByteArray_GET_SIZE(template_obj);
        const char *slash = memchr(template_data, '\\', (size_t)template_length);
        if (slash == NULL) {
            return PyBytes_FromObject(template_obj);
        }
        if (PyBytes_CheckExact(template_obj)) {
            int handled = 0;
            PyObject *result = match_expand_simple_numeric(
                self,
                template_obj,
                (Py_ssize_t)(slash - template_data),
                template_length,
                &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
            result = match_expand_explicit_numeric(
                self,
                template_obj,
                (Py_ssize_t)(slash - template_data),
                template_length,
                &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
            result = match_expand_explicit_named(
                self,
                template_obj,
                (Py_ssize_t)(slash - template_data),
                template_length,
                &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
            result = match_expand_multiple_tokens(
                self,
                template_obj,
                (Py_ssize_t)(slash - template_data),
                template_length,
                &handled
            );
            if (handled || result != NULL || PyErr_Occurred()) {
                return result;
            }
        }
    }

    /* Delegate template parsing to the Python compatibility helper. */
    PyObject *module = PyImport_ImportModule("pcre.re_compat");
    if (module == NULL) {
        return NULL;
    }

    /* The helper is a module function, so a direct dictionary lookup avoids
       attribute lookup machinery on every expand() call while retaining the
       module's normal import/refcount lifetime. */
    PyObject *helper = PyDict_GetItemString(
        PyModule_GetDict(module),
        "expand_match_template"
    );
    Py_XINCREF(helper);
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

static PyObject *
Match_subscript(MatchObject *self, PyObject *key)
{
    Py_ssize_t index = 0;
    if (resolve_group_key(self, key, &index) < 0) {
        return NULL;
    }
    return match_get_group_value(self, index);
}

static PyObject *
Match_copy(MatchObject *self, PyObject *Py_UNUSED(args))
{
    return Py_NewRef(self);
}

static PyObject *
Match_deepcopy(MatchObject *self, PyObject *Py_UNUSED(memo))
{
    return Py_NewRef(self);
}

static int
match_set_public_pattern(MatchObject *self, PyObject *public_pattern)
{
    /* The high-level wrapper reuses this C object and swaps in its owner here. */
    PyObject *old_pattern = NULL;
    Py_BEGIN_CRITICAL_SECTION(self);
    old_pattern = self->public_pattern;
    if (public_pattern != NULL) {
        Py_INCREF(public_pattern);
    }
    self->public_pattern = public_pattern;
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old_pattern);
    return 0;
}

static PyMethodDef Match_methods[] = {
    {"group", (PyCFunction)(void(*)(void))Match_group_fast, METH_FASTCALL, PyDoc_STR("Return one or more capture groups.")},
    {"groups", (PyCFunction)Match_groups, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return all capture groups as a tuple." )},
    {"groupdict", (PyCFunction)Match_groupdict, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return a dict for named capture groups." )},
    {"span", (PyCFunction)(void(*)(void))Match_span_fast, METH_FASTCALL, PyDoc_STR("Return the (start, end) span for a group." )},
    {"start", (PyCFunction)(void(*)(void))Match_start_fast, METH_FASTCALL, PyDoc_STR("Return the start index for a group." )},
    {"end", (PyCFunction)(void(*)(void))Match_end_fast, METH_FASTCALL, PyDoc_STR("Return the end index for a group." )},
    {"expand", (PyCFunction)Match_expand, METH_O, PyDoc_STR("Apply a replacement template to the match." )},
    {"__copy__", (PyCFunction)Match_copy, METH_NOARGS, NULL},
    {"__deepcopy__", (PyCFunction)Match_deepcopy, METH_O, NULL},
    {NULL, NULL, 0, NULL},
};

static PyMappingMethods Match_as_mapping = {
    .mp_subscript = (binaryfunc)Match_subscript,
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
    .tp_as_mapping = &Match_as_mapping,
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
    int retry_nonempty;
#if defined(Py_GIL_DISABLED)
    PyThread_type_lock lock;
#endif
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
                                        PCRE2_SIZE *ovector,
                                        uint32_t replay_options);


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
#if defined(Py_GIL_DISABLED)
    if (self->lock != NULL) {
        PyThread_free_lock(self->lock);
        self->lock = NULL;
    }
#endif
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
FindIter_iternext_unlocked(FindIterObject *self)
{
retry:
    if (self->exhausted) {
        return NULL;
    }

    if (self->current_pos > self->logical_length) {
        self->exhausted = 1;
        return NULL;
    }

    if (self->has_endpos && self->current_pos > self->resolved_end) {
        self->exhausted = 1;
        return NULL;
    }

    if (self->current_byte > self->subject_length_bytes) {
        self->exhausted = 1;
        return NULL;
    }

    const char *buffer = self->utf8_data;
    uint32_t options = self->base_options;
    if (self->retry_nonempty) {
        options |= PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;
    }
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

    int use_jit = pattern_jit_get(self->pattern) && !self->retry_nonempty;
    if (use_jit) {
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
        PCRE2_JIT_CALL_MAYBE_RELEASE_GIL(pcre2_jit_match(self->pattern->code,
                                                     (PCRE2_SPTR)buffer,
                                                     exec_length,
                                                     (PCRE2_SIZE)self->current_byte,
                                                     options,
                                                     self->match_data,
                                                     self->match_context),
                                                   exec_length);

        if (rc == PCRE2_ERROR_JIT_BADOPTION || rc == PCRE2_ERROR_BADOPTION) {
            pattern_jit_set(self->pattern, 0);
            use_jit = 0;
            if (self->jit_stack != NULL) {
                if (self->match_context != NULL) {
                    pcre2_jit_stack_assign(self->match_context, NULL, NULL);
                }
                jit_stack_cache_release(self->jit_stack);
                self->jit_stack = NULL;
            }
        } else if (rc == PCRE2_ERROR_NOMATCH) {
            goto no_match;
        } else if (rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(self->match_data);
            raise_pcre_error("jit_match", rc, error_offset);
            return NULL;
        } else if (rc >= 0) {
            goto matched;
        }
    }

    if (!use_jit) {
        PCRE2_CALL_MAYBE_RELEASE_GIL(pcre2_match(self->pattern->code,
                                                 (PCRE2_SPTR)buffer,
                                                 exec_length,
                                                 (PCRE2_SIZE)self->current_byte,
                                                 options,
                                                 self->match_data,
                                                 self->match_context),
                                               exec_length);

        if (rc == PCRE2_ERROR_NOMATCH) {
            goto no_match;
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
        ovector,
        options);
    if (match == NULL) {
        return NULL;
    }

    if (self->public_pattern != NULL) {
        if (match_set_public_pattern(match, self->public_pattern) < 0) {
            Py_DECREF(match);
            return NULL;
        }
    }

    self->current_pos = end_index;
    self->current_byte = end_byte;
    self->retry_nonempty = (end_index == start_index);

    self->byte_to_index_cached_index = self->current_pos;
    self->byte_to_index_cached_byte = self->current_byte;
    self->index_to_byte_cached_index = self->current_pos;
    self->index_to_byte_cached_byte = self->current_byte;

    return (PyObject *)match;

no_match:
    if (self->retry_nonempty) {
        self->retry_nonempty = 0;
        if (self->current_pos >= self->logical_length ||
            (self->has_endpos && self->current_pos >= self->resolved_end)) {
            self->exhausted = 1;
            return NULL;
        }

        if (self->subject_is_bytes) {
            int single_byte = (self->pattern->compile_options & PCRE2_UTF) == 0;
            self->current_byte = advance_one_character(
                self->utf8_data,
                self->subject_length_bytes,
                self->current_byte,
                single_byte
            );
            self->current_pos = self->current_byte;
        } else if (self->utf8_is_ascii) {
            self->current_pos += 1;
            self->current_byte += 1;
        } else {
            self->current_pos += 1;
            self->current_byte = finditer_index_to_byte(self, self->current_pos);
        }
        self->byte_to_index_cached_index = self->current_pos;
        self->byte_to_index_cached_byte = self->current_byte;
        self->index_to_byte_cached_index = self->current_pos;
        self->index_to_byte_cached_byte = self->current_byte;
        goto retry;
    }

    self->exhausted = 1;
    return NULL;
}

static PyObject *
FindIter_iternext(FindIterObject *self)
{
#if defined(Py_GIL_DISABLED)
    PyThread_acquire_lock(self->lock, WAIT_LOCK);
    PyObject *result = FindIter_iternext_unlocked(self);
    PyThread_release_lock(self->lock);
    return result;
#else
    return FindIter_iternext_unlocked(self);
#endif
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
                    PCRE2_SIZE *ovector,
                    uint32_t replay_options)
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
#if SIZE_MAX < UINT64_MAX
    if ((uint64_t)ovec_count > (uint64_t)(SIZE_MAX / sizeof(Py_ssize_t) / 2)) {
        PyErr_NoMemory();
        PyObject_Del(match);
        return NULL;
    }
#endif
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
    match->replay_options = replay_options;
    match->lastindex_cache = -2;
    match->regs_cache = NULL;
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
    iter->retry_nonempty = 0;
#if defined(Py_GIL_DISABLED)
    iter->lock = PyThread_allocate_lock();
    if (iter->lock == NULL) {
        PyErr_NoMemory();
        goto error;
    }
#endif

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
        PyObject *buf_view = buffer_bytes_from_object(subject_obj, &buf_data, &buf_length);
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

    if (ensure_subject_type_compatible(pattern, iter->subject_is_bytes) < 0) {
        goto error;
    }

    Py_ssize_t logical_length = iter->logical_length;

    if (pos < 0) {
        pos = 0;
    }
    if (pos > logical_length) {
        pos = logical_length;
    }

    Py_ssize_t resolved_end = logical_length;
    Py_ssize_t resolved_end_byte = iter->subject_length_bytes;
    int has_endpos = 0;

    int impossible_range = 0;
    if (endpos >= 0) {
        has_endpos = 1;
        if (endpos > logical_length) {
            endpos = logical_length;
        }
        if (endpos < pos) {
            impossible_range = 1;
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
    iter->exhausted = impossible_range;
    iter->retry_nonempty = 0;

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
#if defined(Py_GIL_DISABLED)
    if (iter->lock != NULL) {
        PyThread_free_lock(iter->lock);
        iter->lock = NULL;
    }
#endif
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
    pcre2_code *lastindex_code = atomic_exchange_explicit(
        &self->lastindex_replay_code,
        NULL,
        memory_order_acq_rel
    );
    if (lastindex_code != NULL) {
        pcre2_code_free(lastindex_code);
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
    if (self->lastindex_replay_code != NULL) {
        pcre2_code_free(self->lastindex_replay_code);
        self->lastindex_replay_code = NULL;
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
    return PyDictProxy_New(self->groupindex);
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
    PyObject *options_obj = NULL;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnOO", kwlist,
                                     &subject, &pos, &endpos, &options_obj, &owner)) {
        return NULL;
    }

    uint32_t options = 0;
    if (coerce_uint32_argument(options_obj, "options", &options) < 0) {
        return NULL;
    }

    return Pattern_create_finditer(self, subject, pos, endpos, options, owner);
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
        PyObject *buf_view = buffer_bytes_from_object(subject_obj, &buf_data, &buf_length);
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

    if (ensure_subject_type_compatible(self, subject_is_bytes) < 0) {
        Py_DECREF(utf8_owner);
        return NULL;
    }

    if (pos < 0) {
        pos = 0;
    }
    if (pos > logical_length) {
        pos = logical_length;
    }

    Py_ssize_t adjusted_endpos = endpos;
    if (adjusted_endpos >= 0) {
        if (adjusted_endpos > logical_length) {
            adjusted_endpos = logical_length;
        }
        if (adjusted_endpos < pos) {
            Py_DECREF(utf8_owner);
            Py_RETURN_NONE;
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

        PCRE2_JIT_CALL_MAYBE_RELEASE_GIL(pcre2_jit_match(self->code,
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
        ovector,
        match_options);

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
    PyObject *options_obj = NULL;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnOO", kwlist,
                                     &subject, &pos, &endpos, &options_obj, &owner)) {
        return NULL;
    }

    uint32_t options = 0;
    if (coerce_uint32_argument(options_obj, "options", &options) < 0) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, options, EXEC_MODE_MATCH, owner);
}

static PyObject *
Pattern_search_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", "owner", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    PyObject *options_obj = NULL;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnOO", kwlist,
                                     &subject, &pos, &endpos, &options_obj, &owner)) {
        return NULL;
    }

    uint32_t options = 0;
    if (coerce_uint32_argument(options_obj, "options", &options) < 0) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, options, EXEC_MODE_SEARCH, owner);
}

static PyObject *
Pattern_fullmatch_method(PatternObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"subject", "pos", "endpos", "options", "owner", NULL};
    PyObject *subject = NULL;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = -1;
    PyObject *options_obj = NULL;
    PyObject *owner = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnOO", kwlist,
                                     &subject, &pos, &endpos, &options_obj, &owner)) {
        return NULL;
    }

    uint32_t options = 0;
    if (coerce_uint32_argument(options_obj, "options", &options) < 0) {
        return NULL;
    }

    return Pattern_execute(self, subject, pos, endpos, options, EXEC_MODE_FULLMATCH, owner);
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
        return extract_findall_group_from_offsets(utf8_data, subject_is_bytes,
                                                   subject_is_ascii, start, end);
    }

    PyObject *tuple = PyTuple_New((Py_ssize_t)ovec_count - 1);
    if (tuple == NULL) {
        return NULL;
    }
    for (uint32_t i = 1; i < ovec_count; ++i) {
        Py_ssize_t start = (Py_ssize_t)ovector[(size_t)i * 2];
        Py_ssize_t end = (Py_ssize_t)ovector[(size_t)i * 2 + 1];
        PyObject *value = extract_findall_group_from_offsets(utf8_data, subject_is_bytes,
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
        PyObject *buf_view = buffer_bytes_from_object(subject_obj, &buf_data, &buf_length);
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

    if (ensure_subject_type_compatible(self, subject_is_bytes) < 0) {
        goto error;
    }

    if (pos < 0) {
        pos = 0;
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
            result = PyList_New(0);
            goto cleanup;
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
    int retry_nonempty = 0;
    /*
     * A findall scan usually performs one expensive PCRE2 call followed by
     * Python object construction.  Release the GIL for that first large
     * scan, but stop doing so after a match: patterns such as ``.`` can
     * produce millions of tiny matches and repeatedly saving/restoring the
     * GIL would cost more than the matcher.  Each worker owns its match data
     * and keeps ``utf8_owner`` alive, so the PCRE2 call remains thread-safe.
     */
    int release_gil_for_match =
        subject_length_bytes > (Py_ssize_t)PCRE2_GIL_RELEASE_THRESHOLD;

    while (1) {
        if (current_byte > subject_length_bytes) {
            break;
        }
        if (has_endpos && current_pos > adjusted_endpos) {
            break;
        }
        if (!has_endpos && current_pos > logical_length) {
            break;
        }
        if (has_endpos && current_byte > byte_end) {
            break;
        }

        int rc = 0;
        uint32_t current_options = match_options;
        if (retry_nonempty) {
            current_options |= PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;
        }
        int use_jit = attempt_jit && !retry_nonempty;

        if (use_jit) {
            if (release_gil_for_match) {
                PCRE2_JIT_CALL_MAYBE_RELEASE_GIL(pcre2_jit_match(self->code,
                                                                 (PCRE2_SPTR)utf8_data,
                                                                 exec_length,
                                                                 (PCRE2_SIZE)current_byte,
                                                                 current_options,
                                                                 match_data,
                                                                 match_context),
                                                 exec_length);
            } else {
                jit_guard_acquire();
                rc = pcre2_jit_match(self->code,
                                     (PCRE2_SPTR)utf8_data,
                                     exec_length,
                                     (PCRE2_SIZE)current_byte,
                                     current_options,
                                     match_data,
                                     match_context);
                jit_guard_release();
            }

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
                goto findall_no_match;
            } else if (rc < 0) {
                PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
                raise_pcre_error("jit_match", rc, error_offset);
                goto error;
            }
        }

        if (!use_jit) {
            if (release_gil_for_match) {
                PCRE2_CALL_MAYBE_RELEASE_GIL(pcre2_match(self->code,
                                                         (PCRE2_SPTR)utf8_data,
                                                         exec_length,
                                                         (PCRE2_SIZE)current_byte,
                                                         current_options,
                                                         match_data,
                                                         match_context),
                                             exec_length);
            } else {
                rc = pcre2_match(self->code,
                                 (PCRE2_SPTR)utf8_data,
                                 exec_length,
                                 (PCRE2_SIZE)current_byte,
                                 current_options,
                                 match_data,
                                 match_context);
            }

            if (rc == PCRE2_ERROR_NOMATCH) {
                goto findall_no_match;
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
        release_gil_for_match = 0;
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

        current_pos = end_index;
        current_byte = end_byte;
        retry_nonempty = (start_byte == end_byte);
        continue;

findall_no_match:
        if (!retry_nonempty) {
            break;
        }
        retry_nonempty = 0;
        if (current_pos >= logical_length ||
            (has_endpos && current_pos >= adjusted_endpos)) {
            break;
        }
        int single_byte = subject_is_bytes
            ? (self->compile_options & PCRE2_UTF) == 0
            : subject_is_ascii;
        current_byte = advance_one_character(utf8_data,
                                             subject_length_bytes,
                                             current_byte,
                                             single_byte);
        if (subject_is_bytes) {
            current_pos = current_byte;
        } else {
            current_pos += 1;
        }
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
    PyObject *options_obj = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nnO", kwlist,
                                     &subject, &pos, &endpos, &options_obj)) {
        return NULL;
    }

    uint32_t options = 0;
    if (coerce_uint32_argument(options_obj, "options", &options) < 0) {
        return NULL;
    }

    return Pattern_findall(self, subject, pos, endpos, options);
}

typedef struct {
    uint32_t limit;
    int stopped;
} SubstituteLimitState;

enum {
    SUBSTITUTE_REPLACEMENT_GENERAL = 0,
    SUBSTITUTE_REPLACEMENT_LITERAL = 1,
    SUBSTITUTE_REPLACEMENT_SINGLE_REFERENCE = 2,
};

static int PCRE2_CALL_CONVENTION
bounded_substitute_callout(pcre2_substitute_callout_block *block, void *data)
{
    SubstituteLimitState *state = (SubstituteLimitState *)data;
    if (block->subscount > state->limit) {
        state->stopped = 1;
        return -1;
    }
    return 0;
}

static PyObject *
Pattern_substitute(PatternObject *self,
                   PyObject *subject_obj,
                   PyObject *repl_obj,
                   Py_ssize_t count,
                   int replacement_shape)
{
    PyObject *result = NULL;
    PyObject *result_tuple = NULL;
    PyObject *utf8_owner = NULL;
    const char *subject_data = NULL;
    Py_ssize_t subject_length = 0;
    int subject_is_bytes = 0;

    const char *repl_data = NULL;
    Py_ssize_t repl_length = 0;
    int repl_is_bytes = 0;

    pcre2_match_data *match_data = NULL;
    pcre2_match_context *match_context = NULL;
    pcre2_jit_stack *jit_stack = NULL;
    int match_data_from_pattern = 0;
    int match_context_from_pattern = 0;
    int substitute_callout_installed = 0;

    if (count < 0 || count > 8 ||
        (count > 1 && replacement_shape == SUBSTITUTE_REPLACEMENT_GENERAL)) {
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
        PyObject *buf_view = buffer_bytes_from_object(subject_obj, &buf_data, &buf_length);
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

    if (ensure_subject_type_compatible(self, subject_is_bytes) < 0) {
        goto error;
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

    if (pattern_jit_get(self) || count > 1) {
        match_context = pattern_match_context_acquire(self, 0, &match_context_from_pattern);
        if (match_context == NULL) {
            PyErr_NoMemory();
            goto error;
        }
    }

    SubstituteLimitState limit_state = {(uint32_t)count, 0};

    if (count > 1) {
        int callout_rc = pcre2_set_substitute_callout(
            match_context,
            bounded_substitute_callout,
            &limit_state
        );
        if (callout_rc < 0) {
            raise_pcre_error("set_substitute_callout", callout_rc, 0);
            goto error;
        }
        substitute_callout_installed = 1;
    }

    if (pattern_jit_get(self)) {
        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            PyErr_NoMemory();
            goto error;
        }
        pcre2_jit_stack_assign(match_context, NULL, jit_stack);
    }

    uint32_t sub_options = PCRE2_SUBSTITUTE_EXTENDED
                         | PCRE2_SUBSTITUTE_UNSET_EMPTY
                         | PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
    if (count != 1) {
        sub_options |= PCRE2_SUBSTITUTE_GLOBAL;
    }
    if (!subject_is_bytes) {
        sub_options |= PCRE2_NO_UTF_CHECK;
    }

    if (repl_length > PY_SSIZE_T_MAX - 16 ||
        subject_length > PY_SSIZE_T_MAX - repl_length - 16) {
        PyErr_NoMemory();
        goto error;
    }
    PCRE2_SIZE initial_outlen = (PCRE2_SIZE)(subject_length + repl_length + 16);
    PCRE2_SIZE bounded_max_outlen = 0;
    if (count > 1) {
        if (repl_length > (PY_SSIZE_T_MAX - subject_length - 16) / count) {
            PyErr_NoMemory();
            goto error;
        }
        initial_outlen = (PCRE2_SIZE)(
            subject_length + count * repl_length + 16
        );

        if (replacement_shape == SUBSTITUTE_REPLACEMENT_LITERAL) {
            bounded_max_outlen = initial_outlen;
        } else {
            /* Exactly one capture can contribute at most one whole subject
             * per accepted replacement. The first allocation uses the usual
             * compact bound; only a genuine expansion overflow grows
             * geometrically toward this strict linear ceiling. */
            if (subject_length > PY_SSIZE_T_MAX - repl_length) {
                PyErr_NoMemory();
                goto error;
            }
            Py_ssize_t per_replacement = subject_length + repl_length;
            if (per_replacement >
                (PY_SSIZE_T_MAX - subject_length - 16) / count) {
                PyErr_NoMemory();
                goto error;
            }
            bounded_max_outlen = (PCRE2_SIZE)(
                subject_length + count * per_replacement + 16
            );
        }
        if (bounded_max_outlen < initial_outlen) {
            PyErr_NoMemory();
            goto error;
        }
    }
    PCRE2_SIZE outlen = initial_outlen;
    PCRE2_SIZE out_capacity = initial_outlen;
    PCRE2_UCHAR *out = (PCRE2_UCHAR *)PyMem_Malloc(outlen);
    if (out == NULL) {
        PyErr_NoMemory();
        goto error;
    }

    for (int attempts = 0; attempts < 5; ++attempts) {
        limit_state.stopped = 0;
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
            if (count > 1) {
                if (out_capacity >= bounded_max_outlen) {
                    PyMem_Free(out);
                    PyErr_NoMemory();
                    goto error;
                }
                required = out_capacity <= bounded_max_outlen / 2
                    ? out_capacity * 2
                    : bounded_max_outlen;
            } else if (required == (PCRE2_SIZE)-1) {
                if ((PCRE2_SIZE)subject_length > (PCRE2_SIZE)PY_SSIZE_T_MAX - initial_outlen) {
                    PyMem_Free(out);
                    PyErr_NoMemory();
                    goto error;
                }
                required = initial_outlen + (PCRE2_SIZE)subject_length;
            }
            if (required < initial_outlen) {
                required = initial_outlen;
            }
            if (required > (PCRE2_SIZE)PY_SSIZE_T_MAX) {
                PyMem_Free(out);
                PyErr_NoMemory();
                goto error;
            }
            void *new_out = PyMem_Realloc(out, required);
            if (new_out == NULL) {
                PyMem_Free(out);
                PyErr_NoMemory();
                goto error;
            }
            out = (PCRE2_UCHAR *)new_out;
            out_capacity = required;
            outlen = required;
            continue;
        }
        if (rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
            PyMem_Free(out);
            raise_pcre_error("substitute", rc, error_offset);
            goto error;
        }
        if (count > 1 && limit_state.stopped && rc > 0) {
            /* PCRE2 includes the rejected stopping match in its return value;
             * public subn() counts accepted replacements only. */
            rc -= 1;
        }

        PyObject *out_obj = NULL;
        if (subject_is_bytes) {
            out_obj = PyBytes_FromStringAndSize((const char *)out, (Py_ssize_t)outlen);
        } else if (ascii_prefix_length((const char *)out, (Py_ssize_t)outlen) ==
                   (Py_ssize_t)outlen) {
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
    if (substitute_callout_installed && match_context != NULL) {
        int clear_rc = pcre2_set_substitute_callout(match_context, NULL, NULL);
        if (clear_rc < 0) {
            /* Never publish a context that could retain a pointer to the
             * stack-local limit state, even if a future PCRE2 build reports a
             * failure while clearing the callout. */
            pcre2_match_context_free(match_context);
            match_context = NULL;
        }
    }
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

    return Pattern_substitute(
        self,
        subject,
        replacement,
        count,
        SUBSTITUTE_REPLACEMENT_GENERAL
    );
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
        PyObject *buf_view = buffer_bytes_from_object(subject_obj, &buf_data, &buf_length);
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

    if (ensure_subject_type_compatible(self, subject_is_bytes) < 0) {
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
    int retry_nonempty = 0;

    while (1) {
        if (maxsplit < 0) {
            break;
        }
        if (maxsplit > 0 && splits_done >= maxsplit) {
            break;
        }
        if (current_byte > subject_length_bytes) {
            break;
        }

        int rc = 0;
        uint32_t current_options = options;
        if (retry_nonempty) {
            current_options |= PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;
        }
        int use_jit = attempt_jit && !retry_nonempty;
        if (use_jit) {
            jit_guard_acquire();
            rc = pcre2_jit_match(self->code,
                                 (PCRE2_SPTR)utf8_data,
                                 (PCRE2_SIZE)subject_length_bytes,
                                 (PCRE2_SIZE)current_byte,
                                 current_options,
                                 match_data,
                                 match_context);
            jit_guard_release();
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
                goto split_no_match;
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
                             current_options,
                             match_data,
                             match_context);
            if (rc == PCRE2_ERROR_NOMATCH) {
                goto split_no_match;
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
        current_byte = end_byte;
        retry_nonempty = (start_byte == end_byte);
        continue;

split_no_match:
        if (!retry_nonempty) {
            break;
        }
        retry_nonempty = 0;
        if (current_byte >= subject_length_bytes) {
            break;
        }
        int single_byte = subject_is_bytes
            ? (self->compile_options & PCRE2_UTF) == 0
            : subject_is_ascii;
        current_byte = advance_one_character(utf8_data,
                                             subject_length_bytes,
                                             current_byte,
                                             single_byte);
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

/*
 * Private vectorcall entry points used by the Python wrapper's default-shape
 * dispatch.  The public methods retain their keyword-compatible ABI; these
 * helpers only remove temporary tuple/keyword objects from the allocation-heavy
 * findall/substitute paths.  Replacement and match ownership remain unchanged.
 */
static PyObject *
Pattern_findall_fast(PatternObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1) {
        PyErr_Format(PyExc_TypeError,
                     "_findall_fast() takes exactly 1 positional argument (%zd given)",
                     nargs);
        return NULL;
    }
    return Pattern_findall(self, args[0], 0, -1, 0);
}

static PyObject *
Pattern_substitute_fast(PatternObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2 && nargs != 3) {
        PyErr_Format(PyExc_TypeError,
                     "_substitute_fast() takes 2 or 3 positional arguments (%zd given)",
                     nargs);
        return NULL;
    }
    Py_ssize_t count = 0;
    if (nargs == 3) {
        count = PyLong_AsSsize_t(args[2]);
        if (count == -1 && PyErr_Occurred()) {
            return NULL;
        }
    }
    int replacement_shape = SUBSTITUTE_REPLACEMENT_GENERAL;
    if (count > 1) {
        if (PyBytes_CheckExact(args[1])) {
            const char *data = PyBytes_AS_STRING(args[1]);
            Py_ssize_t length = PyBytes_GET_SIZE(args[1]);
            if (memchr(data, '\\', (size_t)length) == NULL &&
                memchr(data, '$', (size_t)length) == NULL) {
                replacement_shape = SUBSTITUTE_REPLACEMENT_LITERAL;
            }
        } else if (PyUnicode_CheckExact(args[1]) &&
                   PyUnicode_FindChar(
                       args[1], '\\', 0, PyUnicode_GET_LENGTH(args[1]), 1
                   ) < 0 &&
                   PyUnicode_FindChar(
                       args[1], '$', 0, PyUnicode_GET_LENGTH(args[1]), 1
                   ) < 0) {
            replacement_shape = SUBSTITUTE_REPLACEMENT_LITERAL;
        }
    }
    return Pattern_substitute(
        self, args[0], args[1], count, replacement_shape
    );
}

static int
pattern_has_ascii_group_name(PatternObject *self,
                             const char *name,
                             Py_ssize_t name_length)
{
    uint32_t name_count = 0;
    uint32_t entry_size = 0;
    PCRE2_SPTR name_table = NULL;
    if (name_length <= 0 ||
        pcre2_pattern_info(self->code, PCRE2_INFO_NAMECOUNT, &name_count) != 0 ||
        pcre2_pattern_info(self->code,
                           PCRE2_INFO_NAMEENTRYSIZE,
                           &entry_size) != 0 ||
        pcre2_pattern_info(self->code,
                           PCRE2_INFO_NAMETABLE,
                           &name_table) != 0 ||
        name_table == NULL || entry_size < 3) {
        return 0;
    }
    size_t name_max = (size_t)entry_size - 2;
    for (uint32_t i = 0; i < name_count; ++i) {
        const char *entry_name = (const char *)(
            name_table + (size_t)i * entry_size + 2
        );
        size_t entry_length = strnlen(entry_name, name_max);
        if (entry_length == (size_t)name_length &&
            memcmp(entry_name, name, entry_length) == 0) {
            return 1;
        }
    }
    return 0;
}

static Py_UCS4
replacement_character_at(PyObject *replacement,
                         int replacement_is_bytes,
                         Py_ssize_t index)
{
    return replacement_is_bytes
        ? (unsigned char)PyBytes_AS_STRING(replacement)[index]
        : PyUnicode_ReadChar(replacement, index);
}

static Py_ssize_t
replacement_find_character(PyObject *replacement,
                           int replacement_is_bytes,
                           Py_UCS4 character,
                           Py_ssize_t start,
                           Py_ssize_t length)
{
    if (!replacement_is_bytes) {
        return PyUnicode_FindChar(replacement, character, start, length, 1);
    }
    const char *data = PyBytes_AS_STRING(replacement);
    const char *found = memchr(
        data + start, (unsigned char)character, (size_t)(length - start)
    );
    return found == NULL ? -1 : (Py_ssize_t)(found - data);
}

static PyObject *
pattern_translate_single_replacement(PatternObject *self,
                                     PyObject *replacement,
                                     int *handled)
{
    *handled = 0;
    int replacement_is_bytes = PyBytes_CheckExact(replacement);
    if (!replacement_is_bytes && !PyUnicode_CheckExact(replacement)) {
        return NULL;
    }
    if (!replacement_is_bytes && PyUnicode_READY(replacement) < 0) {
        return NULL;
    }
    Py_ssize_t length = replacement_is_bytes
        ? PyBytes_GET_SIZE(replacement) : PyUnicode_GET_LENGTH(replacement);
    Py_ssize_t slash_index = replacement_find_character(
        replacement, replacement_is_bytes, '\\', 0, length
    );
    if (slash_index < 0 ||
        replacement_find_character(
            replacement, replacement_is_bytes, '$', 0, length
        ) >= 0 ||
        slash_index + 1 >= length) {
        return NULL;
    }

    Py_UCS4 following = replacement_character_at(
        replacement, replacement_is_bytes, slash_index + 1
    );
    if (following >= '1' && following <= '9') {
        if ((uint32_t)(following - '0') > self->capture_count ||
            (slash_index + 2 < length &&
             replacement_character_at(
                 replacement, replacement_is_bytes, slash_index + 2
             ) >= '0' &&
             replacement_character_at(
                 replacement, replacement_is_bytes, slash_index + 2
             ) <= '9') ||
            replacement_find_character(
                replacement,
                replacement_is_bytes,
                '\\',
                slash_index + 2,
                length
            ) >= 0) {
            return NULL;
        }
        if (length > PY_SSIZE_T_MAX - 3) {
            PyErr_NoMemory();
            return NULL;
        }
        *handled = 1;
        Py_ssize_t result_length = length + 3;
        if (replacement_is_bytes) {
            PyObject *result = PyBytes_FromStringAndSize(NULL, result_length);
            if (result == NULL) {
                return NULL;
            }
            char *output = PyBytes_AS_STRING(result);
            const char *input = PyBytes_AS_STRING(replacement);
            memcpy(output, input, (size_t)slash_index);
            output[slash_index] = '\\';
            output[slash_index + 1] = 'g';
            output[slash_index + 2] = '<';
            output[slash_index + 3] = (char)following;
            output[slash_index + 4] = '>';
            memcpy(output + slash_index + 5,
                   input + slash_index + 2,
                   (size_t)(length - slash_index - 2));
            return result;
        }

        PyObject *result = PyUnicode_New(
            result_length, PyUnicode_MAX_CHAR_VALUE(replacement)
        );
        if (result == NULL) {
            return NULL;
        }
        if ((slash_index > 0 &&
             PyUnicode_CopyCharacters(
                 result, 0, replacement, 0, slash_index
             ) < 0) ||
            PyUnicode_WriteChar(result, slash_index, '\\') < 0 ||
            PyUnicode_WriteChar(result, slash_index + 1, 'g') < 0 ||
            PyUnicode_WriteChar(result, slash_index + 2, '<') < 0 ||
            PyUnicode_WriteChar(result, slash_index + 3, following) < 0 ||
            PyUnicode_WriteChar(result, slash_index + 4, '>') < 0 ||
            (slash_index + 2 < length &&
             PyUnicode_CopyCharacters(result,
                                      slash_index + 5,
                                      replacement,
                                      slash_index + 2,
                                      length - slash_index - 2) < 0)) {
            Py_DECREF(result);
            return NULL;
        }
        return result;
    }

    if (following != 'g' || slash_index + 4 >= length ||
        replacement_character_at(
            replacement, replacement_is_bytes, slash_index + 2
        ) != '<') {
        return NULL;
    }
    char name[129];
    Py_ssize_t name_length = 0;
    uint32_t group_index = 0;
    int numeric = 1;
    Py_ssize_t cursor = slash_index + 3;
    while (cursor < length) {
        Py_UCS4 character = replacement_character_at(
            replacement, replacement_is_bytes, cursor
        );
        if (character == '>') {
            break;
        }
        if (character > 0x7f || name_length >= 128) {
            return NULL;
        }
        name[name_length++] = (char)character;
        if (character < '0' || character > '9') {
            numeric = 0;
        } else if (numeric) {
            uint32_t digit = (uint32_t)(character - '0');
            if (group_index > (UINT32_MAX - digit) / 10) {
                return NULL;
            }
            group_index = group_index * 10 + digit;
        }
        cursor += 1;
    }
    if (name_length == 0 || cursor >= length ||
        replacement_find_character(
            replacement,
            replacement_is_bytes,
            '\\',
            cursor + 1,
            length
        ) >= 0 ||
        (numeric
            ? group_index > self->capture_count
            : !pattern_has_ascii_group_name(self, name, name_length))) {
        return NULL;
    }
    *handled = 1;
    Py_INCREF(replacement);
    return replacement;
}

static PyObject *
Pattern_substitute_python_fast(PatternObject *self,
                               PyObject *const *args,
                               Py_ssize_t nargs)
{
    if (nargs != 2 && nargs != 3) {
        PyErr_Format(PyExc_TypeError,
                     "_substitute_python_fast() takes 2 or 3 positional arguments (%zd given)",
                     nargs);
        return NULL;
    }
    Py_ssize_t count = 0;
    if (nargs == 3) {
        count = PyLong_AsSsize_t(args[2]);
        if (count == -1 && PyErr_Occurred()) {
            return NULL;
        }
    }
    int handled = 0;
    PyObject *replacement = pattern_translate_single_replacement(
        self, args[1], &handled
    );
    if (replacement == NULL) {
        if (PyErr_Occurred()) {
            return NULL;
        }
        Py_RETURN_NOTIMPLEMENTED;
    }
    PyObject *result = Pattern_substitute(
        self,
        args[0],
        replacement,
        count,
        SUBSTITUTE_REPLACEMENT_SINGLE_REFERENCE
    );
    Py_DECREF(replacement);
    return result;
}

/*
 * These lookup helpers are intentionally private.  They are used only by the
 * high-level parallel fan-out when every optional argument has its default
 * value, avoiding a Python wrapper call and its keyword dictionary per item.
 * The owner is still supplied so Match.re retains the public Pattern wrapper.
 */
static PyObject *
Pattern_lookup_fast(PatternObject *self,
                   PyObject *const *args,
                   Py_ssize_t nargs,
                   int mode,
                   const char *name)
{
    if (nargs != 2) {
        PyErr_Format(PyExc_TypeError,
                     "%s() takes exactly 2 positional arguments (%zd given)",
                     name, nargs);
        return NULL;
    }
    return Pattern_execute(self, args[0], 0, -1, 0, mode, args[1]);
}

static PyObject *
Pattern_match_fast(PatternObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    return Pattern_lookup_fast(self, args, nargs, EXEC_MODE_MATCH, "_match_fast");
}

static PyObject *
Pattern_search_fast(PatternObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    return Pattern_lookup_fast(self, args, nargs, EXEC_MODE_SEARCH, "_search_fast");
}

static PyObject *
Pattern_fullmatch_fast(PatternObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    return Pattern_lookup_fast(self, args, nargs, EXEC_MODE_FULLMATCH, "_fullmatch_fast");
}

static PyObject *
Pattern_finditer_fast(PatternObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 5) {
        PyErr_Format(PyExc_TypeError,
                     "_finditer_fast() takes exactly 5 positional arguments (%zd given)",
                     nargs);
        return NULL;
    }

    Py_ssize_t pos = PyLong_AsSsize_t(args[1]);
    if (pos == (Py_ssize_t)-1 && PyErr_Occurred()) {
        return NULL;
    }
    Py_ssize_t endpos = PyLong_AsSsize_t(args[2]);
    if (endpos == (Py_ssize_t)-1 && PyErr_Occurred()) {
        return NULL;
    }
    uint32_t options = 0;
    if (coerce_uint32_argument(args[3], "options", &options) < 0) {
        return NULL;
    }
    return Pattern_create_finditer(self, args[0], pos, endpos, options, args[4]);
}

static PyObject *
Pattern_split_fast(PatternObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2) {
        PyErr_Format(PyExc_TypeError,
                     "_split_fast() takes exactly 2 positional arguments (%zd given)",
                     nargs);
        return NULL;
    }
    Py_ssize_t maxsplit = PyLong_AsSsize_t(args[1]);
    if (maxsplit == (Py_ssize_t)-1 && PyErr_Occurred()) {
        return NULL;
    }
    return Pattern_split(self, args[0], maxsplit);
}

static PyMethodDef Pattern_methods[] = {
    {"findall", (PyCFunction)Pattern_findall_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return a list of all non-overlapping matches.")},
    {"substitute", (PyCFunction)Pattern_substitute_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Fast substitution using pcre2_substitute.")},
    {"finditer", (PyCFunction)Pattern_finditer_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Return an iterator over successive matches.")},
    {"split", (PyCFunction)Pattern_split_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Split the subject by occurrences of the pattern.")},
    {"match", (PyCFunction)Pattern_match_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Match the pattern at the start of the subject.")},
    {"search", (PyCFunction)Pattern_search_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Search the subject for the pattern." )},
    {"fullmatch", (PyCFunction)Pattern_fullmatch_method, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Require the pattern to match the entire subject." )},
    {"_findall_fast", (PyCFunction)(void(*)(void))Pattern_findall_fast, METH_FASTCALL, NULL},
    {"_substitute_fast", (PyCFunction)(void(*)(void))Pattern_substitute_fast, METH_FASTCALL, NULL},
    {"_substitute_python_fast", (PyCFunction)(void(*)(void))Pattern_substitute_python_fast, METH_FASTCALL, NULL},
    {"_match_fast", (PyCFunction)(void(*)(void))Pattern_match_fast, METH_FASTCALL, NULL},
    {"_search_fast", (PyCFunction)(void(*)(void))Pattern_search_fast, METH_FASTCALL, NULL},
    {"_fullmatch_fast", (PyCFunction)(void(*)(void))Pattern_fullmatch_fast, METH_FASTCALL, NULL},
    {"_finditer_fast", (PyCFunction)(void(*)(void))Pattern_finditer_fast, METH_FASTCALL, NULL},
    {"_split_fast", (PyCFunction)(void(*)(void))Pattern_split_fast, METH_FASTCALL, NULL},
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

static uint32_t
apply_leading_inline_options(const char *pattern, Py_ssize_t length, uint32_t options)
{
    Py_ssize_t offset = 0;
    while (length - offset >= 4 && pattern[offset] == '(' && pattern[offset + 1] == '?') {
        Py_ssize_t cursor = offset + 2;
        int clearing = 0;
        uint32_t set_mask = 0;
        uint32_t clear_mask = 0;
        int valid = 1;
        while (cursor < length && pattern[cursor] != ')') {
            char flag = pattern[cursor++];
            if (flag == '-') {
                clearing = 1;
                continue;
            }
            uint32_t bit = 0;
            switch (flag) {
                case 'i': bit = PCRE2_CASELESS; break;
                case 'm': bit = PCRE2_MULTILINE; break;
                case 's': bit = PCRE2_DOTALL; break;
                case 'x': bit = PCRE2_EXTENDED; break;
                case 'J': bit = PCRE2_DUPNAMES; break;
                case 'U': bit = PCRE2_UNGREEDY; break;
                case 'n': bit = PCRE2_NO_AUTO_CAPTURE; break;
                default:
                    valid = 0;
                    break;
            }
            if (!valid) {
                break;
            }
            if (clearing) {
                clear_mask |= bit;
            } else {
                set_mask |= bit;
            }
        }
        if (!valid || cursor >= length || pattern[cursor] != ')') {
            break;
        }
        options = (options | set_mask) & ~clear_mask;
        offset = cursor + 1;
    }
    return options;
}

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
#if defined(PCRE2_NEVER_BACKSLASH_C)
    if (!is_bytes) {
        compile_options |= PCRE2_NEVER_BACKSLASH_C;
    }
#endif

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
    atomic_store_explicit(&pattern->lastindex_replay_code, NULL, memory_order_relaxed);
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
    pattern->lastindex_replay_code = NULL;
#endif

    pattern->code = code;
    Py_INCREF(pattern_obj);
    pattern->pattern = pattern_obj;
    pattern->pattern_bytes = pattern_bytes;
    pattern->pattern_is_bytes = is_bytes;
    pattern->original_compile_options = compile_options;
    pattern->compile_options = compile_options;
    pattern_jit_set(pattern, 0);
    pattern->has_first_literal = 0;
    pattern->first_literal = 0;
    pattern->first_literal_caseless = 0;

    uint32_t effective_options = compile_options;
    if (pcre2_pattern_info(code, PCRE2_INFO_ALLOPTIONS, &effective_options) == 0) {
        pattern->compile_options = effective_options;
    }
    pattern->compile_options = apply_leading_inline_options(
        PyBytes_AS_STRING(pattern_bytes),
        pattern_length,
        pattern->compile_options
    );
    pattern->first_literal_caseless =
        (pattern->compile_options & PCRE2_CASELESS) != 0;

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
        jit_guard_acquire();
        int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
        jit_guard_release();
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
    PyObject *jit_explicit_bool = NULL;
    PyObject *cache_key = NULL;
    int use_cache = PyUnicode_CheckExact(pattern_obj) || PyBytes_CheckExact(pattern_obj);
    if (use_cache) {
        Py_ssize_t cache_units = PyUnicode_CheckExact(pattern_obj)
            ? PyUnicode_GET_LENGTH(pattern_obj)
            : PyBytes_GET_SIZE(pattern_obj);
        if (cache_units > PCRE_PATTERN_CACHE_INPUT_LIMIT) {
            use_cache = 0;
        }
    }
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
    jit_explicit_bool = PyBool_FromLong(jit_explicit != 0);
    if (jit_explicit_bool == NULL) {
        Py_DECREF(flags_obj);
        Py_DECREF(jit_bool);
        return NULL;
    }

    cache_key = PyTuple_Pack(4, pattern_obj, flags_obj, jit_bool, jit_explicit_bool);
    Py_DECREF(flags_obj);
    Py_DECREF(jit_bool);
    Py_DECREF(jit_explicit_bool);
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
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O$O", kwlist, &pattern, &flags_obj, &jit_obj)) {
        return NULL;
    }

    uint32_t flags = 0;
    if (coerce_uint32_argument(flags_obj, "flags", &flags) < 0) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *compiled = Pattern_compile_cached(pattern, flags, jit, jit_explicit);
    return (PyObject *)compiled;
}

static PyObject *
module_match(PyObject *Py_UNUSED(module), PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"pattern", "string", "flags", "jit", NULL};
    PyObject *pattern_obj = NULL;
    PyObject *subject = NULL;
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|O$O", kwlist, &pattern_obj, &subject, &flags_obj, &jit_obj)) {
        return NULL;
    }
    uint32_t flags = 0;
    if (coerce_uint32_argument(flags_obj, "flags", &flags) < 0) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *pattern = Pattern_compile_cached(pattern_obj, flags, jit, jit_explicit);
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
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|O$O", kwlist, &pattern_obj, &subject, &flags_obj, &jit_obj)) {
        return NULL;
    }
    uint32_t flags = 0;
    if (coerce_uint32_argument(flags_obj, "flags", &flags) < 0) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *pattern = Pattern_compile_cached(pattern_obj, flags, jit, jit_explicit);
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
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|O$O", kwlist, &pattern_obj, &subject, &flags_obj, &jit_obj)) {
        return NULL;
    }
    uint32_t flags = 0;
    if (coerce_uint32_argument(flags_obj, "flags", &flags) < 0) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *pattern = Pattern_compile_cached(pattern_obj, flags, jit, jit_explicit);
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
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|O$O", kwlist,
                                     &pattern_obj, &subject, &flags_obj, &jit_obj)) {
        return NULL;
    }
    uint32_t flags = 0;
    if (coerce_uint32_argument(flags_obj, "flags", &flags) < 0) {
        return NULL;
    }

    int jit = 0;
    int jit_explicit = 0;
    int current_default = default_jit_get();
    if (coerce_jit_argument(jit_obj, current_default, &jit, &jit_explicit) < 0) {
        return NULL;
    }

    PatternObject *pattern = Pattern_compile_cached(pattern_obj, flags, jit, jit_explicit);
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
    {"escape", (PyCFunction)(void(*)(void))module_escape, METH_FASTCALL | METH_KEYWORDS, PyDoc_STR("escape($module, pattern)\n--\n\nEscape special characters using re.escape semantics.")},
    {NULL, NULL, 0, NULL},
};

static int module_exec(PyObject *module);

static PyModuleDef_Slot module_slots[] = {
    {Py_mod_exec, module_exec},
#if PY_VERSION_HEX >= 0x030C0000
    {Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
#endif
#if PY_VERSION_HEX >= 0x030D0000
    {Py_mod_gil, Py_MOD_GIL_NOT_USED},
#endif
    {0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pcre_ext_c",
    .m_doc = "Low-level bindings to the PCRE2 regular expression engine.",
    .m_size = 0,
    .m_methods = module_methods,
    .m_slots = module_slots,
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
        jit_guard_acquire();
        int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
        jit_guard_release();
        if (jit_rc >= 0) {
            pcre2_match_data *match_data = pcre2_match_data_create(2, NULL);
            if (match_data != NULL) {
                jit_guard_acquire();
                int rc = pcre2_jit_match(code,
                                         (PCRE2_SPTR)"X2025-10-08",
                                         11,
                                         0,
                                         PCRE2_ANCHORED,
                                         match_data,
                                         NULL);
                jit_guard_release();
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
            jit_guard_acquire();
            int jit_rc = pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
            jit_guard_release();
            if (jit_rc >= 0) {
                pcre2_match_data *match_data = pcre2_match_data_create(2, NULL);
                if (match_data != NULL) {
                    jit_guard_acquire();
                    int rc = pcre2_jit_match(code,
                                             (PCRE2_SPTR)"ab",
                                             2,
                                             0,
                                             PCRE2_ANCHORED | PCRE2_ENDANCHORED,
                                             match_data,
                                             NULL);
                    jit_guard_release();
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

static int
module_exec(PyObject *module)
{
    PyInterpreterState *current_interpreter = PyInterpreterState_Get();
    PyInterpreterState *expected_interpreter = NULL;
    if (!atomic_compare_exchange_strong_explicit(
            &primary_interpreter,
            &expected_interpreter,
            current_interpreter,
            memory_order_acq_rel,
            memory_order_acquire) &&
        expected_interpreter != current_interpreter) {
        PyErr_SetString(
            PyExc_ImportError,
            "pcre_ext_c does not support loading in multiple interpreters"
        );
        return -1;
    }

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
        goto error_pattern_cache;
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

    if (pcre_memory_initialize() < 0) {
        goto error_memory;
    }

    if (pcre_error_init(module) < 0) {
        goto error_errors;
    }

    if (cache_initialize(pattern_cache_global) < 0) {
        goto error_cache;
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

    return 0;

error_cache:
    cache_teardown();
error_errors:
    pcre_error_teardown();
error_memory:
    pcre_memory_teardown();
error_pattern_cache:
    pattern_cache_teardown();
error_jit_support:
    jit_support_teardown();
    return -1;
}

PyMODINIT_FUNC
PyInit_pcre_ext_c(void)
{
    return PyModuleDef_Init(&moduledef);
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
    if (pcre2_config(PCRE2_CONFIG_VERSION, buffer) > 0 && buffer[0] != '\0') {
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
