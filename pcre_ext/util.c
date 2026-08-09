// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

int
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

#if defined(_MSC_VER)
static inline unsigned int
popcountll(uint64_t value)
{
    value -= (value >> 1) & 0x5555555555555555ULL;
    value = (value & 0x3333333333333333ULL) + ((value >> 2) & 0x3333333333333333ULL);
    value = (value + (value >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (unsigned int)((value * 0x0101010101010101ULL) >> 56);
}
#else
static inline unsigned int
popcountll(uint64_t value)
{
    return (unsigned int)__builtin_popcountll(value);
}
#endif

PyObject *
buffer_bytes_from_object(PyObject *obj, const char **data_out, Py_ssize_t *length_out)
{
    /*
     * Wrap any buffer-protocol object (e.g. mmap.mmap, bytearray) in a
     * memoryview long enough to validate its shape, then snapshot the bytes.
     * Holding a raw buffer pointer across a PCRE2 call is unsafe on free-
     * threaded Python because bytearray, mmap, and custom exporters can mutate
     * concurrently. The immutable snapshot also prevents validated UTF-8 from
     * becoming malformed after validation but before/during matching.
     *
     * PyMemoryView_FromObject is required here rather than a manual
     * PyObject_GetBuffer + PyMemoryView_FromBuffer: the latter leaves the
     * exporter's buffer export count permanently elevated (observed as
     * mmap.close() raising BufferError even after the memoryview and all
     * its referents are collected), whereas PyMemoryView_FromObject
     * correctly releases the buffer when the memoryview is deallocated.
     */
    PyObject *view = PyMemoryView_FromObject(obj);
    if (view == NULL) {
        return NULL;
    }

    Py_buffer *buffer = PyMemoryView_GET_BUFFER(view);
    if (buffer->itemsize != 1 || !PyBuffer_IsContiguous(buffer, 'C')) {
        Py_DECREF(view);
        PyErr_SetString(PyExc_TypeError,
                        "subject buffer must be a contiguous sequence of single-byte items");
        return NULL;
    }

    PyObject *base = PyMemoryView_GET_BASE(view);
    if (base == NULL) {
        base = obj;
    }

    PyObject *snapshot = NULL;
    Py_BEGIN_CRITICAL_SECTION(base);
    snapshot = PyBytes_FromStringAndSize((const char *)buffer->buf, buffer->len);
    Py_END_CRITICAL_SECTION();
    Py_DECREF(view);
    if (snapshot == NULL) {
        return NULL;
    }

    *data_out = PyBytes_AS_STRING(snapshot);
    *length_out = PyBytes_GET_SIZE(snapshot);
    return snapshot;
}

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
    /*
     * Convert a UTF-8 byte offset to a code-point index without allocating a
     * temporary Python unicode object.  This is used heavily by Match.span()/start()
     * /end() and is therefore kept allocation-free.
     *
     * The number of code points in the first N bytes equals the number of
     * non-continuation bytes (i.e. UTF-8 lead bytes and ASCII bytes) in those N
     * bytes.  We count those in 8-byte chunks with a bit-trick and popcount,
     * then handle the tail one byte at a time.
     */
    if (length <= 0) {
        return 0;
    }

    const unsigned char *u = (const unsigned char *)data;
    Py_ssize_t index = 0;
    Py_ssize_t offset = 0;
    const Py_ssize_t chunk = (Py_ssize_t)sizeof(uint64_t);
    const uint64_t high_mask = 0x8080808080808080ULL;
    const uint64_t bit6_mask = 0x4040404040404040ULL;

    while (offset + chunk <= length) {
        uint64_t w;
        memcpy(&w, u + offset, sizeof(uint64_t));
        /* A byte is a continuation byte iff its top bit is 1 and its second-top
         * bit is 0 (0b10xxxxxx).  In the common all-ASCII case we can skip the
         * popcount entirely; otherwise count continuation bytes and subtract. */
        if ((w & high_mask) == 0) {
            index += chunk;
        } else {
            uint64_t bit6_shifted = (w & bit6_mask) << 1;
            uint64_t cont = (w & high_mask) & ~bit6_shifted;
            index += chunk - (Py_ssize_t)popcountll(cont);
        }
        offset += chunk;
    }

    while (offset < length) {
        if ((u[offset] & 0xC0) != 0x80) {
            index += 1;
        }
        offset += 1;
    }

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

    if (kind == PyUnicode_1BYTE_KIND) {
        if (PyUnicode_IS_ASCII(unicode_obj)) {
            *offset_out = index;
            return 0;
        }

        const Py_UCS1 *start = (const Py_UCS1 *)data;
        const Py_ssize_t chunk = (Py_ssize_t)sizeof(uint64_t);
        const uint64_t high_bit_mask = 0x8080808080808080ULL;

        Py_ssize_t non_ascii = 0;
        Py_ssize_t fast_chunks = index / chunk;
        const Py_UCS1 *ptr = start;

        for (Py_ssize_t i = 0; i < fast_chunks; ++i) {
            uint64_t block;
            memcpy(&block, ptr, sizeof(uint64_t));
            non_ascii += popcountll(block & high_bit_mask);
            ptr += chunk;
        }

        Py_ssize_t remainder = index - fast_chunks * chunk;
        for (Py_ssize_t i = 0; i < remainder; ++i) {
            non_ascii += (ptr[i] & 0x80) >> 7;
        }

        *offset_out = index + non_ascii;
        return 0;
    }

    /*
     * For 2-byte and 4-byte Unicode kinds, read the UTF-8 cache and scan it
     * in 8-byte chunks counting starter bytes.  This is the inverse of
     * utf8_offset_to_index and is much faster than per-code-point iteration for
     * large indexes.
     */
    Py_ssize_t utf8_length = 0;
    const char *utf8_data = PyUnicode_AsUTF8AndSize(unicode_obj, &utf8_length);
    if (utf8_data == NULL) {
        return -1;
    }

    if (index <= 0) {
        *offset_out = 0;
        return 0;
    }
    if (index >= length) {
        *offset_out = utf8_length;
        return 0;
    }

    const unsigned char *u = (const unsigned char *)utf8_data;
    Py_ssize_t remaining = index;
    Py_ssize_t offset = 0;
    const Py_ssize_t chunk = (Py_ssize_t)sizeof(uint64_t);
    const uint64_t high_mask = 0x8080808080808080ULL;
    const uint64_t bit6_mask = 0x4040404040404040ULL;

    while (offset + chunk <= utf8_length) {
        uint64_t w;
        memcpy(&w, u + offset, sizeof(uint64_t));
        if ((w & high_mask) == 0) {
            if (remaining >= chunk) {
                remaining -= chunk;
                offset += chunk;
                continue;
            }
            offset += remaining;
            remaining = 0;
            break;
        }

        uint64_t bit6_shifted = (w & bit6_mask) << 1;
        uint64_t cont = (w & high_mask) & ~bit6_shifted;
        Py_ssize_t starters = chunk - (Py_ssize_t)popcountll(cont);
        if (remaining >= starters) {
            remaining -= starters;
            offset += chunk;
            continue;
        }

        for (Py_ssize_t i = 0; i < chunk; ++i) {
            if ((u[offset + i] & 0xC0) != 0x80) {
                if (remaining == 0) {
                    offset += i;
                    remaining = 0;
                    break;
                }
                remaining -= 1;
            }
        }
        break;
    }

    if (remaining > 0) {
        while (offset < utf8_length) {
            if ((u[offset] & 0xC0) != 0x80) {
                if (remaining == 0) {
                    break;
                }
                remaining -= 1;
            }
            offset += 1;
        }
    }

    *offset_out = offset;
    return 0;
}

typedef struct {
    const unsigned char *entry;
    uint16_t number;
} named_entry;

static int
compare_named_entries(const void *left, const void *right)
{
    const named_entry *a = (const named_entry *)left;
    const named_entry *b = (const named_entry *)right;
    return (a->number > b->number) - (a->number < b->number);
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

    if ((size_t)namecount > SIZE_MAX / sizeof(named_entry)) {
        Py_DECREF(mapping);
        PyErr_NoMemory();
        return NULL;
    }
    named_entry *entries = PyMem_Malloc((size_t)namecount * sizeof(*entries));
    if (entries == NULL) {
        Py_DECREF(mapping);
        PyErr_NoMemory();
        return NULL;
    }
    for (uint32_t i = 0; i < namecount; ++i) {
        const unsigned char *entry = (const unsigned char *)(
            table + (size_t)i * entry_size
        );
        entries[i].entry = entry;
        entries[i].number = entry_size >= 2
            ? (uint16_t)((entry[0] << 8) | entry[1])
            : 0;
    }
    qsort(entries, (size_t)namecount, sizeof(*entries), compare_named_entries);

    size_t name_max = (entry_size > 2) ? (size_t)(entry_size - 2) : 0;
    for (uint32_t i = 0; i < namecount; ++i) {
        const unsigned char *entry = entries[i].entry;
        if (entry_size < 2) {
            continue;
        }
        uint16_t number = entries[i].number;
        const char *name = (const char *)(entry + 2);

        size_t name_len = strnlen(name, name_max);
        PyObject *key = PyUnicode_FromStringAndSize(name, (Py_ssize_t)name_len);
        PyObject *value = PyLong_FromUnsignedLong((unsigned long)number);
        if (key == NULL || value == NULL) {
            Py_XDECREF(key);
            Py_XDECREF(value);
            PyMem_Free(entries);
            Py_DECREF(mapping);
            return NULL;
        }
        int contains = PyDict_Contains(mapping, key);
        if (contains < 0 ||
            (!contains && PyDict_SetItem(mapping, key, value) < 0)) {
            Py_DECREF(key);
            Py_DECREF(value);
            PyMem_Free(entries);
            Py_DECREF(mapping);
            return NULL;
        }
        Py_DECREF(key);
        Py_DECREF(value);
    }

    PyMem_Free(entries);
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
