// SPDX-FileCopyrightText: 2025 ModelCloud.ai
// SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
// SPDX-License-Identifier: Apache-2.0
// Contact: qubitium@modelcloud.ai, x.com/qubitium

#ifndef ATOMIC_COMPAT_H
#define ATOMIC_COMPAT_H

#include <stdatomic.h>

#define ATOMIC_COMPAT_HAVE_ATOMICS 1
#define ATOMIC_VAR(type) _Atomic(type)

#endif /* ATOMIC_COMPAT_H */
