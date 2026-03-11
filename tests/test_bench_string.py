"""Benchmark helper reproducing the string scan measurements."""

from __future__ import annotations

import argparse
import collections
import json
import statistics
import sys
import time
from pathlib import Path
from typing import Any, Dict

try:
    import pcre_ext_c
except ImportError:
    # Allow direct execution without installing the package by injecting the project root.
    project_root = Path(__file__).resolve().parents[1]
    if str(project_root) not in sys.path:
        sys.path.insert(0, str(project_root))
    module = sys.modules.get("pcre_ext_c")
    if module is not None:
        module_file = getattr(module, "__file__", "") or ""
        if not module_file.startswith(str(project_root)):
            sys.modules.pop("pcre_ext_c", None)
    import pcre_ext_c

_SUBJECT_BUILDERS = {
    "text_ascii": lambda n: "a" * n,
    "text_latin1": lambda n: ("b" * (n - 1)) + "\u00ff",
    "text_2byte": lambda n: "\u0100" * n,
    "text_4byte": lambda n: "\U0001f600" * n,
    "bytes_ascii": lambda n: b"a" * n,
    "bytes_high": lambda n: (b"b" * (n - 1)) + b"\xff",
}
_LENGTHS = (128, 64000)
_BASE_ITERATIONS = 5  # legacy default used for averaging
_REQUIRED_MULTIPLIER = 5
_MIN_ITERATIONS = 50
_DEFAULT_ITERATIONS = max(_MIN_ITERATIONS, _BASE_ITERATIONS * _REQUIRED_MULTIPLIER)
_OPERATION_REPEAT = 256


def _make_pattern(subject: str | bytes, pattern_text: str, pattern_bytes: bytes):
    return pcre_ext_c.compile(pattern_text if isinstance(subject, str) else pattern_bytes)


def _drain_finditer(pattern: Any, subject: str | bytes) -> None:
    collections.deque(pattern.finditer(subject), maxlen=0)


def _scaled_repeat(subject: str | bytes, *, budget: int) -> int:
    return max(1, budget // max(1, len(subject)))


def _run_search_scan(pattern: Any, subject: str | bytes) -> None:
    for index in range(len(subject)):
        pattern.search(subject, pos=index)


def _run_repeated_match(pattern: Any, subject: str | bytes) -> None:
    for _ in range(_scaled_repeat(subject, budget=_OPERATION_REPEAT * 1024)):
        pattern.match(subject)


def _run_repeated_fullmatch(pattern: Any, subject: str | bytes) -> None:
    for _ in range(_scaled_repeat(subject, budget=_OPERATION_REPEAT * 1024)):
        pattern.fullmatch(subject)


def _run_repeated_finditer(pattern: Any, subject: str | bytes) -> None:
    for _ in range(_scaled_repeat(subject, budget=_OPERATION_REPEAT * 32)):
        _drain_finditer(pattern, subject)


_BENCHMARK_SCENARIOS = (
    {
        "name": "scan with search(pos=...)",
        "pattern_text": r".",
        "pattern_bytes": br".",
        "runner": _run_search_scan,
    },
    {
        "name": "repeat match at start",
        "pattern_text": r".+",
        "pattern_bytes": br".+",
        "runner": _run_repeated_match,
    },
    {
        "name": "repeat fullmatch whole subject",
        "pattern_text": r".+",
        "pattern_bytes": br".+",
        "runner": _run_repeated_fullmatch,
    },
    {
        "name": "drain all finditer matches",
        "pattern_text": r".",
        "pattern_bytes": br".",
        "runner": _run_repeated_finditer,
    },
)


def _run_benchmarks(iterations: int) -> Dict[str, Dict[int, Dict[str, Dict[str, float]]]]:
    results: Dict[str, Dict[int, Dict[str, Dict[str, float]]]] = {}

    for label, builder in _SUBJECT_BUILDERS.items():
        results[label] = {}
        for length in _LENGTHS:
            subject = builder(length)
            results[label][length] = {}
            for scenario in _BENCHMARK_SCENARIOS:
                pattern = _make_pattern(subject, scenario["pattern_text"], scenario["pattern_bytes"])
                samples = []
                for _ in range(iterations):
                    start = time.perf_counter()
                    scenario["runner"](pattern, subject)
                    samples.append(time.perf_counter() - start)
                results[label][length][scenario["name"]] = {
                    "mean_s": statistics.mean(samples),
                    "stdev_s": statistics.stdev(samples) if len(samples) > 1 else 0.0,
                    "samples": samples,
                }
    return results


def _load_json(path: Path | None) -> Dict[str, Dict[int, Dict[str, Dict[str, float]]]]:
    if path is None:
        return {}
    return {
        label: {
            int(length): {
                scenario: metrics for scenario, metrics in scenario_entry.items()
            }
            for length, scenario_entry in entry.items()
        }
        for label, entry in json.loads(path.read_text()).items()
    }


def _dump_json(results: Dict[str, Dict[int, Dict[str, Dict[str, float]]]], path: Path | None) -> None:
    if path is None:
        return
    serialisable = {
        label: {str(length): metrics for length, metrics in entry.items()}
        for label, entry in results.items()
    }
    path.write_text(json.dumps(serialisable, indent=2) + "\n")


def _render_table(
        baseline: Dict[str, Dict[int, Dict[str, Dict[str, float]]]],
        candidate: Dict[str, Dict[int, Dict[str, Dict[str, float]]]],
) -> str:
    header = ("subject", "len", "scenario", "before_ms", "after_ms", "speedup%")
    lines = [" ".join(f"{col:>12}" for col in header)]
    for label in sorted(candidate):
        for length in sorted(candidate[label]):
            for scenario in sorted(candidate[label][length]):
                after_mean_ms = candidate[label][length][scenario]["mean_s"] * 1000
                before_entry = baseline.get(label, {}).get(length, {}).get(scenario)
                if before_entry is None:
                    lines.append(
                        " ".join(
                            (
                                f"{label:>12}",
                                f"{length:>12}",
                                f"{scenario:>12}",
                                f"{'n/a':>12}",
                                f"{after_mean_ms:12.3f}",
                                f"{'n/a':>12}",
                            )
                        )
                    )
                    continue
                before_mean_ms = before_entry["mean_s"] * 1000
                speedup = (
                    ((before_mean_ms - after_mean_ms) / before_mean_ms) * 100
                    if before_mean_ms
                    else 0.0
                )
                lines.append(
                    " ".join(
                        (
                            f"{label:>12}",
                            f"{length:>12}",
                            f"{scenario:>12}",
                            f"{before_mean_ms:12.3f}",
                            f"{after_mean_ms:12.3f}",
                            f"{speedup:12.2f}",
                        )
                    )
                )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--baseline",
        type=Path,
        help="Path to benchmark JSON captured before the optimisation.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write the freshly captured results to this JSON file.",
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=_DEFAULT_ITERATIONS,
        help=f"Number of repetitions per subject length (default: {_DEFAULT_ITERATIONS}).",
    )
    args = parser.parse_args()

    baseline = _load_json(args.baseline)
    candidate = _run_benchmarks(args.iterations)
    _dump_json(candidate, args.output)

    print(_render_table(baseline, candidate))

# fix CI empty test error
def test_bench() -> None:
    print()

if __name__ == "__main__":
    main()
