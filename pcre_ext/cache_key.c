// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

#include <stdbool.h>

typedef struct {
    PyObject_HEAD
    PyObject *pattern;
    Py_ssize_t flags;
    unsigned char jit;
    Py_hash_t hash_value;
} SparseCacheKeyObject;

static PyObject *key_cache = NULL;
extern PyTypeObject SparseCacheKeyType;

static void
SparseCacheKey_dealloc(SparseCacheKeyObject *self)
{
    Py_CLEAR(self->pattern);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static int
compute_key_hash(PyObject *pattern, Py_ssize_t flags, unsigned char jit, Py_hash_t *hash_out)
{
    Py_hash_t base_hash;
    int error = 0;

    if (PyUnicode_Check(pattern) || PyBytes_Check(pattern) || PyByteArray_Check(pattern) || PyMemoryView_Check(pattern)) {
        base_hash = pcre_sparse_hash_object(pattern, &error);
        if (error < 0) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_TypeError, "sparse_half_hash expects str or bytes-like input");
            }
            return -1;
        }
    } else {
        base_hash = PyObject_Hash(pattern);
        if (base_hash == -1 && PyErr_Occurred()) {
            return -1;
        }
    }

    Py_hash_t combined = base_hash ^ ((Py_hash_t)flags << 1) ^ (Py_hash_t)jit;
    if (combined == -1) {
        combined = -2;
    }

    *hash_out = combined;
    return 0;
}

static PyObject *
SparseCacheKey_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    (void)kwargs;

    PyObject *pattern = NULL;
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = NULL;

    if (!PyArg_ParseTuple(args, "OOO:SparseCacheKey", &pattern, &flags_obj, &jit_obj)) {
        return NULL;
    }

    Py_ssize_t flags = PyLong_AsSsize_t(flags_obj);
    if (flags == -1 && PyErr_Occurred()) {
        return NULL;
    }

    int jit_int = PyObject_IsTrue(jit_obj);
    if (jit_int < 0) {
        return NULL;
    }

    SparseCacheKeyObject *self = (SparseCacheKeyObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    Py_INCREF(pattern);
    self->pattern = pattern;
    self->flags = flags;
    self->jit = jit_int ? 1 : 0;

    if (compute_key_hash(pattern, flags, self->jit, &self->hash_value) < 0) {
        Py_DECREF(pattern);
        Py_DECREF(self);
        return NULL;
    }

    return (PyObject *)self;
}

static Py_hash_t
SparseCacheKey_hash(SparseCacheKeyObject *self)
{
    return self->hash_value;
}

static PyObject *
SparseCacheKey_richcompare(PyObject *a_obj, PyObject *b_obj, int op)
{
    if (!PyObject_TypeCheck(a_obj, &SparseCacheKeyType) || !PyObject_TypeCheck(b_obj, &SparseCacheKeyType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (op != Py_EQ && op != Py_NE) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    SparseCacheKeyObject *a = (SparseCacheKeyObject *)a_obj;
    SparseCacheKeyObject *b = (SparseCacheKeyObject *)b_obj;

    int equal = 0;
    if (a->hash_value == b->hash_value && a->flags == b->flags && a->jit == b->jit) {
        if (a->pattern == b->pattern) {
            equal = 1;
        } else {
            equal = PyObject_RichCompareBool(a->pattern, b->pattern, Py_EQ);
            if (equal < 0) {
                return NULL;
            }
        }
    }

    if (op == Py_EQ) {
        return PyBool_FromLong(equal);
    }
    return PyBool_FromLong(!equal);
}

static PyObject *
SparseCacheKey_repr(SparseCacheKeyObject *self)
{
    return PyUnicode_FromFormat("SparseCacheKey(pattern=%R, flags=%zd, jit=%s)",
                                self->pattern,
                                self->flags,
                                self->jit ? "True" : "False");
}

static PyMemberDef SparseCacheKey_members[] = {
    {"pattern", T_OBJECT_EX, offsetof(SparseCacheKeyObject, pattern), READONLY, PyDoc_STR("Underlying pattern")},
    {"flags", T_PYSSIZET, offsetof(SparseCacheKeyObject, flags), READONLY, PyDoc_STR("Compiler flags")},
    {"jit", T_BOOL, offsetof(SparseCacheKeyObject, jit), READONLY, PyDoc_STR("JIT flag")},
    {NULL},
};

PyTypeObject SparseCacheKeyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pcre_ext_c.SparseCacheKey",
    .tp_basicsize = sizeof(SparseCacheKeyObject),
    .tp_dealloc = (destructor)SparseCacheKey_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("Dictionary key that hashes patterns with sparse sampling."),
    .tp_new = SparseCacheKey_new,
    .tp_hash = (hashfunc)SparseCacheKey_hash,
    .tp_repr = (reprfunc)SparseCacheKey_repr,
    .tp_richcompare = SparseCacheKey_richcompare,
    .tp_members = SparseCacheKey_members,
};
PyObject *
cache_key_get(PyObject *Py_UNUSED(module), PyObject *args)
{
    PyObject *pattern = NULL;
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = NULL;

    if (!PyArg_ParseTuple(args, "OOO:cache_key_get", &pattern, &flags_obj, &jit_obj)) {
        return NULL;
    }

    Py_ssize_t flags = PyLong_AsSsize_t(flags_obj);
    if (flags == -1 && PyErr_Occurred()) {
        return NULL;
    }

    int jit = PyObject_IsTrue(jit_obj);
    if (jit < 0) {
        return NULL;
    }

    PyObject *token_obj = PyLong_FromUnsignedLongLong((((unsigned long long)flags) << 1) | (unsigned long long)jit);
    if (token_obj == NULL) {
        return NULL;
    }

    PyObject *per_pattern = NULL;
    if (key_cache != NULL) {
        per_pattern = PyDict_GetItem(key_cache, pattern);
        if (per_pattern == NULL && PyErr_Occurred()) {
            PyErr_Clear();
        }
    }

    PyObject *existing = NULL;
    if (per_pattern != NULL) {
        existing = PyDict_GetItem(per_pattern, token_obj);
        if (existing != NULL) {
            Py_INCREF(existing);
            Py_DECREF(token_obj);
            return existing;
        }
    }

    PyObject *args_tuple = Py_BuildValue("OOO", pattern, flags_obj, jit_obj);
    if (args_tuple == NULL) {
        Py_DECREF(token_obj);
        return NULL;
    }
    existing = SparseCacheKey_new(&SparseCacheKeyType, args_tuple, NULL);
    Py_DECREF(args_tuple);
    if (existing == NULL) {
        Py_DECREF(token_obj);
        return NULL;
    }

    if (key_cache == NULL) {
        key_cache = PyDict_New();
        if (key_cache == NULL) {
            Py_DECREF(existing);
            Py_DECREF(token_obj);
            return NULL;
        }
    }

    if (per_pattern == NULL) {
        per_pattern = PyDict_New();
        if (per_pattern == NULL) {
            Py_DECREF(existing);
            Py_DECREF(token_obj);
            return NULL;
        }
        if (PyDict_SetItem(key_cache, pattern, per_pattern) < 0) {
            Py_DECREF(per_pattern);
            Py_DECREF(existing);
            Py_DECREF(token_obj);
            return NULL;
        }
        Py_DECREF(per_pattern);
        per_pattern = PyDict_GetItem(key_cache, pattern);
    }
    if (PyDict_SetItem(per_pattern, token_obj, existing) < 0) {
        Py_DECREF(existing);
        Py_DECREF(token_obj);
        return NULL;
    }

    Py_DECREF(token_obj);
    return existing;
}

PyObject *
cache_key_discard(PyObject *Py_UNUSED(module), PyObject *args)
{
    PyObject *pattern = NULL;
    PyObject *flags_obj = NULL;
    PyObject *jit_obj = NULL;
    if (!PyArg_ParseTuple(args, "OOO:cache_key_discard", &pattern, &flags_obj, &jit_obj)) {
        return NULL;
    }

    if (key_cache == NULL) {
        Py_RETURN_NONE;
    }

    PyObject *per_pattern = PyDict_GetItem(key_cache, pattern);
    if (per_pattern == NULL && PyErr_Occurred()) {
        PyErr_Clear();
    }
    if (per_pattern == NULL) {
        Py_RETURN_NONE;
    }

    Py_ssize_t flags = PyLong_AsSsize_t(flags_obj);
    if (flags == -1 && PyErr_Occurred()) {
        return NULL;
    }
    int jit = PyObject_IsTrue(jit_obj);
    if (jit < 0) {
        return NULL;
    }

    PyObject *token_obj = PyLong_FromUnsignedLongLong((((unsigned long long)flags) << 1) | (unsigned long long)jit);
    if (token_obj == NULL) {
        return NULL;
    }

    PyDict_DelItem(per_pattern, token_obj);
    Py_DECREF(token_obj);

    if (PyDict_Size(per_pattern) == 0) {
        PyDict_DelItem(key_cache, pattern);
    }

    Py_RETURN_NONE;
}

PyObject *
cache_key_clear(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(ignored))
{
    if (key_cache != NULL) {
        PyDict_Clear(key_cache);
    }
    Py_RETURN_NONE;
}
