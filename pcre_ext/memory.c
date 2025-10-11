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
static alloc_fn current_alloc = (alloc_fn)PyMem_Malloc;
static free_fn current_free = (free_fn)PyMem_Free;
static const char *current_name = "pymem";
static int allocator_initialized = 0;

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
    if (allocator_initialized) {
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

    const char *forced = getenv("PYPCRE_ALLOCATOR");

    if (forced == NULL || *forced == '\0' || equals_ignore_case(forced, "pymem")) {
        current_handle = NULL;
        current_alloc = (alloc_fn)PyMem_Malloc;
        current_free = (free_fn)PyMem_Free;
        current_name = "pymem";
        allocator_initialized = 1;
        return 0;
    }

    if (equals_ignore_case(forced, "malloc")) {
        current_handle = NULL;
        current_alloc = malloc;
        current_free = free;
        current_name = "malloc";
        allocator_initialized = 1;
        return 0;
    }

    if (equals_ignore_case(forced, "jemalloc")) {
        if (load_allocator(&jemalloc_candidate) == 0) {
            allocator_initialized = 1;
            return 0;
        }
    } else if (equals_ignore_case(forced, "tcmalloc")) {
        if (load_allocator(&tcmalloc_candidate) == 0) {
            allocator_initialized = 1;
            return 0;
        }
    }

    current_handle = NULL;
    current_alloc = (alloc_fn)PyMem_Malloc;
    current_free = (free_fn)PyMem_Free;
    current_name = "pymem";
    allocator_initialized = 1;
    return 0;
}

void
pcre_memory_teardown(void)
{
#if !defined(_WIN32)
    void *handle_to_close = current_handle;
    current_handle = NULL;
    if (handle_to_close != NULL) {
        dlclose(handle_to_close);
    }
#endif
    current_alloc = (alloc_fn)PyMem_Malloc;
    current_free = (free_fn)PyMem_Free;
    current_name = "pymem";
    allocator_initialized = 0;
}

void *
pcre_malloc(size_t size)
{
    if (!allocator_initialized) {
        if (pcre_memory_initialize() != 0) {
            return NULL;
        }
    }
    return current_alloc(size);
}

void
pcre_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    current_free(ptr);
}

const char *
pcre_memory_allocator_name(void)
{
    return current_name;
}
