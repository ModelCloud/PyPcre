#define _POSIX_C_SOURCE 200809L

#include "pcre2_module.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

typedef void *(*alloc_fn)(size_t size);
typedef void (*free_fn)(void *ptr);

typedef struct allocator_candidate {
    const char *name;
    const char *const *libraries;
    const char *alloc_symbol;
    const char *free_symbol;
} allocator_candidate;

static void *current_handle = NULL;
static alloc_fn current_alloc = malloc;
static free_fn current_free = free;
static const char *current_name = "malloc";
static int allocator_initialized = 0;
static PyThread_type_lock allocator_lock = NULL;

static int
ensure_allocator_lock(void)
{
    if (allocator_lock != NULL) {
        return 0;
    }
    allocator_lock = PyThread_allocate_lock();
    if (allocator_lock == NULL) {
        return -1;
    }
    return 0;
}

static inline void
allocator_lock_acquire(void)
{
    PyThread_acquire_lock(allocator_lock, 1);
}

static inline void
allocator_lock_release(void)
{
    PyThread_release_lock(allocator_lock);
}

#if defined(_WIN32)
static int
load_allocator(const allocator_candidate *candidate)
{
    (void)candidate;
    return -1;
}
#else
static int
load_allocator(const allocator_candidate *candidate)
{
    const char *const *lib = candidate->libraries;
    void *handle = NULL;

    for (; *lib != NULL; ++lib) {
        handle = dlopen(*lib, RTLD_LAZY | RTLD_LOCAL);
        if (handle != NULL) {
            break;
        }
    }

    if (handle == NULL) {
        return -1;
    }

    dlerror();
    alloc_fn alloc = (alloc_fn)dlsym(handle, candidate->alloc_symbol);
    const char *alloc_error = dlerror();
    if (alloc_error != NULL || alloc == NULL) {
        dlclose(handle);
        return -1;
    }

    dlerror();
    free_fn free_fn_ptr = (free_fn)dlsym(handle, candidate->free_symbol);
    const char *free_error = dlerror();
    if (free_error != NULL || free_fn_ptr == NULL) {
        dlclose(handle);
        return -1;
    }

    current_handle = handle;
    current_alloc = alloc;
    current_free = free_fn_ptr;
    current_name = candidate->name;
    return 0;
}
#endif

static int
equals_ignore_case(const char *value, const char *target)
{
    if (value == NULL || target == NULL) {
        return 0;
    }
    while (*value != '\0' && *target != '\0') {
        unsigned char a = (unsigned char)*value++;
        unsigned char b = (unsigned char)*target++;
        if (tolower(a) != tolower(b)) {
            return 0;
        }
    }
    return *value == '\0' && *target == '\0';
}

int
pcre_memory_initialize(void)
{
    if (ensure_allocator_lock() < 0) {
        PyErr_NoMemory();
        return -1;
    }

    allocator_lock_acquire();
    if (allocator_initialized) {
        allocator_lock_release();
        return 0;
    }

    static const char *const jemalloc_libs[] = {
        "libjemalloc.so",
        "libjemalloc.so.2",
        NULL,
    };
    static const allocator_candidate jemalloc_candidate = {
        .name = "jemalloc",
        .libraries = jemalloc_libs,
        .alloc_symbol = "malloc",
        .free_symbol = "free",
    };

    static const char *const tcmalloc_libs[] = {
        "libtcmalloc_minimal.so",
        "libtcmalloc_minimal.so.4",
        "libtcmalloc.so",
        NULL,
    };
    static const allocator_candidate tcmalloc_candidate = {
        .name = "tcmalloc",
        .libraries = tcmalloc_libs,
        .alloc_symbol = "tc_malloc",
        .free_symbol = "tc_free",
    };

    const allocator_candidate *const default_candidates[] = {
        &jemalloc_candidate,
        &tcmalloc_candidate,
        NULL,
    };

    const char *forced = getenv("PCRE_ALLOCATOR");
    const allocator_candidate *order_storage[4] = {NULL, NULL, NULL, NULL};
    const allocator_candidate *const *candidates = default_candidates;

    if (forced != NULL && *forced != '\0') {
        size_t pos = 0;

        if (equals_ignore_case(forced, "malloc")) {
            current_handle = NULL;
            current_alloc = malloc;
            current_free = free;
            current_name = "malloc";
            allocator_initialized = 1;
            allocator_lock_release();
            return 0;
        }

        if (equals_ignore_case(forced, "pymem")) {
            current_handle = NULL;
            current_alloc = (alloc_fn)PyMem_Malloc;
            current_free = (free_fn)PyMem_Free;
            current_name = "pymem";
            allocator_initialized = 1;
            allocator_lock_release();
            return 0;
        }

        if (equals_ignore_case(forced, "jemalloc")) {
            order_storage[pos++] = &jemalloc_candidate;
        } else if (equals_ignore_case(forced, "tcmalloc")) {
            order_storage[pos++] = &tcmalloc_candidate;
        }

        if (pos > 0) {
            if (!equals_ignore_case(forced, "jemalloc")) {
                order_storage[pos++] = &jemalloc_candidate;
            }
            if (!equals_ignore_case(forced, "tcmalloc")) {
                order_storage[pos++] = &tcmalloc_candidate;
            }
            order_storage[pos] = NULL;
            candidates = order_storage;
        }
    }

    for (size_t i = 0; candidates[i] != NULL; ++i) {
        if (load_allocator(candidates[i]) == 0) {
            allocator_initialized = 1;
            allocator_lock_release();
            return 0;
        }
    }

    current_handle = NULL;
    current_alloc = malloc;
    current_free = free;
    current_name = "malloc";
    allocator_initialized = 1;
    allocator_lock_release();
    return 0;
}

void
pcre_memory_teardown(void)
{
    if (ensure_allocator_lock() < 0) {
        return;
    }

    allocator_lock_acquire();
#if !defined(_WIN32)
    void *handle_to_close = current_handle;
    current_handle = NULL;
#endif
    current_alloc = malloc;
    current_free = free;
    current_name = "malloc";
    allocator_initialized = 0;
    allocator_lock_release();

#if !defined(_WIN32)
    if (handle_to_close != NULL) {
        dlclose(handle_to_close);
    }
#endif
}

void *
pcre_malloc(size_t size)
{
    if (ensure_allocator_lock() < 0) {
        return NULL;
    }

    allocator_lock_acquire();
    int initialized = allocator_initialized;
    allocator_lock_release();

    if (!initialized) {
        if (pcre_memory_initialize() != 0) {
            return NULL;
        }
    }

    allocator_lock_acquire();
    alloc_fn alloc = current_alloc;
    allocator_lock_release();
    return alloc(size);
}

void
pcre_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    if (ensure_allocator_lock() < 0) {
        current_free(ptr);
        return;
    }

    allocator_lock_acquire();
    free_fn free_fn_ptr = current_free;
    allocator_lock_release();
    free_fn_ptr(ptr);
}

const char *
pcre_memory_allocator_name(void)
{
    if (ensure_allocator_lock() < 0) {
        return current_name;
    }

    allocator_lock_acquire();
    const char *name = current_name;
    allocator_lock_release();
    return name;
}
