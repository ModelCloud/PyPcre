// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

#define FNV64_OFFSET 0xcbf29ce484222325ULL
#define FNV64_PRIME  0x100000001b3ULL

static inline Py_ssize_t
compute_sparse_stride(Py_ssize_t length)
{
    Py_ssize_t stride = 2;
    while (length / stride > 8) {
        if (stride > (PY_SSIZE_T_MAX >> 1)) {
            return stride;
        }
        stride <<= 1;
    }

    return stride;
}

static inline uint64_t
fnv_mix_sparse_bytes(const unsigned char *data, Py_ssize_t length, Py_ssize_t stride)
{
    uint64_t hash = FNV64_OFFSET;
    Py_ssize_t start = stride - 1;
    if (start >= length) {
        return hash;
    }

    for (Py_ssize_t index = start; index < length; index += stride) {
        hash ^= (uint64_t)data[index];
        hash *= FNV64_PRIME;
    }
    return hash;
}

static inline uint64_t
fnv_mix_sparse_unicode(PyObject *unicode_obj, Py_ssize_t length, Py_ssize_t stride)
{
    if (PyUnicode_READY(unicode_obj) == -1) {
        return FNV64_OFFSET;
    }

    uint64_t hash = FNV64_OFFSET;
    Py_UCS4 value;
    Py_ssize_t index;
    void *data = PyUnicode_DATA(unicode_obj);
    int kind = PyUnicode_KIND(unicode_obj);

    Py_ssize_t start = stride - 1;
    if (start >= length) {
        return hash;
    }

    for (index = start; index < length; index += stride) {
        value = PyUnicode_READ(kind, data, index);
        hash ^= (uint64_t)value;
        hash *= FNV64_PRIME;
    }

    return hash;
}

Py_hash_t
pcre_sparse_hash_object(PyObject *arg, int *error)
{
    Py_ssize_t length = 0;
    uint64_t hash = FNV64_OFFSET;
    Py_ssize_t stride = 2;
    Py_buffer view;
    int has_view = 0;

    if (error != NULL) {
        *error = 0;
    }

    if (PyUnicode_Check(arg)) {
        length = PyUnicode_GetLength(arg);
        if (length > 1) {
            stride = compute_sparse_stride(length);
            hash = fnv_mix_sparse_unicode(arg, length, stride);
        }
    } else {
        if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) != 0) {
            if (error != NULL) {
                *error = -1;
            }
            return (Py_hash_t)0;
        }
        has_view = 1;
        length = view.len;
        if (length > 1 && view.buf != NULL) {
            stride = compute_sparse_stride(length);
            hash = fnv_mix_sparse_bytes((const unsigned char *)view.buf, length, stride);
        }
    }

    if (has_view) {
        PyBuffer_Release(&view);
    }

    hash ^= (uint64_t)((Py_ssize_t)length >> 5);

    Py_hash_t result = (Py_hash_t)hash;
    if (result == -1) {
        result = -2;
    }

    return result;
}

PyObject *
module_sparse_half_hash(PyObject *module, PyObject *arg)
{
    (void)module;
    int error = 0;
    Py_hash_t result = pcre_sparse_hash_object(arg, &error);
    if (error < 0) {
        PyErr_SetString(PyExc_TypeError, "sparse_half_hash expects str or bytes-like input");
        return NULL;
    }

    return PyLong_FromSsize_t(result);
}
