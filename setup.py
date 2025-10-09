# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import shutil
import sys
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext as _build_ext

ROOT_DIR = Path(__file__).resolve().parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from setup_utils import MODULE_SOURCES, RUNTIME_LIBRARY_FILES, collect_build_config


EXTENSION = Extension(
    name="pcre_ext_c",
    sources=MODULE_SOURCES,
    **collect_build_config(),
)


class build_ext(_build_ext):
    def run(self) -> None:
        super().run()
        self._copy_runtime_libraries()

    def _copy_runtime_libraries(self) -> None:
        if not self.extensions:
            return

        destinations = {
            Path(self.get_ext_fullpath(ext.name)).parent for ext in self.extensions
        }

        for runtime_path in RUNTIME_LIBRARY_FILES:
            source = Path(runtime_path)
            if not source.is_file():
                continue
            try:
                source.relative_to(ROOT_DIR)
            except ValueError:
                continue
            for target_dir in destinations:
                target_dir.mkdir(parents=True, exist_ok=True)
                destination = target_dir / source.name
                try:
                    if destination.resolve() == source.resolve():
                        continue
                except FileNotFoundError:
                    pass
                shutil.copy2(source, destination)


setup(ext_modules=[EXTENSION], cmdclass={"build_ext": build_ext})
