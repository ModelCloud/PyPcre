#!/bin/bash

cd "$(dirname "$0")" || exit

# force ruff/isort to be same version as setup.py
pip install -U ruff==0.13.0

ruff check ../pcre ../tests ../setup.py --fix --unsafe-fixes
ruff_status=$?

# Exit with the status code of ruff check
exit $ruff_status