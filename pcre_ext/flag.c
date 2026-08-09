// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#include "pcre2_module.h"

int
pcre_flag_add_constants(PyObject *module)
{
#define ADD_FLAG(name)                                                   \
    do {                                                                 \
        PyObject *value = PyLong_FromUnsignedLong((unsigned long)(name)); \
        if (value == NULL || PyModule_AddObject(module, #name, value) < 0) { \
            Py_XDECREF(value);                                           \
            return -1;                                                   \
        }                                                                \
    } while (0)

    ADD_FLAG(PCRE2_ANCHORED);
    ADD_FLAG(PCRE2_CASELESS);
    ADD_FLAG(PCRE2_DOTALL);
    ADD_FLAG(PCRE2_EXTENDED);
    ADD_FLAG(PCRE2_LITERAL);
    ADD_FLAG(PCRE2_MULTILINE);
    ADD_FLAG(PCRE2_NO_AUTO_CAPTURE);
    ADD_FLAG(PCRE2_UNGREEDY);
    ADD_FLAG(PCRE2_UTF);
    ADD_FLAG(PCRE2_UCP);

    ADD_FLAG(PCRE2_NOTBOL);
    ADD_FLAG(PCRE2_NOTEOL);
    ADD_FLAG(PCRE2_NOTEMPTY);
    ADD_FLAG(PCRE2_NOTEMPTY_ATSTART);
    ADD_FLAG(PCRE2_PARTIAL_HARD);
    ADD_FLAG(PCRE2_PARTIAL_SOFT);

#undef ADD_FLAG

    return 0;
}
