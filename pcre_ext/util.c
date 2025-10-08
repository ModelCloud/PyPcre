// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

PyObject *
bytes_from_text(PyObject *obj)
{
    if (PyBytes_Check(obj)) {
        Py_INCREF(obj);
        return obj;
    }
    if (PyUnicode_Check(obj)) {
        return PyUnicode_AsUTF8String(obj);
    }
    PyErr_SetString(PyExc_TypeError, "expected str or bytes");
    return NULL;
}

Py_ssize_t
utf8_offset_to_index(const char *data, Py_ssize_t length)
{
    PyObject *tmp = PyUnicode_DecodeUTF8(data, length, "strict");
    if (tmp == NULL) {
        return -1;
    }
    Py_ssize_t index = PyUnicode_GET_LENGTH(tmp);
    Py_DECREF(tmp);
    return index;
}

int
utf8_index_to_offset(PyObject *unicode_obj, Py_ssize_t index, Py_ssize_t *offset_out)
{
    if (!PyUnicode_Check(unicode_obj)) {
        *offset_out = index;
        return 0;
    }

    if (PyUnicode_READY(unicode_obj) < 0) {
        return -1;
    }

    Py_ssize_t length = PyUnicode_GET_LENGTH(unicode_obj);
    if (index < 0) {
        index += length;
        if (index < 0) {
            index = 0;
        }
    }
    if (index > length) {
        index = length;
    }

    int kind = PyUnicode_KIND(unicode_obj);
    void *data = PyUnicode_DATA(unicode_obj);
    Py_ssize_t offset = 0;

    for (Py_ssize_t i = 0; i < index; ++i) {
        Py_UCS4 ch = PyUnicode_READ(kind, data, i);
        if (ch <= 0x7F) {
            offset += 1;
        } else if (ch <= 0x7FF) {
            offset += 2;
        } else if (ch <= 0xFFFF) {
            offset += 3;
        } else {
            offset += 4;
        }
    }

    *offset_out = offset;
    return 0;
}

PyObject *
create_groupindex_dict(pcre2_code *code)
{
    uint32_t namecount = 0;
    if (pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &namecount) != 0 || namecount == 0) {
        return PyDict_New();
    }

    uint32_t entry_size = 0;
    if (pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &entry_size) != 0) {
        return PyDict_New();
    }

    PCRE2_SPTR table = NULL;
    if (pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &table) != 0 || table == NULL) {
        return PyDict_New();
    }

    PyObject *mapping = PyDict_New();
    if (mapping == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < namecount; ++i) {
        const unsigned char *entry = (const unsigned char *)(table + i * entry_size);
        uint16_t number = (uint16_t)((entry[0] << 8) | entry[1]);
        const char *name = (const char *)(entry + 2);

        PyObject *key = PyUnicode_FromString(name);
        PyObject *value = PyLong_FromUnsignedLong((unsigned long)number);
        if (key == NULL || value == NULL) {
            Py_XDECREF(key);
            Py_XDECREF(value);
            Py_DECREF(mapping);
            return NULL;
        }
        if (PyDict_SetItem(mapping, key, value) < 0) {
            Py_DECREF(key);
            Py_DECREF(value);
            Py_DECREF(mapping);
            return NULL;
        }
        Py_DECREF(key);
        Py_DECREF(value);
    }

    return mapping;
}

int
coerce_jit_argument(PyObject *value, int default_value, int *out, int *is_explicit)
{
    if (is_explicit != NULL) {
        *is_explicit = (value != NULL && value != Py_None);
    }
    if (value == NULL || value == Py_None) {
        *out = default_value;
        return 0;
    }

    int truth = PyObject_IsTrue(value);
    if (truth < 0) {
        return -1;
    }

    *out = truth ? 1 : 0;
    return 0;
}
