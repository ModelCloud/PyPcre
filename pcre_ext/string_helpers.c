// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#   include <immintrin.h>
#endif
#if defined(_M_X64) || defined(_M_AMD64)
#   include <intrin.h>
#endif

static inline int
escape_required(Py_UCS4 character)
{
    switch (character) {
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '?':
        case '*':
        case '+':
        case '-':
        case '|':
        case '^':
        case '$':
        case '\\':
        case '.':
        case '&':
        case '~':
        case '#':
        case ' ':
        case '\t':
        case '\n':
        case '\v':
        case '\f':
        case '\r':
            return 1;
        default:
            return 0;
    }
}

static PyObject *
escape_exact_bytes(PyObject *pattern)
{
    const unsigned char *input = (const unsigned char *)PyBytes_AS_STRING(pattern);
    Py_ssize_t length = PyBytes_GET_SIZE(pattern);
    Py_ssize_t escape_count = 0;
    for (Py_ssize_t index = 0; index < length; ++index) {
        escape_count += escape_required((Py_UCS4)input[index]);
    }

    if (escape_count == 0) {
        return Py_NewRef(pattern);
    }
    if (length > PY_SSIZE_T_MAX - escape_count) {
        return PyErr_NoMemory();
    }

    PyObject *result = PyBytes_FromStringAndSize(NULL, length + escape_count);
    if (result == NULL) {
        return NULL;
    }
    char *output = PyBytes_AS_STRING(result);
    for (Py_ssize_t index = 0; index < length; ++index) {
        unsigned char character = input[index];
        if (escape_required((Py_UCS4)character)) {
            *output++ = '\\';
        }
        *output++ = (char)character;
    }
    return result;
}

static PyObject *
escape_exact_unicode(PyObject *pattern)
{
    Py_ssize_t length = PyUnicode_GET_LENGTH(pattern);
    int input_kind = PyUnicode_KIND(pattern);
    void *input_data = PyUnicode_DATA(pattern);
    Py_ssize_t escape_count = 0;
    for (Py_ssize_t index = 0; index < length; ++index) {
        escape_count += escape_required(PyUnicode_READ(input_kind, input_data, index));
    }

    if (escape_count == 0) {
        return Py_NewRef(pattern);
    }
    if (length > PY_SSIZE_T_MAX - escape_count) {
        return PyErr_NoMemory();
    }

    PyObject *result = PyUnicode_New(
        length + escape_count,
        PyUnicode_MAX_CHAR_VALUE(pattern)
    );
    if (result == NULL) {
        return NULL;
    }
    int output_kind = PyUnicode_KIND(result);
    void *output_data = PyUnicode_DATA(result);
    Py_ssize_t output_index = 0;
    for (Py_ssize_t index = 0; index < length; ++index) {
        Py_UCS4 character = PyUnicode_READ(input_kind, input_data, index);
        if (escape_required(character)) {
            PyUnicode_WRITE(output_kind, output_data, output_index++, '\\');
        }
        PyUnicode_WRITE(output_kind, output_data, output_index++, character);
    }
    return result;
}

static PyObject *
escape_stdlib_fallback(PyObject *pattern)
{
    /* Preserve dynamic ``str.translate`` overrides and the full stdlib buffer
     * coercion/error behaviour for non-exact inputs. This is intentionally
     * uncached: keeping a ``re`` module or callable in process-global extension
     * state would create interpreter-lifetime and teardown hazards. */
    PyObject *re_module = PyImport_ImportModule("re");
    if (re_module == NULL) {
        return NULL;
    }
    PyObject *escape_callable = PyObject_GetAttrString(re_module, "escape");
    Py_DECREF(re_module);
    if (escape_callable == NULL) {
        return NULL;
    }
    PyObject *result = PyObject_CallOneArg(escape_callable, pattern);
    Py_DECREF(escape_callable);
    return result;
}

PyObject *
module_escape(PyObject *Py_UNUSED(module),
              PyObject *const *args,
              Py_ssize_t nargs,
              PyObject *kwnames)
{
    Py_ssize_t keyword_count = kwnames == NULL ? 0 : PyTuple_GET_SIZE(kwnames);
    PyObject *pattern = NULL;

    if (nargs == 1 && keyword_count == 0) {
        pattern = args[0];
    } else if (nargs == 0 && keyword_count == 1) {
        PyObject *keyword = PyTuple_GET_ITEM(kwnames, 0);
        if (PyUnicode_Check(keyword) &&
            PyUnicode_CompareWithASCIIString(keyword, "pattern") == 0) {
            pattern = args[0];
        } else {
            PyErr_Format(PyExc_TypeError,
                         "escape() got an unexpected keyword argument '%U'",
                         keyword);
            return NULL;
        }
    } else {
        PyErr_Format(PyExc_TypeError,
                     "escape() takes 1 argument (%zd given)",
                     nargs + keyword_count);
        return NULL;
    }

    if (PyUnicode_CheckExact(pattern)) {
        return escape_exact_unicode(pattern);
    }
    if (PyBytes_CheckExact(pattern)) {
        return escape_exact_bytes(pattern);
    }
    return escape_stdlib_fallback(pattern);
}

static inline Py_ssize_t
ascii_prefix_length_scalar(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    while (offset < max_len) {
        if ((data[offset] & 0x80) != 0) {
            break;
        }
        offset += 1;
    }
    return offset;
}

#if defined(__x86_64__) && defined(__GNUC__)

static int
ascii_vector_mode_gnu(void)
{
    static ATOMIC_VAR(int) cached = ATOMIC_VAR_INIT(-1);
    int value = atomic_load_explicit(&cached, memory_order_acquire);
    if (value != -1) {
        return value;
    }

    int detected = 0;
#if defined(__has_builtin)
#  if __has_builtin(__builtin_cpu_supports)
#    define PCRE_HAVE_CPU_SUPPORTS 1
#  endif
#elif defined(__GNUC__)
#  if (__GNUC__ > 4)
#    define PCRE_HAVE_CPU_SUPPORTS 1
#  endif
#endif
#if defined(PCRE_HAVE_CPU_SUPPORTS)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx512bw")) {
        detected = 3;
    } else if (__builtin_cpu_supports("avx2")) {
        detected = 2;
    } else if (__builtin_cpu_supports("sse2")) {
        detected = 1;
    }
#endif

    int expected = -1;
    if (!atomic_compare_exchange_strong_explicit(
            &cached,
            &expected,
            detected,
            memory_order_acq_rel,
            memory_order_acquire)) {
        detected = atomic_load_explicit(&cached, memory_order_acquire);
    }

    return detected;
}

#if defined(__GNUC__)
__attribute__((target("avx512bw")))
static Py_ssize_t
ascii_prefix_length_avx512(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    const Py_ssize_t step = 64;
    while (offset + step <= max_len) {
        __m512i chunk = _mm512_loadu_si512((const void *)(data + offset));
        __mmask64 mask = _mm512_movepi8_mask(chunk);
        if (mask != 0) {
            unsigned long idx = __builtin_ctzll(mask);
            return offset + (Py_ssize_t)idx;
        }
        offset += step;
    }
    return offset + ascii_prefix_length_scalar(data + offset, max_len - offset);
}

__attribute__((target("avx2")))
static Py_ssize_t
ascii_prefix_length_avx2(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    const Py_ssize_t step = 32;
    while (offset + step <= max_len) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)(data + offset));
        unsigned int mask = (unsigned int)_mm256_movemask_epi8(chunk);
        if (mask != 0) {
            unsigned int idx = __builtin_ctz(mask);
            return offset + (Py_ssize_t)idx;
        }
        offset += step;
    }
    return offset + ascii_prefix_length_scalar(data + offset, max_len - offset);
}

__attribute__((target("sse2")))
static Py_ssize_t
ascii_prefix_length_sse2(const char *data, Py_ssize_t max_len)
{
    Py_ssize_t offset = 0;
    const Py_ssize_t step = 16;
    while (offset + step <= max_len) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(data + offset));
        unsigned int mask = (unsigned int)_mm_movemask_epi8(chunk);
        if (mask != 0) {
            unsigned int idx = __builtin_ctz(mask);
            return offset + (Py_ssize_t)idx;
        }
        offset += step;
    }
    return offset + ascii_prefix_length_scalar(data + offset, max_len - offset);
}
#endif

#elif defined(_M_X64) || defined(_M_AMD64)

static int
ascii_vector_mode_msvc(void)
{
    static ATOMIC_VAR(int) cached = ATOMIC_VAR_INIT(-1);
    int value = atomic_load_explicit(&cached, memory_order_acquire);
    if (value != -1) {
        return value;
    }

    int detected = 0;
    int cpu_info[4] = {0};

    __cpuid(cpu_info, 0);
    int max_leaf = cpu_info[0];

    if (max_leaf >= 1) {
        __cpuid(cpu_info, 1);

        const int has_sse2 = (cpu_info[3] & (1 << 26)) != 0;
        if (has_sse2) {
            detected = 1;
        }

        const int has_osxsave = (cpu_info[2] & (1 << 27)) != 0;
        const int has_avx = (cpu_info[2] & (1 << 28)) != 0;

        unsigned long long xcr0 = 0;
        if (has_osxsave) {
            xcr0 = _xgetbv(0);
        }

        const unsigned long long xcr0_avx_mask = (1ull << 1) | (1ull << 2);

        if (has_osxsave && has_avx && (xcr0 & xcr0_avx_mask) == xcr0_avx_mask && max_leaf >= 7) {
            __cpuidex(cpu_info, 7, 0);

            const int has_avx2 = (cpu_info[1] & (1 << 5)) != 0;
            if (has_avx2) {
                detected = 2;
            }

            const int has_avx512f = (cpu_info[1] & (1 << 16)) != 0;
            const int has_avx512bw = (cpu_info[1] & (1 << 30)) != 0;
            const unsigned long long xcr0_avx512_mask = (1ull << 1) | (1ull << 2) | (1ull << 5) | (1ull << 6) | (1ull << 7);
            if (has_avx512f && has_avx512bw && (xcr0 & xcr0_avx512_mask) == xcr0_avx512_mask) {
                detected = 3;
            }
        }
    }

    int expected = -1;
    if (!atomic_compare_exchange_strong_explicit(
            &cached,
            &expected,
            detected,
            memory_order_acq_rel,
            memory_order_acquire)) {
        detected = atomic_load_explicit(&cached, memory_order_acquire);
    }

    return detected;
}

#endif

int
ascii_vector_mode(void)
{
#if defined(__x86_64__) && defined(__GNUC__)
    return ascii_vector_mode_gnu();
#elif defined(_M_X64) || defined(_M_AMD64)
    return ascii_vector_mode_msvc();
#else
    return 0;
#endif
}

Py_ssize_t
ascii_prefix_length(const char *data, Py_ssize_t max_len)
{
#if defined(__x86_64__) && defined(__GNUC__)
    switch (ascii_vector_mode_gnu()) {
        case 3:
            return ascii_prefix_length_avx512(data, max_len);
        case 2:
            return ascii_prefix_length_avx2(data, max_len);
        case 1:
            return ascii_prefix_length_sse2(data, max_len);
        default:
            break;
    }
#endif
    return ascii_prefix_length_scalar(data, max_len);
}

int
ensure_valid_utf8_for_bytes_subject(PatternObject *pattern,
                                    int subject_is_bytes,
                                    const char *data,
                                    Py_ssize_t length)
{
    if (!subject_is_bytes) {
        return 0;
    }

    if ((pattern->compile_options & PCRE2_UTF) == 0) {
        return 0;
    }

    PyObject *utf8_check = PyUnicode_DecodeUTF8(data, length, NULL);
    if (utf8_check == NULL) {
        PyErr_Clear();
        PyErr_SetString(PcreError,
                        "bytes subject must be valid UTF-8 when pattern uses the UTF flag");
        return -1;
    }
    Py_DECREF(utf8_check);
    return 0;
}

static inline int
is_hex_digit(unsigned char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static inline unsigned int
hex_value(unsigned char value)
{
    if (value >= '0' && value <= '9') {
        return (unsigned int)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (unsigned int)(value - 'a' + 10);
    }
    return (unsigned int)(value - 'A' + 10);
}

PyObject *
module_translate_unicode_escapes(PyObject *Py_UNUSED(module), PyObject *arg)
{
    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "pattern must be str");
        return NULL;
    }

    Py_ssize_t byte_length = 0;
    const char *src = PyUnicode_AsUTF8AndSize(arg, &byte_length);
    if (src == NULL) {
        return NULL;
    }

    if (byte_length < 2) {
        return Py_NewRef(arg);
    }

    if (byte_length > (PY_SSIZE_T_MAX - 1) / 2) {
        PyErr_SetString(PyExc_OverflowError, "pattern too large to translate");
        return NULL;
    }

    Py_ssize_t capacity = (byte_length * 2) + 1;
    char *buffer = PyMem_Malloc((size_t)capacity);
    if (buffer == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    const char *p = src;
    const char *end = src + byte_length;
    char *out = buffer;
    int modified = 0;

    while (p < end) {
        if ((size_t)(end - p) >= 3 && p[0] == '(' && p[1] == '?' && p[2] == '#') {
            const char *comment_end = memchr(p + 3, ')', (size_t)(end - (p + 3)));
            if (comment_end == NULL) {
                memcpy(out, p, (size_t)(end - p));
                out += end - p;
                p = end;
                break;
            }
            size_t comment_length = (size_t)(comment_end + 1 - p);
            memcpy(out, p, comment_length);
            out += comment_length;
            p = comment_end + 1;
            continue;
        }

        if (*p == '\\') {
            const char *run_end = p;
            while (run_end < end && *run_end == '\\') {
                ++run_end;
            }
            size_t slash_count = (size_t)(run_end - p);
            if ((slash_count & 1u) != 0 && run_end < end &&
                (*run_end == 'u' || *run_end == 'U')) {
                int is_upper = (*run_end == 'U');
                int hex_len = is_upper ? 8 : 4;
                size_t remaining = (size_t)(end - run_end);
                if (remaining >= 1u + (size_t)hex_len) {
                    unsigned int codepoint = 0;
                    int valid = 1;
                    for (int offset = 0; offset < hex_len; ++offset) {
                        unsigned char digit = (unsigned char)run_end[1 + offset];
                        if (!is_hex_digit(digit)) {
                            valid = 0;
                            break;
                        }
                        codepoint = (codepoint << 4) | hex_value(digit);
                    }
                    if (valid) {
                        if (codepoint > 0x10FFFFu) {
                            PyMem_Free(buffer);
                            PyErr_Format(
                                PcreError,
                                "Unicode escape \\%c%.*s exceeds 0x10FFFF",
                                *run_end,
                                hex_len,
                                run_end + 1
                            );
                            return NULL;
                        }

                        if (slash_count > 1) {
                            memcpy(out, p, slash_count - 1);
                            out += slash_count - 1;
                        }
                        *out++ = '\\';
                        *out++ = 'x';
                        *out++ = '{';
                        memcpy(out, run_end + 1, (size_t)hex_len);
                        out += hex_len;
                        *out++ = '}';
                        p = run_end + 1 + hex_len;
                        modified = 1;
                        continue;
                    }
                }
            }
            memcpy(out, p, slash_count);
            out += slash_count;
            p = run_end;
            continue;
        }

        *out++ = *p++;
    }

    if (!modified) {
        PyMem_Free(buffer);
        return Py_NewRef(arg);
    }

    Py_ssize_t result_length = out - buffer;
    PyObject *result = PyUnicode_DecodeUTF8(buffer, result_length, "strict");
    PyMem_Free(buffer);
    return result;
}

PyObject *
module_cpu_ascii_vector_mode(PyObject *Py_UNUSED(module), PyObject *Py_UNUSED(args))
{
#if (defined(__x86_64__) && defined(__GNUC__)) || defined(_M_X64) || defined(_M_AMD64)
    return PyLong_FromLong(ascii_vector_mode());
#else
    return PyLong_FromLong(0);
#endif
}
