# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import collections
import os
import re
import threading
import time
import unittest
from statistics import mean, median

import pcre_ext_c

import pcre

try:
    import regex as external_regex
except ImportError:  # pragma: no cover - optional dependency
    external_regex = None

from tabulate import tabulate


RUN_BENCHMARKS = os.getenv("PYPCRE_RUN_BENCHMARKS") == "1"
THREAD_COUNT = int(os.getenv("PYPCRE_BENCH_THREADS", "16"))
SINGLE_ITERATIONS = int(os.getenv("PYPCRE_BENCH_ITERS", "5000"))
THREAD_ITERATIONS = int(os.getenv("PYPCRE_BENCH_THREAD_ITERS", "40"))
COMPILE_ITERATIONS = int(os.getenv("PYPCRE_BENCH_COMPILE_ITERS", "1000"))
TRANSFORM_ITERATIONS = int(os.getenv("PYPCRE_BENCH_TRANSFORM_ITERS", "3000"))


UNICODE_SAMPLE_LENGTH = 128
UNICODE_VARIANT_BASES = {
    "ascii": "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
    "latin-1": "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf",
    "2byte": "\u0100\u0102\u0104\u0106\u0108\u010a\u010c\u010e\u0110\u0112\u0114\u0116\u0118\u011a\u011c\u011e\u0120\u0122\u0124\u0126\u0128\u012a\u012c\u012e\u0130\u0134\u0136\u0139\u013b\u013d\u013f\u0141\u0143\u0145\u0147\u014a",
    "3byte": "\u6f22\u5b57\u4eee\u540d\u4ea4\u932f\u7e41\u9ad4\u5b57\u6d4b\u8bd5\u7de8\u78bc\u8cc7\u6599",
    "4byte": "\U0001f600\U0001f601\U0001f602\U0001f923\U0001f603\U0001f604\U0001f605\U0001f606\U0001f607\U0001f608\U0001f609\U0001f60a\U0001f60b\U0001f60c\U0001f60d\U0001f60e",
}


def _expand_unicode_variants():
    subjects = {}
    for label, base in UNICODE_VARIANT_BASES.items():
        repeats = (UNICODE_SAMPLE_LENGTH // len(base)) + 1
        primary = (base * repeats)[:UNICODE_SAMPLE_LENGTH]
        rotation = min(len(primary), len(base))
        rotated = primary[rotation:] + primary[:rotation]
        mirrored = primary[::-1]
        subjects[label] = [primary, rotated, mirrored]
    return subjects


UNICODE_VARIANT_SUBJECTS = _expand_unicode_variants()


PATTERN_CASES = [
    (r"\bfoo\b", ["foo bar foo", "prefix foo suffix", "no match here"]),
    (r"(?P<word>[A-Za-z]+)", ["Hello world", "Another Line", "lower CASE"]),
    (r"(?:(?<=foo)bar|baz)(?!qux)", ["foobar", "foobaz", "foobazqux"]),
]

BYTE_PATTERN_CASES = [
    (br"foo", [b"foo bar foo", b"prefix foo suffix", b"no match here"]),
    (br"([A-Za-z]+)", [b"Hello world", b"Another Line", b"lower CASE"]),
    (br"\x00[\xff\xfe]+\x01", [b"\x00\xff\xfe\x01", b"\x00\xff\xff\xfe\x01", b"no match"]),
]

COMPILE_PATTERN_TEMPLATES = [
    r"foo{n}",
    r"(?P<word>[A-Za-z]{{1,8}}){n}",
    r"(?:(?<=foo{n})bar|baz{n})(?!qux)",
]

BYTE_COMPILE_PATTERN_TEMPLATES = [
    br"foo{n}",
    br"([A-Za-z]{1,8}){n}",
    br"\x00[\xff\xfe]{1,{n}}\x01",
]

TRANSFORM_CASES = [
    {
        "name": "Text substitute digits",
        "pattern": r"(?P<num>\d+)",
        "subjects": ["item-1 item-22 item-333", "id=404 status=500", "no digits here"],
        "replacement": "#",
        "module_name": "sub",
        "compiled_name": "sub",
        "compiled_args": lambda pattern, subject, replacement: (replacement, subject),
        "module_args": lambda pattern_text, subject, replacement: (pattern_text, replacement, subject),
    },
    {
        "name": "Text split CSV",
        "pattern": r"\s*,\s*",
        "subjects": ["alpha, beta, gamma", "1,2,3,4", "singleton"],
        "replacement": None,
        "module_name": "split",
        "compiled_name": "split",
        "compiled_args": lambda pattern, subject, replacement: (subject,),
        "module_args": lambda pattern_text, subject, replacement: (pattern_text, subject),
    },
]

BYTE_TRANSFORM_CASES = [
    {
        "name": "Bytes substitute digits",
        "pattern": br"(\d+)",
        "subjects": [b"item-1 item-22 item-333", b"id=404 status=500", b"no digits here"],
        "replacement": b"#",
        "module_name": "sub",
        "compiled_name": "sub",
        "compiled_args": lambda pattern, subject, replacement: (replacement, subject),
        "module_args": lambda pattern_text, subject, replacement: (pattern_text, replacement, subject),
    },
    {
        "name": "Bytes split CSV",
        "pattern": br"\s*,\s*",
        "subjects": [b"alpha, beta, gamma", b"1,2,3,4", b"singleton"],
        "replacement": None,
        "module_name": "split",
        "compiled_name": "split",
        "compiled_args": lambda pattern, subject, replacement: (subject,),
        "module_args": lambda pattern_text, subject, replacement: (pattern_text, subject),
    },
]


def _build_compiled_operations(pattern):
    operations = {}
    if hasattr(pattern, "match"):
        method = pattern.match
        operations["match"] = lambda text, method=method: method(text)
    if hasattr(pattern, "search"):
        method = pattern.search
        operations["search"] = lambda text, method=method: method(text)
    if hasattr(pattern, "fullmatch"):
        method = pattern.fullmatch
        operations["fullmatch"] = lambda text, method=method: method(text)
    if hasattr(pattern, "findall"):
        method = pattern.findall
        operations["findall"] = lambda text, method=method: method(text)
    if hasattr(pattern, "finditer"):
        method = pattern.finditer
        operations["finditer"] = lambda text, method=method: collections.deque(method(text), maxlen=0)
    return operations


def _build_module_operations(module):
    operations = {}
    for name in ("match", "search", "fullmatch", "findall", "finditer"):
        func = getattr(module, name, None)
        if func is None:
            continue
        if name == "finditer":
            operations[f"module_{name}"] = lambda pattern, text, func=func: collections.deque(func(pattern, text), maxlen=0)
        else:
            operations[f"module_{name}"] = lambda pattern, text, func=func: func(pattern, text)
    return operations


def _emit_results(title, key_label, results_by_combo, engines, sort_metric):
    if not results_by_combo:
        return
    engine_order = {engine_name: index for index, (engine_name, _, _) in enumerate(engines)}
    print(f"\n{title}:")
    for combo_key in sorted(results_by_combo):
        result_rows = sorted(
            results_by_combo[combo_key],
            key=lambda row: (
                engine_order.get(row["engine"], len(engine_order)),
                row[sort_metric],
            ),
        )
        present_engines = {row["engine"] for row in result_rows}
        for engine_name, _, _ in engines:
            if engine_name not in present_engines:
                result_rows.append(
                    {
                        "engine": engine_name,
                        "calls": "n/a",
                        "total_ms": "n/a",
                    }
                )
        result_rows.sort(
            key=lambda row: (
                engine_order.get(row["engine"], len(engine_order)),
                row[sort_metric] if isinstance(row[sort_metric], (int, float)) else float("inf"),
            )
        )
        best_metric = min(
            (
                row[sort_metric]
                for row in result_rows
                if isinstance(row.get(sort_metric), (int, float))
            ),
            default=None,
        )
        display_rows = []
        for row in result_rows:
            display_row = dict(row)
            display_row["best"] = (
                "*"
                if best_metric is not None and isinstance(row.get(sort_metric), (int, float)) and row[sort_metric] == best_metric
                else ""
            )
            display_rows.append(display_row)
        print(f"\n{key_label}: {combo_key}")
        print(
            tabulate(
                display_rows,
                headers="keys",
                floatfmt=".3f",
                tablefmt="github",
            )
        )
    print(f"\n* best {sort_metric}")


def _format_pattern_label(pattern) -> str:
    return repr(pattern) if isinstance(pattern, (bytes, bytearray)) else str(pattern)


def _describe_operation(op_name: str) -> str:
    if op_name.startswith("module_"):
        return f"module-level {op_name.removeprefix('module_')} call\n"
    if op_name.startswith("compiled_"):
        return f"compiled-pattern {op_name.removeprefix('compiled_')} call\n"
    return f"compiled-pattern {op_name} call\n"


def _describe_pattern_scenario(subject_kind: str, pattern_text, op_name: str) -> str:
    return f"{subject_kind} pattern {_format_pattern_label(pattern_text)} using {_describe_operation(op_name)}"


def _describe_unicode_scenario(variant_label: str, op_name: str) -> str:
    width_label = {
        "ascii": "ASCII text",
        "latin-1": "Latin-1 text",
        "2byte": "2-byte Unicode text",
        "3byte": "3-byte Unicode text",
        "4byte": "4-byte Unicode text",
    }.get(variant_label, variant_label)
    return f"{width_label} using {_describe_operation(op_name)}"


def _describe_compile_scenario(subject_kind: str, template) -> str:
    return f"{subject_kind} compile for template {_format_pattern_label(template)}"


@unittest.skipUnless(RUN_BENCHMARKS, "Set PYPCRE_RUN_BENCHMARKS=1 to enable benchmark tests")
class TestRegexBenchmarks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def _compile_pcre(pattern):
            return pcre.compile(pattern)

        def _compile_pypcre_backend(pattern):
            flags = pcre_ext_c.PCRE2_UTF | pcre_ext_c.PCRE2_UCP if isinstance(pattern, str) else 0
            try:
                return pcre_ext_c.compile(pattern, flags)
            except Exception:
                return None

        def _compile_re(pattern):
            try:
                return re.compile(pattern)
            except re.error:
                return None

        def _compile_regex(pattern):
            if external_regex is None:
                return None
            try:
                return external_regex.compile(pattern)
            except Exception:
                return None

        cls.engines = [
            ("PyPcre", pcre, _compile_pcre),
            ("PyPcre backend", pcre_ext_c, _compile_pypcre_backend),
        ]
        if external_regex is not None:
            cls.engines.append(("regex", external_regex, _compile_regex))
        cls.api_engines = [cls.engines[0]]
        cls.engine_engines = [cls.engines[1]]
        if external_regex is not None:
            cls.api_engines.append(cls.engines[2])
            cls.engine_engines.append(cls.engines[2])
        if min(SINGLE_ITERATIONS, THREAD_ITERATIONS, COMPILE_ITERATIONS, TRANSFORM_ITERATIONS) <= 0:
            raise unittest.SkipTest("Iterations must be positive for meaningful benchmarks")

    def test_single_thread_patterns(self):
        engine_results_by_combo = collections.defaultdict(list)
        api_compiled_results_by_combo = collections.defaultdict(list)
        api_module_results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            module_ops = _build_module_operations(module)
            for pattern_text, subjects in PATTERN_CASES:
                compiled = compile_fn(pattern_text)
                if compiled is None:
                    continue
                compiled_ops = _build_compiled_operations(compiled)
                expected_calls = SINGLE_ITERATIONS * len(subjects)

                for op_name, operation in compiled_ops.items():
                    with self.subTest(engine=engine_name, pattern=pattern_text, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        row = {
                            "engine": engine_name,
                            "calls": expected_calls,
                            "total_ms": elapsed * 1000,
                        }
                        if any(engine_name == name for name, _, _ in self.engine_engines):
                            engine_results_by_combo[(pattern_text, op_name)].append(row)
                        if any(engine_name == name for name, _, _ in self.api_engines):
                            api_compiled_results_by_combo[(pattern_text, op_name)].append(row)

                for op_name, operation in module_ops.items():
                    with self.subTest(engine=engine_name, pattern=pattern_text, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(pattern_text, subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        if any(engine_name == name for name, _, _ in self.api_engines):
                            api_module_results_by_combo[(pattern_text, op_name)].append(
                                {
                                    "engine": engine_name,
                                    "calls": expected_calls,
                                    "total_ms": elapsed * 1000,
                                }
                            )

        if engine_results_by_combo:
            flattened = {
                _describe_pattern_scenario("Text", pattern_text, op_name): rows
                for (pattern_text, op_name), rows in engine_results_by_combo.items()
            }
            _emit_results("Engine benchmark: single-thread text compiled match", "Scenario", flattened, self.engine_engines, "total_ms")
        if api_compiled_results_by_combo:
            flattened = {
                _describe_pattern_scenario("Text", pattern_text, op_name): rows
                for (pattern_text, op_name), rows in api_compiled_results_by_combo.items()
            }
            _emit_results("API benchmark: single-thread text compiled match", "Scenario", flattened, self.api_engines, "total_ms")
        if api_module_results_by_combo:
            flattened = {
                _describe_pattern_scenario("Text", pattern_text, op_name): rows
                for (pattern_text, op_name), rows in api_module_results_by_combo.items()
            }
            _emit_results("API benchmark: single-thread text module-level match", "Scenario", flattened, self.api_engines, "total_ms")

    def test_single_thread_bytes_patterns(self):
        engine_results_by_combo = collections.defaultdict(list)
        api_compiled_results_by_combo = collections.defaultdict(list)
        api_module_results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            module_ops = _build_module_operations(module)
            for pattern_text, subjects in BYTE_PATTERN_CASES:
                compiled = compile_fn(pattern_text)
                if compiled is None:
                    continue
                compiled_ops = _build_compiled_operations(compiled)
                expected_calls = SINGLE_ITERATIONS * len(subjects)

                for op_name, operation in compiled_ops.items():
                    with self.subTest(engine=engine_name, pattern=pattern_text, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        row = {
                            "engine": engine_name,
                            "calls": expected_calls,
                            "total_ms": elapsed * 1000,
                        }
                        if any(engine_name == name for name, _, _ in self.engine_engines):
                            engine_results_by_combo[(pattern_text, op_name)].append(row)
                        if any(engine_name == name for name, _, _ in self.api_engines):
                            api_compiled_results_by_combo[(pattern_text, op_name)].append(row)

                for op_name, operation in module_ops.items():
                    with self.subTest(engine=engine_name, pattern=pattern_text, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(pattern_text, subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        if any(engine_name == name for name, _, _ in self.api_engines):
                            api_module_results_by_combo[(pattern_text, op_name)].append(
                                {
                                    "engine": engine_name,
                                    "calls": expected_calls,
                                    "total_ms": elapsed * 1000,
                                }
                            )

        if engine_results_by_combo:
            flattened = {
                _describe_pattern_scenario("Bytes", pattern_text, op_name): rows
                for (pattern_text, op_name), rows in engine_results_by_combo.items()
            }
            _emit_results("Engine benchmark: single-thread bytes compiled match", "Scenario", flattened, self.engine_engines, "total_ms")
        if api_compiled_results_by_combo:
            flattened = {
                _describe_pattern_scenario("Bytes", pattern_text, op_name): rows
                for (pattern_text, op_name), rows in api_compiled_results_by_combo.items()
            }
            _emit_results("API benchmark: single-thread bytes compiled match", "Scenario", flattened, self.api_engines, "total_ms")
        if api_module_results_by_combo:
            flattened = {
                _describe_pattern_scenario("Bytes", pattern_text, op_name): rows
                for (pattern_text, op_name), rows in api_module_results_by_combo.items()
            }
            _emit_results("API benchmark: single-thread bytes module-level match", "Scenario", flattened, self.api_engines, "total_ms")


    def test_character_width_subjects(self):
        pattern_text = r".+"
        engine_results_by_combo = collections.defaultdict(list)
        api_compiled_results_by_combo = collections.defaultdict(list)
        api_module_results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            module_ops = _build_module_operations(module)
            compiled = compile_fn(pattern_text)
            if compiled is None:
                continue
            compiled_ops = _build_compiled_operations(compiled)
            for variant_label, subjects in UNICODE_VARIANT_SUBJECTS.items():
                expected_calls = SINGLE_ITERATIONS * len(subjects)
                for op_name, operation in compiled_ops.items():
                    with self.subTest(engine=engine_name, variant=variant_label, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        row = {
                            "engine": engine_name,
                            "calls": expected_calls,
                            "total_ms": elapsed * 1000,
                        }
                        if any(engine_name == name for name, _, _ in self.engine_engines):
                            engine_results_by_combo[(variant_label, op_name)].append(row)
                        if any(engine_name == name for name, _, _ in self.api_engines):
                            api_compiled_results_by_combo[(variant_label, op_name)].append(row)
                for op_name, operation in module_ops.items():
                    with self.subTest(engine=engine_name, variant=variant_label, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(pattern_text, subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        if any(engine_name == name for name, _, _ in self.api_engines):
                            api_module_results_by_combo[(variant_label, op_name)].append(
                                {
                                    "engine": engine_name,
                                    "calls": expected_calls,
                                    "total_ms": elapsed * 1000,
                                }
                            )
        if engine_results_by_combo:
            flattened = {
                _describe_unicode_scenario(variant_label, op_name): rows
                for (variant_label, op_name), rows in engine_results_by_combo.items()
            }
            _emit_results("Engine benchmark: Unicode width sensitivity", "Scenario", flattened, self.engine_engines, "total_ms")
        if api_compiled_results_by_combo:
            flattened = {
                _describe_unicode_scenario(variant_label, op_name): rows
                for (variant_label, op_name), rows in api_compiled_results_by_combo.items()
            }
            _emit_results("API benchmark: Unicode width compiled match", "Scenario", flattened, self.api_engines, "total_ms")
        if api_module_results_by_combo:
            flattened = {
                _describe_unicode_scenario(variant_label, op_name): rows
                for (variant_label, op_name), rows in api_module_results_by_combo.items()
            }
            _emit_results("API benchmark: Unicode width module-level match", "Scenario", flattened, self.api_engines, "total_ms")

    def test_compile_patterns(self):
        engine_results_by_combo = collections.defaultdict(list)
        api_results_by_combo = collections.defaultdict(list)
        for engine_name, _, compile_fn in self.engines:
            for template in COMPILE_PATTERN_TEMPLATES:
                with self.subTest(engine=engine_name, template=template):
                    patterns = [template.format(n=index) for index in range(COMPILE_ITERATIONS)]
                    compiled_count = 0
                    start = time.perf_counter()
                    for pattern_text in patterns:
                        compiled = compile_fn(pattern_text)
                        if compiled is not None:
                            compiled_count += 1
                    elapsed = time.perf_counter() - start
                    self.assertEqual(compiled_count, COMPILE_ITERATIONS)
                    self.assertGreaterEqual(elapsed, 0.0)
                    row = {
                        "engine": engine_name,
                        "calls": COMPILE_ITERATIONS,
                        "total_ms": elapsed * 1000,
                    }
                    if any(engine_name == name for name, _, _ in self.engine_engines):
                        engine_results_by_combo[template].append(row)
                    if any(engine_name == name for name, _, _ in self.api_engines):
                        api_results_by_combo[template].append(row)

        flattened = {
            _describe_compile_scenario("Text pattern", template): rows
            for template, rows in engine_results_by_combo.items()
        }
        _emit_results("Engine benchmark: text pattern compile", "Scenario", flattened, self.engine_engines, "total_ms")
        flattened = {
            _describe_compile_scenario("Text pattern", template): rows
            for template, rows in api_results_by_combo.items()
        }
        _emit_results("API benchmark: text pattern compile", "Scenario", flattened, self.api_engines, "total_ms")

    def test_compile_bytes_patterns(self):
        engine_results_by_combo = collections.defaultdict(list)
        api_results_by_combo = collections.defaultdict(list)
        for engine_name, _, compile_fn in self.engines:
            for template in BYTE_COMPILE_PATTERN_TEMPLATES:
                with self.subTest(engine=engine_name, template=template):
                    patterns = [
                        template.replace(b"{n}", str(index + 1).encode("ascii"))
                        for index in range(COMPILE_ITERATIONS)
                    ]
                    compiled_count = 0
                    start = time.perf_counter()
                    for pattern_text in patterns:
                        compiled = compile_fn(pattern_text)
                        if compiled is not None:
                            compiled_count += 1
                    elapsed = time.perf_counter() - start
                    self.assertEqual(compiled_count, COMPILE_ITERATIONS)
                    self.assertGreaterEqual(elapsed, 0.0)
                    row = {
                        "engine": engine_name,
                        "calls": COMPILE_ITERATIONS,
                        "total_ms": elapsed * 1000,
                    }
                    if any(engine_name == name for name, _, _ in self.engine_engines):
                        engine_results_by_combo[template].append(row)
                    if any(engine_name == name for name, _, _ in self.api_engines):
                        api_results_by_combo[template].append(row)

        flattened = {
            _describe_compile_scenario("Bytes pattern", template): rows
            for template, rows in engine_results_by_combo.items()
        }
        _emit_results("Engine benchmark: bytes pattern compile", "Scenario", flattened, self.engine_engines, "total_ms")
        flattened = {
            _describe_compile_scenario("Bytes pattern", template): rows
            for template, rows in api_results_by_combo.items()
        }
        _emit_results("API benchmark: bytes pattern compile", "Scenario", flattened, self.api_engines, "total_ms")

    def test_transform_patterns(self):
        results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            for case in TRANSFORM_CASES:
                compiled = compile_fn(case["pattern"])
                compiled_method = getattr(compiled, case["compiled_name"], None) if compiled is not None else None
                module_method = getattr(module, case["module_name"], None)
                expected_calls = TRANSFORM_ITERATIONS * len(case["subjects"])

                if compiled_method is not None:
                    with self.subTest(engine=engine_name, case=case["name"], mode="compiled"):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(TRANSFORM_ITERATIONS):
                            for subject in case["subjects"]:
                                compiled_method(*case["compiled_args"](compiled, subject, case["replacement"]))
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(case["name"], f"compiled_{case['compiled_name']}")].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                            }
                        )

                if module_method is not None:
                    with self.subTest(engine=engine_name, case=case["name"], mode="module"):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(TRANSFORM_ITERATIONS):
                            for subject in case["subjects"]:
                                module_method(*case["module_args"](case["pattern"], subject, case["replacement"]))
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(case["name"], f"module_{case['module_name']}")].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                            }
                        )

        flattened = {
            f"{case_name} | {_describe_operation(op_name)}": rows
            for (case_name, op_name), rows in results_by_combo.items()
        }
        _emit_results("API benchmark: text transform", "Scenario", flattened, self.api_engines, "total_ms")

    def test_transform_bytes_patterns(self):
        results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            for case in BYTE_TRANSFORM_CASES:
                compiled = compile_fn(case["pattern"])
                compiled_method = getattr(compiled, case["compiled_name"], None) if compiled is not None else None
                module_method = getattr(module, case["module_name"], None)
                expected_calls = TRANSFORM_ITERATIONS * len(case["subjects"])

                if compiled_method is not None:
                    with self.subTest(engine=engine_name, case=case["name"], mode="compiled"):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(TRANSFORM_ITERATIONS):
                            for subject in case["subjects"]:
                                compiled_method(*case["compiled_args"](compiled, subject, case["replacement"]))
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(case["name"], f"compiled_{case['compiled_name']}")].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                            }
                        )

                if module_method is not None:
                    with self.subTest(engine=engine_name, case=case["name"], mode="module"):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(TRANSFORM_ITERATIONS):
                            for subject in case["subjects"]:
                                module_method(*case["module_args"](case["pattern"], subject, case["replacement"]))
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(case["name"], f"module_{case['module_name']}")].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                            }
                        )

        flattened = {
            f"{case_name} | {_describe_operation(op_name)}": rows
            for (case_name, op_name), rows in results_by_combo.items()
        }
        _emit_results("API benchmark: bytes transform", "Scenario", flattened, self.api_engines, "total_ms")

    def test_multi_threaded_match(self):
        pattern_text, subjects = PATTERN_CASES[0]
        engine_results_by_combo = collections.defaultdict(list)
        api_results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            compiled = compile_fn(pattern_text)
            compiled_ops = _build_compiled_operations(compiled)
            if "search" in compiled_ops:
                op_name = "search"
            elif "match" in compiled_ops:
                op_name = "match"
            else:
                self.skipTest(f"{engine_name} does not provide match or search for multi-thread benchmark")
            operation = compiled_ops[op_name]
            subjects_cycle = subjects

            with self.subTest(engine=engine_name, operation=op_name):
                thread_times, total_elapsed = self._run_thread_benchmark(operation, subjects_cycle)
                self.assertEqual(len(thread_times), THREAD_COUNT)
                for duration in thread_times:
                    self.assertGreaterEqual(duration, 0.0)
                row = {
                    "engine": engine_name,
                    "threads": THREAD_COUNT,
                    "min_ms": min(thread_times) * 1000,
                    "median_ms": median(thread_times) * 1000,
                    "max_ms": max(thread_times) * 1000,
                    "mean_ms": mean(thread_times) * 1000,
                    "total_ms": total_elapsed * 1000,
                }
                if any(engine_name == name for name, _, _ in self.engine_engines):
                    engine_results_by_combo[(pattern_text, op_name)].append(row)
                if any(engine_name == name for name, _, _ in self.api_engines):
                    api_results_by_combo[(pattern_text, op_name)].append(row)

        self._emit_thread_results("Engine benchmark: multi-thread text search", engine_results_by_combo, self.engine_engines)
        self._emit_thread_results("API benchmark: multi-thread text search", api_results_by_combo, self.api_engines)

    def _emit_thread_results(self, title, results_by_combo, engines):
        if not results_by_combo:
            return
        engine_order = {engine_name: index for index, (engine_name, _, _) in enumerate(engines)}
        print(f"\n{title}:")
        for (pattern_text, op_name) in sorted(results_by_combo):
            result_rows = sorted(
                results_by_combo[(pattern_text, op_name)],
                key=lambda row: (
                    engine_order.get(row["engine"], len(engine_order)),
                    row["mean_ms"],
                ),
            )
            present_engines = {row["engine"] for row in result_rows}
            for engine_name, _, _ in engines:
                if engine_name not in present_engines:
                    result_rows.append(
                        {
                            "engine": engine_name,
                            "threads": "n/a",
                            "min_ms": "n/a",
                            "median_ms": "n/a",
                            "max_ms": "n/a",
                            "mean_ms": "n/a",
                            "total_ms": "n/a",
                        }
                    )
            result_rows.sort(
                key=lambda row: (
                    engine_order.get(row["engine"], len(engine_order)),
                    row["mean_ms"] if isinstance(row["mean_ms"], (int, float)) else float("inf"),
                )
            )
            best_metric = min(
                (
                    row["total_ms"]
                    for row in result_rows
                    if isinstance(row.get("total_ms"), (int, float))
                ),
                default=None,
            )
            display_rows = []
            for row in result_rows:
                display_row = dict(row)
                display_row["best"] = (
                    "*"
                    if best_metric is not None and isinstance(row.get("total_ms"), (int, float)) and row["total_ms"] == best_metric
                    else ""
                )
                display_rows.append(display_row)
            print(f"\nScenario: {_describe_pattern_scenario('Text', pattern_text, op_name)}")
            print(
                tabulate(
                    display_rows,
                    headers="keys",
                    floatfmt=".3f",
                    tablefmt="github",
                )
            )
        print("\n* best total_ms")

    def _run_thread_benchmark(self, operation, subjects):
        start_barrier = threading.Barrier(THREAD_COUNT + 1)
        finish_barrier = threading.Barrier(THREAD_COUNT + 1)
        durations = [0.0] * THREAD_COUNT

        def worker(index: int):
            # Ensure threads are ready before timing
            start_barrier.wait()
            start_time = time.perf_counter()
            for _ in range(THREAD_ITERATIONS):
                for subject in subjects:
                    operation(subject)
            durations[index] = time.perf_counter() - start_time
            finish_barrier.wait()

        threads = [threading.Thread(target=worker, args=(idx,)) for idx in range(THREAD_COUNT)]
        for thread in threads:
            thread.start()

        start_barrier.wait()  # Wait for all workers to report ready
        global_start = time.perf_counter()
        finish_barrier.wait()  # Wait for all workers to finish work
        total_elapsed = time.perf_counter() - global_start

        for thread in threads:
            thread.join()

        self.assertGreaterEqual(total_elapsed, 0.0)
        return durations, total_elapsed


if __name__ == "__main__":
    unittest.main()
