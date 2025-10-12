// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>
#include <string.h>
#include "pythread.h"

#define DEFAULT_THREAD_CACHE_LIMIT 32
#define DEFAULT_GLOBAL_CACHE_LIMIT 128

typedef enum {
    CACHE_STRATEGY_THREAD_LOCAL = 0,
    CACHE_STRATEGY_GLOBAL = 1,
} CacheStrategy;

typedef struct ThreadCacheStateObject ThreadCacheStateObject;

typedef struct {
    ThreadCacheStateObject *owner;
    PyObject *pattern_cache;
    Py_ssize_t cache_limit;  // -1 for None/unbounded, >=0 for capacity
} ThreadCacheEntry;

struct ThreadCacheStateObject {
    PyObject_HEAD
    Py_tss_t tss_key;
    int tss_initialized;
    PyObject *cleanup_key;
};

typedef struct {
    PyObject_HEAD
    PyObject *pattern_cache;
    PyThread_type_lock lock;
    Py_ssize_t cache_limit;
    PyObject *order;
    Py_ssize_t order_head;
} GlobalCacheStateObject;

static ThreadCacheStateObject *THREAD_LOCAL_OBJ = NULL;
static GlobalCacheStateObject *GLOBAL_STATE_OBJ = NULL;
static CacheStrategy CURRENT_STRATEGY = CACHE_STRATEGY_THREAD_LOCAL;
static int CACHE_STRATEGY_LOCKED = 0;
static int DEFAULT_STRATEGY_INITIALISED = 0;

static const char THREAD_CACHE_CAPSULE_NAME[] = "pcre.cache.thread_state";
static const char THREAD_CACHE_CLEANUP_KEY[] = "_pcre_cache_state";

// Utility helpers ---------------------------------------------------------

static inline int
is_truthy_env_flag(const char *value)
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

static inline int
ensure_mapping(PyObject *mapping)
{
    if (mapping == NULL) {
        return 0;
    }
    if (!PyMapping_Check(mapping)) {
        PyErr_SetString(PyExc_TypeError, "pattern_cache must be a mapping");
        return -1;
    }
    return 0;
}

static int
mapping_clear(PyObject *mapping)
{
    if (mapping == NULL) {
        return 0;
    }
    PyObject *result = PyObject_CallMethod(mapping, "clear", NULL);
    if (result == NULL) {
        return -1;
    }
    Py_DECREF(result);
    return 0;
}

static int
remove_oldest_entry(PyObject *mapping)
{
    PyObject *iter = PyObject_GetIter(mapping);
    if (iter == NULL) {
        return -1;
    }
    PyObject *first_key = PyIter_Next(iter);
    Py_DECREF(iter);
    if (first_key == NULL) {
        if (PyErr_Occurred()) {
            return -1;
        }
        return 0;
    }
    PyObject *result = PyObject_CallMethod(mapping, "pop", "O", first_key);
    Py_DECREF(first_key);
    if (result == NULL) {
        return -1;
    }
    Py_DECREF(result);
    return 0;
}

static int
mapping_ensure_capacity(PyObject *mapping, Py_ssize_t limit)
{
    if (mapping == NULL || limit < 0) {
        return 0;
    }
    if (limit == 0) {
        return mapping_clear(mapping);
    }
    Py_ssize_t size = PyObject_Size(mapping);
    if (size == -1) {
        return -1;
    }
    while (size >= limit) {
        if (remove_oldest_entry(mapping) < 0) {
            return -1;
        }
        size -= 1;
    }
    return 0;
}

static PyObject *
mapping_get(PyObject *mapping, PyObject *key, int *is_unhashable)
{
    *is_unhashable = 0;
    if (mapping == NULL) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }
    PyObject *result = PyObject_GetItem(mapping, key);
    if (result != NULL) {
        return result;
    }
    if (PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyErr_Clear();
        return NULL;
    }
    if (PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_Clear();
        *is_unhashable = 1;
        return NULL;
    }
    return NULL;
}

static int
mapping_set(PyObject *mapping, PyObject *key, PyObject *value)
{
    if (mapping == NULL) {
        return 0;
    }
    if (PyObject_SetItem(mapping, key, value) < 0) {
        if (PyErr_ExceptionMatches(PyExc_TypeError)) {
            PyErr_Clear();
            return 0;
        }
        return -1;
    }
    return 0;
}

static void
global_cache_compact_order(GlobalCacheStateObject *state)
{
    if (state->order == NULL) {
        return;
    }
    Py_ssize_t size = PyList_GET_SIZE(state->order);
    if (state->order_head <= 64 || state->order_head * 2 <= size) {
        return;
    }
    if (PyList_SetSlice(state->order, 0, state->order_head, NULL) < 0) {
        PyErr_Clear();
        return;
    }
    state->order_head = 0;
}

static int
global_cache_evict_one(GlobalCacheStateObject *state)
{
    if (state->order == NULL) {
        return 0;
    }

    Py_ssize_t size = PyList_GET_SIZE(state->order);
    while (state->order_head < size) {
        Py_ssize_t index = state->order_head++;
        PyObject *candidate = PyList_GET_ITEM(state->order, index);
        if (candidate == Py_None) {
            continue;
        }

        if (state->pattern_cache != NULL) {
            if (PyDict_DelItem(state->pattern_cache, candidate) < 0) {
                if (PyErr_ExceptionMatches(PyExc_KeyError)) {
                    PyErr_Clear();
                } else {
                    return -1;
                }
            }
        }

        Py_INCREF(Py_None);
        PyList_SET_ITEM(state->order, index, Py_None);
        Py_DECREF(candidate);
        global_cache_compact_order(state);
        return 0;
    }

    global_cache_compact_order(state);
    return 0;
}

static int
global_cache_trim(GlobalCacheStateObject *state)
{
    if (state->cache_limit < 0 || state->pattern_cache == NULL) {
        return 0;
    }
    Py_ssize_t size = PyDict_Size(state->pattern_cache);
    if (size == -1) {
        return -1;
    }
    while (size > state->cache_limit) {
        if (global_cache_evict_one(state) < 0) {
            return -1;
        }
        size = PyDict_Size(state->pattern_cache);
        if (size == -1) {
            return -1;
        }
        if (state->order != NULL && state->order_head >= PyList_GET_SIZE(state->order)) {
            break;
        }
    }
    return 0;
}

static inline void
lock_cache_strategy(void)
{
    if (!CACHE_STRATEGY_LOCKED) {
        CACHE_STRATEGY_LOCKED = 1;
    }
}

#ifndef Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED
#define Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED ((void *)0)
#endif
#ifndef Py_MOD_GIL_USED
#define Py_MOD_GIL_USED ((void *)0)
#endif

// Thread-local helpers ----------------------------------------------------

static void
thread_cache_entry_free(ThreadCacheEntry *entry)
{
    if (entry == NULL) {
        return;
    }
    Py_XDECREF(entry->pattern_cache);
    PyMem_Free(entry);
}

static void
thread_cache_capsule_destructor(PyObject *capsule)
{
    ThreadCacheEntry *entry = PyCapsule_GetPointer(capsule, THREAD_CACHE_CAPSULE_NAME);
    if (entry == NULL) {
        return;
    }
    ThreadCacheStateObject *owner = entry->owner;
    if (owner != NULL && owner->tss_initialized) {
        ThreadCacheEntry *current = PyThread_tss_get(&owner->tss_key);
        if (current == entry) {
            PyThread_tss_set(&owner->tss_key, NULL);
        }
    }
    thread_cache_entry_free(entry);
}

static ThreadCacheEntry *
thread_cache_entry_get(ThreadCacheStateObject *state, int create)
{
    ThreadCacheEntry *entry = NULL;
    if (state->tss_initialized) {
        entry = PyThread_tss_get(&state->tss_key);
    }
    if (entry != NULL || !create) {
        return entry;
    }

    if (!state->tss_initialized) {
        if (PyThread_tss_create(&state->tss_key) != 0) {
            PyErr_NoMemory();
            return NULL;
        }
        state->tss_initialized = 1;
    }

    entry = PyMem_Calloc(1, sizeof(*entry));
    if (entry == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    entry->owner = state;
    entry->cache_limit = DEFAULT_THREAD_CACHE_LIMIT;
    entry->pattern_cache = PyDict_New();
    if (entry->pattern_cache == NULL) {
        thread_cache_entry_free(entry);
        return NULL;
    }

    if (PyThread_tss_set(&state->tss_key, entry) != 0) {
        thread_cache_entry_free(entry);
        PyErr_SetString(PyExc_RuntimeError, "failed to initialise thread cache state");
        return NULL;
    }

    PyObject *state_dict = PyThreadState_GetDict();
    if (state_dict != NULL) {
        PyObject *capsule = PyCapsule_New(entry, THREAD_CACHE_CAPSULE_NAME, thread_cache_capsule_destructor);
        if (capsule != NULL) {
            if (state->cleanup_key == NULL) {
                state->cleanup_key = PyUnicode_FromString(THREAD_CACHE_CLEANUP_KEY);
            }
            if (state->cleanup_key != NULL) {
                if (PyDict_SetItem(state_dict, state->cleanup_key, capsule) < 0) {
                    PyErr_Clear();
                }
            }
            Py_DECREF(capsule);
        } else {
            PyErr_Clear();
        }
    }

    return entry;
}

// Thread cache type -------------------------------------------------------

static void
ThreadCacheState_dealloc(ThreadCacheStateObject *self)
{
    if (self->tss_initialized) {
        ThreadCacheEntry *entry = PyThread_tss_get(&self->tss_key);
        if (entry != NULL) {
            PyThread_tss_set(&self->tss_key, NULL);
            thread_cache_entry_free(entry);
        }
        PyThread_tss_delete(&self->tss_key);
        self->tss_initialized = 0;
    }
    Py_XDECREF(self->cleanup_key);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
ThreadCacheState_getattro(PyObject *self_obj, PyObject *name_obj)
{
    ThreadCacheStateObject *self = (ThreadCacheStateObject *)self_obj;
    if (!PyUnicode_Check(name_obj)) {
        return PyObject_GenericGetAttr(self_obj, name_obj);
    }
    const char *name = PyUnicode_AsUTF8(name_obj);
    if (name == NULL) {
        return NULL;
    }

    if (strcmp(name, "pattern_cache") == 0) {
        ThreadCacheEntry *entry = thread_cache_entry_get(self, 1);
        if (entry == NULL) {
            return NULL;
        }
        if (entry->pattern_cache == NULL) {
            entry->pattern_cache = PyDict_New();
            if (entry->pattern_cache == NULL) {
                return NULL;
            }
        }
        Py_INCREF(entry->pattern_cache);
        return entry->pattern_cache;
    }

    if (strcmp(name, "cache_limit") == 0) {
        ThreadCacheEntry *entry = thread_cache_entry_get(self, 1);
        if (entry == NULL) {
            return NULL;
        }
        if (entry->cache_limit < 0) {
            Py_RETURN_NONE;
        }
        return PyLong_FromSsize_t(entry->cache_limit);
    }

    return PyObject_GenericGetAttr(self_obj, name_obj);
}

static int
ThreadCacheState_setattro(PyObject *self_obj, PyObject *name_obj, PyObject *value)
{
    ThreadCacheStateObject *self = (ThreadCacheStateObject *)self_obj;
    if (!PyUnicode_Check(name_obj)) {
        return PyObject_GenericSetAttr(self_obj, name_obj, value);
    }
    const char *name = PyUnicode_AsUTF8(name_obj);
    if (name == NULL) {
        return -1;
    }

    if (strcmp(name, "pattern_cache") == 0) {
        if (value == NULL) {
            PyErr_SetString(PyExc_AttributeError, "cannot delete pattern_cache");
            return -1;
        }
        if (ensure_mapping(value) < 0) {
            return -1;
        }
        ThreadCacheEntry *entry = thread_cache_entry_get(self, 1);
        if (entry == NULL) {
            return -1;
        }
        Py_INCREF(value);
        Py_XDECREF(entry->pattern_cache);
        entry->pattern_cache = value;
        return 0;
    }

    if (strcmp(name, "cache_limit") == 0) {
        if (value == NULL) {
            PyErr_SetString(PyExc_AttributeError, "cannot delete cache_limit");
            return -1;
        }
        ThreadCacheEntry *entry = thread_cache_entry_get(self, 1);
        if (entry == NULL) {
            return -1;
        }
        Py_ssize_t limit;
        if (value == Py_None) {
            limit = -1;
        } else {
            PyObject *as_long = PyNumber_Long(value);
            if (as_long == NULL) {
                return -1;
            }
            limit = PyLong_AsSsize_t(as_long);
            Py_DECREF(as_long);
            if (limit < 0 && !PyErr_Occurred()) {
                PyErr_SetString(PyExc_ValueError, "cache limit must be >= 0 or None");
                return -1;
            }
        }
        entry->cache_limit = limit;
        if (entry->pattern_cache != NULL) {
            if (limit == 0) {
                if (mapping_clear(entry->pattern_cache) < 0) {
                    PyErr_Clear();
                }
            } else if (limit > 0) {
                if (mapping_ensure_capacity(entry->pattern_cache, limit) < 0) {
                    PyErr_Clear();
                }
            }
        }
        return 0;
    }

    return PyObject_GenericSetAttr(self_obj, name_obj, value);
}

static PyTypeObject ThreadCacheState_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pcre.cache._ThreadCacheState",
    .tp_basicsize = sizeof(ThreadCacheStateObject),
    .tp_dealloc = (destructor)ThreadCacheState_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getattro = ThreadCacheState_getattro,
    .tp_setattro = ThreadCacheState_setattro,
};

// Global cache type -------------------------------------------------------

static void
GlobalCacheState_dealloc(GlobalCacheStateObject *self)
{
    Py_XDECREF(self->pattern_cache);
    Py_XDECREF(self->order);
    if (self->lock != NULL) {
        PyThread_free_lock(self->lock);
        self->lock = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
GlobalCacheState_getattro(PyObject *self_obj, PyObject *name_obj)
{
    GlobalCacheStateObject *self = (GlobalCacheStateObject *)self_obj;
    if (!PyUnicode_Check(name_obj)) {
        return PyObject_GenericGetAttr(self_obj, name_obj);
    }
    const char *name = PyUnicode_AsUTF8(name_obj);
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "pattern_cache") == 0) {
        if (self->pattern_cache == NULL) {
            self->pattern_cache = PyDict_New();
            if (self->pattern_cache == NULL) {
                return NULL;
            }
        }
        Py_INCREF(self->pattern_cache);
        return self->pattern_cache;
    }
    if (strcmp(name, "cache_limit") == 0) {
        if (self->cache_limit < 0) {
            Py_RETURN_NONE;
        }
        return PyLong_FromSsize_t(self->cache_limit);
    }
    return PyObject_GenericGetAttr(self_obj, name_obj);
}

static int
GlobalCacheState_setattro(PyObject *self_obj, PyObject *name_obj, PyObject *value)
{
    GlobalCacheStateObject *self = (GlobalCacheStateObject *)self_obj;
    if (!PyUnicode_Check(name_obj)) {
        return PyObject_GenericSetAttr(self_obj, name_obj, value);
    }
    const char *name = PyUnicode_AsUTF8(name_obj);
    if (name == NULL) {
        return -1;
    }
    if (strcmp(name, "pattern_cache") == 0) {
        if (value == NULL) {
            PyErr_SetString(PyExc_AttributeError, "cannot delete pattern_cache");
            return -1;
        }
        if (ensure_mapping(value) < 0) {
            return -1;
        }
        Py_INCREF(value);
        Py_XDECREF(self->pattern_cache);
        self->pattern_cache = value;
        return 0;
    }
    if (strcmp(name, "cache_limit") == 0) {
        if (value == NULL) {
            PyErr_SetString(PyExc_AttributeError, "cannot delete cache_limit");
            return -1;
        }
        Py_ssize_t limit;
        if (value == Py_None) {
            limit = -1;
        } else {
            PyObject *as_long = PyNumber_Long(value);
            if (as_long == NULL) {
                return -1;
            }
            limit = PyLong_AsSsize_t(as_long);
            Py_DECREF(as_long);
            if (limit < 0 && !PyErr_Occurred()) {
                PyErr_SetString(PyExc_ValueError, "cache limit must be >= 0 or None");
                return -1;
            }
        }
        self->cache_limit = limit;
        if (self->pattern_cache != NULL) {
            if (self->lock != NULL) {
                PyThread_acquire_lock(self->lock, 1);
            }
            if (limit == 0) {
                if (mapping_clear(self->pattern_cache) < 0) {
                    PyErr_Clear();
                }
            } else if (limit > 0) {
                if (mapping_ensure_capacity(self->pattern_cache, limit) < 0) {
                    PyErr_Clear();
                }
            }
            if (self->lock != NULL) {
                PyThread_release_lock(self->lock);
            }
        }
        return 0;
    }
    return PyObject_GenericSetAttr(self_obj, name_obj, value);
}

static PyTypeObject GlobalCacheState_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pcre.cache._GlobalCacheState",
    .tp_basicsize = sizeof(GlobalCacheStateObject),
    .tp_dealloc = (destructor)GlobalCacheState_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getattro = GlobalCacheState_getattro,
    .tp_setattro = GlobalCacheState_setattro,
};

// Backend compilation helper ---------------------------------------------

static PyObject *
call_compile_and_wrap(PyObject *module, PyObject *pattern, long flags, int jit, PyObject *wrapper)
{
    if (!PyCallable_Check(wrapper)) {
        PyErr_SetString(PyExc_TypeError, "wrapper must be callable");
        return NULL;
    }

    PyObject *backend = PyObject_GetAttrString(module, "_pcre2");
    if (backend == NULL) {
        return NULL;
    }
    PyObject *compile_callable = PyObject_GetAttrString(backend, "compile");
    Py_DECREF(backend);
    if (compile_callable == NULL) {
        return NULL;
    }

    PyObject *args = PyTuple_Pack(1, pattern);
    if (args == NULL) {
        Py_DECREF(compile_callable);
        return NULL;
    }

    PyObject *kwargs = PyDict_New();
    if (kwargs == NULL) {
        Py_DECREF(args);
        Py_DECREF(compile_callable);
        return NULL;
    }

    PyObject *flags_obj = PyLong_FromLong(flags);
    if (flags_obj == NULL) {
        Py_DECREF(kwargs);
        Py_DECREF(args);
        Py_DECREF(compile_callable);
        return NULL;
    }

    PyObject *jit_obj = PyBool_FromLong(jit != 0);
    if (jit_obj == NULL) {
        Py_DECREF(flags_obj);
        Py_DECREF(kwargs);
        Py_DECREF(args);
        Py_DECREF(compile_callable);
        return NULL;
    }

    if (PyDict_SetItemString(kwargs, "flags", flags_obj) < 0 ||
        PyDict_SetItemString(kwargs, "jit", jit_obj) < 0) {
        Py_DECREF(jit_obj);
        Py_DECREF(flags_obj);
        Py_DECREF(kwargs);
        Py_DECREF(args);
        Py_DECREF(compile_callable);
        return NULL;
    }

    Py_DECREF(jit_obj);
    Py_DECREF(flags_obj);

    PyObject *compiled = PyObject_Call(compile_callable, args, kwargs);
    Py_DECREF(kwargs);
    Py_DECREF(args);
    Py_DECREF(compile_callable);
    if (compiled == NULL) {
        return NULL;
    }

    PyObject *wrapped = PyObject_CallFunctionObjArgs(wrapper, compiled, NULL);
    Py_DECREF(compiled);
    return wrapped;
}

// Module functions --------------------------------------------------------

static PyObject *
cached_compile(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *pattern = NULL;
    long flags = 0;
    PyObject *wrapper = NULL;
    PyObject *jit_arg = NULL;
    static char *kwlist[] = {"pattern", "flags", "wrapper", "jit", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OlO|$O", kwlist, &pattern, &flags, &wrapper, &jit_arg)) {
        return NULL;
    }
    if (jit_arg == NULL) {
        PyErr_SetString(PyExc_TypeError, "cached_compile() missing required keyword-only argument 'jit'");
        return NULL;
    }

    int jit = PyObject_IsTrue(jit_arg);
    if (jit < 0) {
        return NULL;
    }

    lock_cache_strategy();

    PyObject *flags_obj = PyLong_FromLong(flags);
    if (flags_obj == NULL) {
        return NULL;
    }
    PyObject *jit_bool = PyBool_FromLong(jit != 0);
    if (jit_bool == NULL) {
        Py_DECREF(flags_obj);
        return NULL;
    }
    PyObject *key = PyTuple_New(3);
    if (key == NULL) {
        Py_DECREF(jit_bool);
        Py_DECREF(flags_obj);
        return NULL;
    }
    Py_INCREF(pattern);
    PyTuple_SET_ITEM(key, 0, pattern);
    PyTuple_SET_ITEM(key, 1, flags_obj);  // steals reference
    PyTuple_SET_ITEM(key, 2, jit_bool);   // steals reference

    PyObject *module = self;

    if (CURRENT_STRATEGY == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheEntry *entry = thread_cache_entry_get(THREAD_LOCAL_OBJ, 1);
        if (entry == NULL) {
            Py_DECREF(key);
            return NULL;
        }

        if (entry->cache_limit == 0) {
            Py_DECREF(key);
            return call_compile_and_wrap(module, pattern, flags, jit, wrapper);
        }

        if (entry->pattern_cache == NULL) {
            entry->pattern_cache = PyDict_New();
            if (entry->pattern_cache == NULL) {
                Py_DECREF(key);
                return NULL;
            }
        }

        int is_unhashable = 0;
        PyObject *cached = mapping_get(entry->pattern_cache, key, &is_unhashable);
        if (cached != NULL) {
            Py_DECREF(key);
            return cached;
        }
        if (is_unhashable) {
            Py_DECREF(key);
            return call_compile_and_wrap(module, pattern, flags, jit, wrapper);
        }
        if (PyErr_Occurred()) {
            Py_DECREF(key);
            return NULL;
        }

        PyObject *result = call_compile_and_wrap(module, pattern, flags, jit, wrapper);
        if (result == NULL) {
            Py_DECREF(key);
            return NULL;
        }

        if (entry->cache_limit != 0) {
            if (entry->cache_limit > 0) {
                if (mapping_ensure_capacity(entry->pattern_cache, entry->cache_limit) < 0) {
                    PyErr_Clear();
                }
            }
            if (mapping_set(entry->pattern_cache, key, result) < 0) {
                PyErr_Clear();
            }
        }

        Py_DECREF(key);
        return result;
    }

    GlobalCacheStateObject *state = GLOBAL_STATE_OBJ;
    PyObject *mapping = state->pattern_cache;
    int unhashable = 0;
    PyObject *cached = NULL;

    if (state->lock != NULL) {
        PyThread_acquire_lock(state->lock, 1);
    }

    if (mapping == NULL) {
        mapping = PyDict_New();
        state->pattern_cache = mapping;
    }

    if (state->cache_limit == 0) {
        if (state->lock != NULL) {
            PyThread_release_lock(state->lock);
        }
        Py_DECREF(key);
        return call_compile_and_wrap(module, pattern, flags, jit, wrapper);
    }

    cached = PyDict_GetItemWithError(mapping, key);
    if (cached != NULL) {
        Py_INCREF(cached);
        if (state->lock != NULL) {
            PyThread_release_lock(state->lock);
        }
        Py_DECREF(key);
        return cached;
    }
    if (PyErr_Occurred()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError)) {
            PyErr_Clear();
            unhashable = 1;
        } else {
            if (state->lock != NULL) {
                PyThread_release_lock(state->lock);
            }
            Py_DECREF(key);
            return NULL;
        }
    }

    if (state->lock != NULL) {
        PyThread_release_lock(state->lock);
    }

    PyObject *result = call_compile_and_wrap(module, pattern, flags, jit, wrapper);
    if (result == NULL) {
        Py_DECREF(key);
        return NULL;
    }

    if (unhashable) {
        Py_DECREF(key);
        return result;
    }

    if (state->lock != NULL) {
        PyThread_acquire_lock(state->lock, 1);
    }

    mapping = state->pattern_cache;
    if (mapping == NULL) {
        mapping = PyDict_New();
        state->pattern_cache = mapping;
    }
    if (state->order == NULL) {
        state->order = PyList_New(0);
        if (state->order == NULL) {
            PyErr_Clear();
        } else {
            state->order_head = 0;
        }
    }

    if (state->cache_limit > 0 && mapping != NULL) {
        if (global_cache_trim(state) < 0) {
            PyErr_Clear();
        }
    }

    if (mapping != NULL) {
        if (PyDict_SetItem(mapping, key, result) < 0) {
            PyErr_Clear();
        } else if (state->order != NULL) {
            if (PyList_Append(state->order, key) < 0) {
                PyErr_Clear();
            }
        }
    }

    if (state->cache_limit > 0 && mapping != NULL) {
        if (global_cache_trim(state) < 0) {
            PyErr_Clear();
        }
    }

    if (state->lock != NULL) {
        PyThread_release_lock(state->lock);
    }

    Py_DECREF(key);
    return result;
}

static PyObject *
cache_strategy_fn(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *value = Py_None;
    static char *kwlist[] = {"strategy", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O", kwlist, &value)) {
        return NULL;
    }

    if (value == Py_None) {
        const char *name = CURRENT_STRATEGY == CACHE_STRATEGY_THREAD_LOCAL ? "thread-local" : "global";
        return PyUnicode_FromString(name);
    }

    const char *raw = PyUnicode_AsUTF8(value);
    if (raw == NULL) {
        return NULL;
    }

    CacheStrategy desired;
    if (strcmp(raw, "thread-local") == 0) {
        desired = CACHE_STRATEGY_THREAD_LOCAL;
    } else if (strcmp(raw, "global") == 0) {
        desired = CACHE_STRATEGY_GLOBAL;
    } else {
        PyErr_SetString(PyExc_ValueError, "cache strategy must be 'thread-local' or 'global'");
        return NULL;
    }

    if (desired == CURRENT_STRATEGY) {
        return PyUnicode_FromString(raw);
    }

    if (CACHE_STRATEGY_LOCKED) {
        PyErr_SetString(
            PyExc_RuntimeError,
            "cache strategy is fixed at import time; set PYPCRE_CACHE_PATTERN_GLOBAL=1 before importing pcre to enable the global cache"
        );
        return NULL;
    }

    CURRENT_STRATEGY = desired;
    return PyUnicode_FromString(raw);
}

static PyObject *
clear_cache(PyObject *self, PyObject *Py_UNUSED(args))
{
    if (CURRENT_STRATEGY == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheEntry *entry = thread_cache_entry_get(THREAD_LOCAL_OBJ, 0);
        if (entry != NULL && entry->pattern_cache != NULL) {
            if (mapping_clear(entry->pattern_cache) < 0) {
                return NULL;
            }
        }
    } else {
        GlobalCacheStateObject *state = GLOBAL_STATE_OBJ;
        if (state->lock != NULL) {
            PyThread_acquire_lock(state->lock, 1);
        }
        if (state->pattern_cache != NULL) {
            if (mapping_clear(state->pattern_cache) < 0) {
                if (state->lock != NULL) {
                    PyThread_release_lock(state->lock);
                }
                return NULL;
            }
        }
        if (state->order != NULL) {
            if (PyList_SetSlice(state->order, 0, PyList_GET_SIZE(state->order), NULL) < 0) {
                PyErr_Clear();
            }
            state->order_head = 0;
        }
        if (state->lock != NULL) {
            PyThread_release_lock(state->lock);
        }
    }

    PyObject *backend = PyObject_GetAttrString(self, "_pcre2");
    if (backend == NULL) {
        return NULL;
    }

    PyObject *result = PyObject_CallMethod(backend, "clear_pattern_cache", NULL);
    if (result == NULL) {
        Py_DECREF(backend);
        return NULL;
    }
    Py_DECREF(result);

    result = PyObject_CallMethod(backend, "clear_match_data_cache", NULL);
    if (result == NULL) {
        Py_DECREF(backend);
        return NULL;
    }
    Py_DECREF(result);

    result = PyObject_CallMethod(backend, "clear_jit_stack_cache", NULL);
    Py_DECREF(backend);
    if (result == NULL) {
        return NULL;
    }
    Py_DECREF(result);

    Py_RETURN_NONE;
}

static PyObject *
set_cache_limit(PyObject *self, PyObject *args)
{
    PyObject *value = NULL;
    if (!PyArg_ParseTuple(args, "O", &value)) {
        return NULL;
    }

    Py_ssize_t limit;
    if (value == Py_None) {
        limit = -1;
    } else {
        PyObject *as_long = PyNumber_Long(value);
        if (as_long == NULL) {
            return NULL;
        }
        limit = PyLong_AsSsize_t(as_long);
        Py_DECREF(as_long);
        if (limit < 0 && !PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError, "cache limit must be >= 0 or None");
            return NULL;
        }
    }

    if (CURRENT_STRATEGY == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheEntry *entry = thread_cache_entry_get(THREAD_LOCAL_OBJ, 1);
        if (entry == NULL) {
            return NULL;
        }
        entry->cache_limit = limit;
        if (entry->pattern_cache != NULL) {
            if (limit == 0) {
                if (mapping_clear(entry->pattern_cache) < 0) {
                    return NULL;
                }
            } else if (limit > 0) {
                if (mapping_ensure_capacity(entry->pattern_cache, limit) < 0) {
                    return NULL;
                }
            }
        }
        Py_RETURN_NONE;
    }

    GlobalCacheStateObject *state = GLOBAL_STATE_OBJ;
    state->cache_limit = limit;
    if (state->lock != NULL) {
        PyThread_acquire_lock(state->lock, 1);
    }
    if (limit == 0) {
        if (state->pattern_cache != NULL) {
            if (mapping_clear(state->pattern_cache) < 0) {
                if (state->lock != NULL) {
                    PyThread_release_lock(state->lock);
                }
                return NULL;
            }
        }
        if (state->order != NULL) {
            if (PyList_SetSlice(state->order, 0, PyList_GET_SIZE(state->order), NULL) < 0) {
                PyErr_Clear();
            }
            state->order_head = 0;
        }
    } else if (limit > 0 && state->pattern_cache != NULL) {
        if (global_cache_trim(state) < 0) {
            if (state->lock != NULL) {
                PyThread_release_lock(state->lock);
            }
            return NULL;
        }
    }
    if (state->lock != NULL) {
        PyThread_release_lock(state->lock);
    }
    Py_RETURN_NONE;
}

static PyObject *
get_cache_limit(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(args))
{
    if (CURRENT_STRATEGY == CACHE_STRATEGY_THREAD_LOCAL) {
        ThreadCacheEntry *entry = thread_cache_entry_get(THREAD_LOCAL_OBJ, 1);
        if (entry == NULL) {
            return NULL;
        }
        if (entry->cache_limit < 0) {
            Py_RETURN_NONE;
        }
        return PyLong_FromSsize_t(entry->cache_limit);
    }

    GlobalCacheStateObject *state = GLOBAL_STATE_OBJ;
    if (state->cache_limit < 0) {
        Py_RETURN_NONE;
    }
    return PyLong_FromSsize_t(state->cache_limit);
}

static PyMethodDef module_methods[] = {
    {"cached_compile", (PyCFunction)cached_compile, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Compile pattern with caching.")},
    {"cache_strategy", (PyCFunction)cache_strategy_fn, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("Query or set the cache strategy." )},
    {"clear_cache", (PyCFunction)clear_cache, METH_NOARGS, PyDoc_STR("Clear cached patterns and backend caches." )},
    {"set_cache_limit", (PyCFunction)set_cache_limit, METH_VARARGS, PyDoc_STR("Set the cache capacity for the active strategy." )},
    {"get_cache_limit", (PyCFunction)get_cache_limit, METH_NOARGS, PyDoc_STR("Return the cache capacity for the active strategy." )},
    {NULL, NULL, 0, NULL},
};

static int
initialise_default_strategy(void)
{
    if (DEFAULT_STRATEGY_INITIALISED) {
        return 0;
    }
    const char *env = Py_GETENV("PYPCRE_CACHE_PATTERN_GLOBAL");
    if (env == NULL) {
        env = Py_GETENV("PCRE2_CACHE_PATTERN_GLOBAL");
    }
    CURRENT_STRATEGY = is_truthy_env_flag(env) ? CACHE_STRATEGY_GLOBAL : CACHE_STRATEGY_THREAD_LOCAL;
    DEFAULT_STRATEGY_INITIALISED = 1;
    return 0;
}

static int cache_module_exec(PyObject *module);

#if defined(Py_mod_multiple_interpreters)
static PyModuleDef_Slot module_slots[] = {
    {Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
    {Py_mod_gil, Py_MOD_GIL_USED},
    {Py_mod_exec, cache_module_exec},
    {0, NULL},
};
#else
static PyModuleDef_Slot module_slots[] = {
    {Py_mod_gil, Py_MOD_GIL_USED},
    {Py_mod_exec, cache_module_exec},
    {0, NULL},
};
#endif

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    .m_name = "pcre.cache",
    .m_doc = "Pattern caching helpers for the high level PCRE wrapper.",
    .m_size = 0,
    .m_methods = module_methods,
    .m_slots = module_slots,
#if defined(Py_MOD_GIL_SAFE_FLAG)
    .m_flags = Py_MOD_GIL_SAFE_FLAG,
#endif
};

PyMODINIT_FUNC
PyInit_cache(void)
{
    return PyModuleDef_Init(&module_def);
}

static int
cache_module_exec(PyObject *module)
{
    PyObject *backend = NULL;
    ThreadCacheStateObject *thread_state = NULL;
    GlobalCacheStateObject *global_state = NULL;

    if (PyType_Ready(&ThreadCacheState_Type) < 0) {
        return -1;
    }
    if (PyType_Ready(&GlobalCacheState_Type) < 0) {
        return -1;
    }

    if (initialise_default_strategy() < 0) {
        return -1;
    }

#ifdef Py_GIL_DISABLED
    if (PyUnstable_Module_SetGIL(module, Py_MOD_GIL_USED) < 0) {
        return -1;
    }
#endif

    thread_state = PyObject_New(ThreadCacheStateObject, &ThreadCacheState_Type);
    if (thread_state == NULL) {
        return -1;
    }
    memset(&thread_state->tss_key, 0, sizeof(Py_tss_t));
    thread_state->tss_initialized = 0;
    thread_state->cleanup_key = NULL;

    if (PyModule_AddObject(module, "_THREAD_LOCAL", (PyObject *)thread_state) < 0) {
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }
    Py_INCREF(thread_state);

    global_state = PyObject_New(GlobalCacheStateObject, &GlobalCacheState_Type);
    if (global_state == NULL) {
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }
    global_state->pattern_cache = PyDict_New();
    if (global_state->pattern_cache == NULL) {
        Py_DECREF((PyObject *)global_state);
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }
    global_state->lock = PyThread_allocate_lock();
    if (global_state->lock == NULL) {
        PyErr_NoMemory();
        Py_DECREF(global_state->pattern_cache);
        Py_DECREF((PyObject *)global_state);
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }
    global_state->order = PyList_New(0);
    if (global_state->order == NULL) {
        PyThread_free_lock(global_state->lock);
        Py_DECREF(global_state->pattern_cache);
        Py_DECREF((PyObject *)global_state);
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }
    global_state->order_head = 0;
    global_state->cache_limit = DEFAULT_GLOBAL_CACHE_LIMIT;

    if (PyModule_AddObject(module, "_GLOBAL_STATE", (PyObject *)global_state) < 0) {
        PyThread_free_lock(global_state->lock);
        Py_DECREF(global_state->pattern_cache);
        Py_DECREF(global_state->order);
        Py_DECREF((PyObject *)global_state);
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }
    Py_INCREF(global_state);

    if (PyModule_AddIntConstant(module, "_DEFAULT_THREAD_CACHE_LIMIT", DEFAULT_THREAD_CACHE_LIMIT) < 0 ||
        PyModule_AddIntConstant(module, "_DEFAULT_GLOBAL_CACHE_LIMIT", DEFAULT_GLOBAL_CACHE_LIMIT) < 0) {
        Py_DECREF((PyObject *)global_state);
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }

    backend = PyImport_ImportModule("pcre_ext_c");
    if (backend == NULL) {
        Py_DECREF((PyObject *)global_state);
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }
    if (PyModule_AddObject(module, "_pcre2", backend) < 0) {
        Py_DECREF(backend);
        Py_DECREF((PyObject *)global_state);
        Py_DECREF((PyObject *)thread_state);
        return -1;
    }

    if (THREAD_LOCAL_OBJ != NULL) {
        Py_DECREF((PyObject *)THREAD_LOCAL_OBJ);
    }
    THREAD_LOCAL_OBJ = thread_state;
    thread_state = NULL;

    if (GLOBAL_STATE_OBJ != NULL) {
        Py_DECREF((PyObject *)GLOBAL_STATE_OBJ);
    }
    GLOBAL_STATE_OBJ = global_state;
    global_state = NULL;

    return 0;
}
