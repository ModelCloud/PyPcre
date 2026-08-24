"""Compatibility helpers for private stdlib :mod:`re` internals."""

from __future__ import annotations

import re as _std_re
import warnings


def _load_parser():
    parser = getattr(_std_re, "_parser", None)
    if parser is not None:
        return parser

    # Python <= 3.10 hides the parser behind the deprecated top-level
    # ``sre_parse`` module; Python 3.15 removes that alias entirely.
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)
            import sre_parse as parser
    except ModuleNotFoundError as exc:  # pragma: no cover - Python >= 3.15
        raise ImportError(
            "stdlib re internals unavailable: expected re._parser "
            "(Python 3.11+) or sre_parse (Python <= 3.10); this interpreter "
            "provides neither"
        ) from exc

    return parser


_parser = _load_parser()

RE_TEMPLATE = getattr(_std_re, "TEMPLATE", 0)
RE_TEMPLATE_FLAG: int = int(getattr(_std_re, "TEMPLATE", 0))
RE_UNICODE_FLAG: int = int(getattr(_std_re, "UNICODE", 0))


__all__ = ["_parser", "RE_TEMPLATE", "RE_TEMPLATE_FLAG", "RE_UNICODE_FLAG"]
