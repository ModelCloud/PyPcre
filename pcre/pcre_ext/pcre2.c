// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"
#include <stdio.h>
#include <string.h>

static int default_jit_enabled = 1;


/* Match type */
static void
Match_dealloc(MatchObject *self)
{
    Py_XDECREF(self->pattern);
    Py_XDECREF(self->subject);
    Py_XDECREF(self->subject_bytes);
    PyMem_Free(self->ovector);
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

    const char *data = PyBytes_AS_STRING(self->subject_bytes);
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
    const char *data = PyBytes_AS_STRING(self->subject_bytes);
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

/* Pattern helpers */
static MatchObject *
create_match_object(PatternObject *pattern,
                    PyObject *subject_obj,
                    PyObject *subject_bytes,
                    uint32_t ovec_count,
                    PCRE2_SIZE *ovector)
{
    MatchObject *match = PyObject_New(MatchObject, &MatchType);
    if (match == NULL) {
        return NULL;
    }

    match->ovector = PyMem_Malloc(sizeof(Py_ssize_t) * ovec_count * 2);
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

    match->subject_bytes = subject_bytes;
    match->subject_is_bytes = PyBytes_Check(subject_obj);

    return match;
}


typedef enum {
    EXEC_MODE_MATCH,
    EXEC_MODE_SEARCH,
    EXEC_MODE_FULLMATCH
} execute_mode;

static void
Pattern_dealloc(PatternObject *self)
{
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
    if (self->jit_enabled) {
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
Pattern_execute(PatternObject *self, PyObject *subject_obj, Py_ssize_t pos,
                Py_ssize_t endpos, uint32_t options, execute_mode mode)
{
    PyObject *subject_bytes = bytes_from_text(subject_obj);
    if (subject_bytes == NULL) {
        return NULL;
    }

    int subject_is_bytes = PyBytes_Check(subject_obj);
    Py_ssize_t subject_length_bytes = PyBytes_GET_SIZE(subject_bytes);
    Py_ssize_t logical_length = subject_is_bytes
                                    ? subject_length_bytes
                                    : PyUnicode_GET_LENGTH(subject_obj);

    if (pos < 0) {
        pos += logical_length;
        if (pos < 0) {
            pos = 0;
        }
    }
    if (pos > logical_length) {
        Py_DECREF(subject_bytes);
        Py_RETURN_NONE;
    }

    Py_ssize_t adjusted_endpos = endpos;
    if (adjusted_endpos >= 0) {
        if (adjusted_endpos > logical_length) {
            adjusted_endpos = logical_length;
        }
        if (adjusted_endpos < pos) {
            Py_DECREF(subject_bytes);
            PyErr_SetString(PyExc_ValueError, "endpos must be >= pos");
            return NULL;
        }
    }

    Py_ssize_t byte_start = pos;
    Py_ssize_t byte_end = subject_length_bytes;

    if (subject_is_bytes) {
        byte_start = pos;
        if (adjusted_endpos >= 0) {
            byte_end = adjusted_endpos;
        }
    } else {
        if (utf8_index_to_offset(subject_obj, pos, &byte_start) < 0) {
            Py_DECREF(subject_bytes);
            return NULL;
        }
        if (adjusted_endpos >= 0) {
            if (utf8_index_to_offset(subject_obj, adjusted_endpos, &byte_end) < 0) {
                Py_DECREF(subject_bytes);
                return NULL;
            }
        }
    }

    if (byte_start > byte_end) {
        Py_DECREF(subject_bytes);
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

    pcre2_match_data *match_data = match_data_cache_acquire(self);
    if (match_data == NULL) {
        Py_DECREF(subject_bytes);
        PyErr_NoMemory();
        return NULL;
    }

    pcre2_match_context *match_context = NULL;
    pcre2_jit_stack *jit_stack = NULL;
    int using_jit_stack = 0;
    if (offset_limit != (PCRE2_SIZE)subject_length_bytes) {
        match_context = pcre2_match_context_create(NULL);
        if (match_context == NULL) {
            match_data_cache_release(match_data);
            Py_DECREF(subject_bytes);
            PyErr_NoMemory();
            return NULL;
        }
        int ctx_rc = pcre2_set_offset_limit(match_context, offset_limit);
        if (ctx_rc < 0) {
            pcre2_match_context_free(match_context);
            match_data_cache_release(match_data);
            Py_DECREF(subject_bytes);
            raise_pcre_error("set_offset_limit", ctx_rc, 0);
            return NULL;
        }
    }

    const char *buffer = PyBytes_AS_STRING(subject_bytes);
    int rc = 0;

    if (self->jit_enabled) {
        if (match_context == NULL) {
            match_context = pcre2_match_context_create(NULL);
            if (match_context == NULL) {
                match_data_cache_release(match_data);
                Py_DECREF(subject_bytes);
                PyErr_NoMemory();
                return NULL;
            }
        }

        jit_stack = jit_stack_cache_acquire();
        if (jit_stack == NULL) {
            pcre2_match_context_free(match_context);
            match_data_cache_release(match_data);
            Py_DECREF(subject_bytes);
            PyErr_NoMemory();
            return NULL;
        }

        pcre2_jit_stack_assign(match_context, NULL, jit_stack);
        using_jit_stack = 1;

        Py_BEGIN_ALLOW_THREADS
        rc = pcre2_jit_match(self->code,
                              (PCRE2_SPTR)buffer,
                              (PCRE2_SIZE)subject_length_bytes,
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

        if (rc == PCRE2_ERROR_JIT_BADOPTION) {
            self->jit_enabled = 0;
        } else if (rc != PCRE2_ERROR_NOMATCH && rc < 0) {
            PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
            match_data_cache_release(match_data);
            if (match_context != NULL) {
                pcre2_match_context_free(match_context);
            }
            Py_DECREF(subject_bytes);
            raise_pcre_error("jit_match", rc, error_offset);
            return NULL;
        }
    }

    if (!self->jit_enabled) {
        Py_BEGIN_ALLOW_THREADS
        rc = pcre2_match(self->code,
                         (PCRE2_SPTR)buffer,
                         (PCRE2_SIZE)subject_length_bytes,
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
        Py_DECREF(subject_bytes);
        Py_RETURN_NONE;
    }

    if (rc < 0) {
        PCRE2_SIZE error_offset = pcre2_get_startchar(match_data);
        match_data_cache_release(match_data);
        if (match_context != NULL) {
            pcre2_match_context_free(match_context);
        }
        Py_DECREF(subject_bytes);
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
        Py_DECREF(subject_bytes);
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
        subject_bytes,
        (uint32_t)expected_pairs,
        ovector);
    match_data_cache_release(match_data);
    if (match_context != NULL) {
        pcre2_match_context_free(match_context);
    }
    if (match == NULL) {
        Py_DECREF(subject_bytes);
        return NULL;
    }

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

    pattern->code = code;
    Py_INCREF(pattern_obj);
    pattern->pattern = pattern_obj;
    pattern->pattern_bytes = pattern_bytes;
    pattern->pattern_is_bytes = is_bytes;
    pattern->compile_options = compile_options;
    pattern->jit_enabled = 0;

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
            pattern->jit_enabled = 1;
        } else if (jit_rc == PCRE2_ERROR_JIT_BADOPTION) {
            pattern->jit_enabled = 0;
#ifdef PCRE2_ERROR_JIT_UNSUPPORTED
        } else if (!jit_explicit && jit_rc == PCRE2_ERROR_JIT_UNSUPPORTED) {
            pattern->jit_enabled = 0;
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
    if (coerce_jit_argument(jit_obj, default_jit_enabled, &jit, &jit_explicit) < 0) {
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
    if (coerce_jit_argument(jit_obj, default_jit_enabled, &jit, &jit_explicit) < 0) {
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
    if (coerce_jit_argument(jit_obj, default_jit_enabled, &jit, &jit_explicit) < 0) {
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
    if (coerce_jit_argument(jit_obj, default_jit_enabled, &jit, &jit_explicit) < 0) {
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
        if (coerce_jit_argument(jit_obj, default_jit_enabled, &jit, NULL) < 0) {
            return NULL;
        }
        default_jit_enabled = jit;
    }

    if (default_jit_enabled) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}


static PyMethodDef module_methods[] = {
    {"compile", (PyCFunction)module_compile, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Compile a pattern into a PCRE2 Pattern object.")},
    {"match", (PyCFunction)module_match, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Match a pattern against the beginning of a string.")},
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
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pcre.cpcre2",
    .m_doc = "Low-level bindings to the PCRE2 regular expression engine.",
    .m_size = -1,
    .m_methods = module_methods,
};

PyMODINIT_FUNC
PyInit_cpcre2(void)
{
    if (PyType_Ready(&PatternType) < 0) {
        return NULL;
    }
    if (PyType_Ready(&MatchType) < 0) {
        return NULL;
    }

    PyObject *module = PyModule_Create(&moduledef);
    if (module == NULL) {
        return NULL;
    }

    if (pcre_error_init(module) < 0) {
        goto error;
    }

    if (cache_initialize() < 0) {
        goto error;
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

    return module;

error:
    cache_teardown();
    pcre_error_teardown();
    Py_DECREF(module);
    return NULL;
}
