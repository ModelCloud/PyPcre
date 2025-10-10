// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#ifndef PCRE_EXT_PCRE2_MODULE_H
#define PCRE_EXT_PCRE2_MODULE_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>
#include "pythread.h"
#include <stddef.h>
#include <stdint.h>

#if !defined(PCRE2_CODE_UNIT_WIDTH)
#define PCRE2_CODE_UNIT_WIDTH 8
#endif
#if defined(__has_include)
// Prefer the system-provided header when available for maximum accuracy.
#   if __has_include(<pcre2.h>)
#       include <pcre2.h>
#   elif __has_include("pcre2.h")
#       include "pcre2.h"
#   else
#       error "Missing required pcre2.h header"
#   endif
#else
#   include "pcre2.h"
#endif

#include "atomic_compat.h"

#if ATOMIC_COMPAT_HAVE_ATOMICS
#   define PCRE_EXT_HAVE_ATOMICS 1
#endif

typedef struct {
    PyObject_HEAD
    pcre2_code *code;
    PyObject *pattern;
    PyObject *pattern_bytes;
    PyObject *groupindex;
    uint32_t compile_options;
    uint32_t capture_count;
    int pattern_is_bytes;
#if defined(PCRE_EXT_HAVE_ATOMICS)
    ATOMIC_VAR(int) jit_enabled;
    ATOMIC_VAR(pcre2_match_data *) cached_match_data;
    ATOMIC_VAR(pcre2_match_context *) cached_match_context;
#else
    PyThread_type_lock jit_lock;
    int jit_enabled;
    pcre2_match_data *cached_match_data;
    pcre2_match_context *cached_match_context;
#endif
    int has_first_literal;
    uint32_t first_literal;
    int first_literal_caseless;
} PatternObject;

typedef struct {
    PyObject_HEAD
    PatternObject *pattern;
    PyObject *subject;
    PyObject *utf8_owner;
    const char *utf8_data;
    Py_ssize_t utf8_length;
    Py_ssize_t *ovector;
    uint32_t ovec_count;
    int subject_is_bytes;
} MatchObject;

extern PyTypeObject PatternType;
extern PyTypeObject MatchType;

/* Error handling */
extern PyObject *PcreError;
int pcre_error_init(PyObject *module);
void pcre_error_teardown(void);
void raise_pcre_error(const char *context, int error_code, PCRE2_SIZE error_offset);

/* Flags */
int pcre_flag_add_constants(PyObject *module);

/* Cache helpers */
int cache_initialize(void);
void cache_teardown(void);
pcre2_match_data *match_data_cache_acquire(PatternObject *self);
void match_data_cache_release(pcre2_match_data *match_data);
pcre2_match_context *match_context_cache_acquire(int use_offset_limit);
void match_context_cache_release(pcre2_match_context *context, int had_offset_limit);
void cache_set_context_cache_enabled(int enabled);
pcre2_jit_stack *jit_stack_cache_acquire(void);
void jit_stack_cache_release(pcre2_jit_stack *jit_stack);
PyObject *module_get_match_data_cache_size(PyObject *module, PyObject *args);
PyObject *module_set_match_data_cache_size(PyObject *module, PyObject *args);
PyObject *module_clear_match_data_cache(PyObject *module, PyObject *args);
PyObject *module_get_match_data_cache_count(PyObject *module, PyObject *args);
PyObject *module_get_jit_stack_cache_size(PyObject *module, PyObject *args);
PyObject *module_set_jit_stack_cache_size(PyObject *module, PyObject *args);
PyObject *module_clear_jit_stack_cache(PyObject *module, PyObject *args);
PyObject *module_get_jit_stack_cache_count(PyObject *module, PyObject *args);
PyObject *module_get_jit_stack_limits(PyObject *module, PyObject *args);
PyObject *module_set_jit_stack_limits(PyObject *module, PyObject *args);
PyObject *module_get_cache_strategy(PyObject *module, PyObject *args);
PyObject *module_set_cache_strategy(PyObject *module, PyObject *args);
PyObject *module_debug_thread_cache_count(PyObject *module, PyObject *args);

/* Utilities */
PyObject *bytes_from_text(PyObject *obj);
Py_ssize_t utf8_offset_to_index(const char *data, Py_ssize_t length);
int utf8_index_to_offset(PyObject *unicode_obj, Py_ssize_t index, Py_ssize_t *offset_out);
PyObject *create_groupindex_dict(pcre2_code *code);
int coerce_jit_argument(PyObject *value, int default_value, int *out, int *is_explicit);

/* Memory management */
int pcre_memory_initialize(void);
void pcre_memory_teardown(void);
void *pcre_malloc(size_t size);
void pcre_free(void *ptr);
const char *pcre_memory_allocator_name(void);

#endif
