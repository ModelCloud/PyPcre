#define _POSIX_C_SOURCE 200809L

#include <Python.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>

#ifdef HAVE_JEMALLOC
#if defined(__has_include)
#  if __has_include(<jemalloc/jemalloc.h>)
#    include <jemalloc/jemalloc.h>
#  else
void *je_malloc(size_t size);
void je_free(void *ptr);
#  endif
#else
void *je_malloc(size_t size);
void je_free(void *ptr);
#endif
#endif

#ifdef HAVE_TCMALLOC
#if defined(__has_include)
#  if __has_include(<gperftools/tcmalloc.h>)
#    include <gperftools/tcmalloc.h>
#  else
void *tc_malloc(size_t size);
void tc_free(void *ptr);
#  endif
#else
void *tc_malloc(size_t size);
void tc_free(void *ptr);
#endif
#endif

typedef void *(*alloc_fn)(size_t size);
typedef void (*free_fn)(void *ptr);

typedef struct allocator {
    const char *name;
    alloc_fn alloc;
    free_fn free;
    int needs_python;
    int (*setup)(void);
} allocator;

static void *pymem_alloc(size_t size)
{
    return PyMem_Malloc(size);
}

static void pymem_free(void *ptr)
{
    PyMem_Free(ptr);
}

static void *malloc_alloc(size_t size)
{
    return malloc(size);
}

static void malloc_free(void *ptr)
{
    free(ptr);
}

#ifdef HAVE_JEMALLOC
static void *jemalloc_handle = NULL;
static void *(*jemalloc_malloc_fn)(size_t) = NULL;
static void (*jemalloc_free_fn)(void *) = NULL;

static int
jemalloc_setup(void)
{
    if (jemalloc_handle != NULL) {
        return 0;
    }

    const char *candidates[] = {
        "libjemalloc.so",
        "libjemalloc.so.2",
        NULL,
    };

    for (size_t i = 0; candidates[i] != NULL; ++i) {
        jemalloc_handle = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
        if (jemalloc_handle != NULL) {
            break;
        }
    }

    if (jemalloc_handle == NULL) {
        fprintf(stderr, "jemalloc: dlopen failed: %s\n", dlerror());
        return -1;
    }

    jemalloc_malloc_fn = (void *(*)(size_t))dlsym(jemalloc_handle, "malloc");
    jemalloc_free_fn = (void (*)(void *))dlsym(jemalloc_handle, "free");

    if (jemalloc_malloc_fn == NULL || jemalloc_free_fn == NULL) {
        fprintf(stderr, "jemalloc: dlsym failed: %s\n", dlerror());
        return -1;
    }

    return 0;
}

static void *
jemalloc_alloc(size_t size)
{
    return jemalloc_malloc_fn(size);
}

static void
jemalloc_free(void *ptr)
{
    jemalloc_free_fn(ptr);
}
#endif

#ifdef HAVE_TCMALLOC
static void *tcmalloc_handle = NULL;
static void *(*tcmalloc_malloc_fn)(size_t) = NULL;
static void (*tcmalloc_free_fn)(void *) = NULL;

static int
tcmalloc_setup(void)
{
    if (tcmalloc_handle != NULL) {
        return 0;
    }

    const char *candidates[] = {
        "libtcmalloc_minimal.so",
        "libtcmalloc_minimal.so.4",
        "libtcmalloc.so",
        NULL,
    };

    for (size_t i = 0; candidates[i] != NULL; ++i) {
        tcmalloc_handle = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
        if (tcmalloc_handle != NULL) {
            break;
        }
    }

    if (tcmalloc_handle == NULL) {
        fprintf(stderr, "tcmalloc: dlopen failed: %s\n", dlerror());
        return -1;
    }

    tcmalloc_malloc_fn = (void *(*)(size_t))dlsym(tcmalloc_handle, "tc_malloc");
    tcmalloc_free_fn = (void (*)(void *))dlsym(tcmalloc_handle, "tc_free");

    if (tcmalloc_malloc_fn == NULL || tcmalloc_free_fn == NULL) {
        fprintf(stderr, "tcmalloc: dlsym failed: %s\n", dlerror());
        return -1;
    }

    return 0;
}

static void *
tcmalloc_alloc(size_t size)
{
    return tcmalloc_malloc_fn(size);
}

static void
tcmalloc_free(void *ptr)
{
    tcmalloc_free_fn(ptr);
}
#endif

typedef struct sample_case {
    size_t size;
    size_t iterations;
} sample_case;

static const sample_case samples[] = {
    {64, 400000},
    {256, 200000},
    {1024, 100000},
    {8192, 50000},
    {65536, 10000},
    {262144, 4000},
    {1048576, 1000},
};

static const size_t sample_count = sizeof(samples) / sizeof(samples[0]);

static double
elapsed_ns(const struct timespec *start, const struct timespec *end)
{
    double seconds = (double)(end->tv_sec - start->tv_sec);
    double nanoseconds = (double)(end->tv_nsec - start->tv_nsec);
    return seconds * 1e9 + nanoseconds;
}

static volatile size_t sink = 0;

static int
run_sample(const allocator *impl, size_t size, size_t iterations)
{
    struct timespec t0 = {0, 0};
    struct timespec t1 = {0, 0};
    size_t local_sink = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        return -1;
    }

    for (size_t i = 0; i < iterations; ++i) {
        void *ptr = impl->alloc(size);
        if (ptr == NULL) {
            fprintf(stderr, "%s: allocation failure at size %zu\n", impl->name, size);
            return -1;
        }
        memset(ptr, (int)(i & 0xFF), size < 64 ? size : 64);
        local_sink += (uintptr_t)ptr;
        impl->free(ptr);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        return -1;
    }

    double total_ns = elapsed_ns(&t0, &t1);
    double per_op_ns = total_ns / (double)iterations;

    printf("  %8zu bytes | %8zu iters | %10.3f ms total | %9.3f ns/op\n",
           size,
           iterations,
           total_ns / 1e6,
           per_op_ns);

    sink += local_sink;
    return 0;
}

static int
run_allocator(const allocator *impl)
{
    printf("\n=== %s ===\n", impl->name);
    fflush(stdout);

    if (impl->setup != NULL) {
        if (impl->setup() != 0) {
            printf("  (skipped: setup failed)\n");
            return 0;
        }
    }

    for (size_t i = 0; i < sample_count; ++i) {
        if (run_sample(impl, samples[i].size, samples[i].iterations) != 0) {
            return -1;
        }
    }

    return 0;
}

static int
finalize_python(void)
{
#if PY_VERSION_HEX >= 0x03070000
    return Py_FinalizeEx();
#else
    Py_Finalize();
    return 0;
#endif
}

int
main(void)
{
    allocator allocators[] = {
        {"pymem", pymem_alloc, pymem_free, 1, NULL},
        {"malloc", malloc_alloc, malloc_free, 0, NULL},
#ifdef HAVE_JEMALLOC
        {"jemalloc", jemalloc_alloc, jemalloc_free, 0, jemalloc_setup},
#endif
#ifdef HAVE_TCMALLOC
        {"tcmalloc", tcmalloc_alloc, tcmalloc_free, 0, tcmalloc_setup},
#endif
    };
    const size_t allocator_count = sizeof(allocators) / sizeof(allocators[0]);

    int needs_python = 0;
    for (size_t i = 0; i < allocator_count; ++i) {
        if (allocators[i].needs_python) {
            needs_python = 1;
            break;
        }
    }

    if (needs_python) {
        Py_Initialize();
    }

    for (size_t i = 0; i < allocator_count; ++i) {
        if (run_allocator(&allocators[i]) != 0) {
            if (needs_python) {
                (void)finalize_python();
            }
            return EXIT_FAILURE;
        }
    }

    if (needs_python) {
        if (finalize_python() < 0) {
            fprintf(stderr, "Py_FinalizeEx failed\n");
            return EXIT_FAILURE;
        }
    }

    printf("\nSink checksum: %" PRIuMAX "\n", (uintmax_t)sink);
    return EXIT_SUCCESS;
}
