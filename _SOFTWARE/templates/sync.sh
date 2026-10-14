#!/bin/sh
# Regenerates enums.json, limits.json and errors.json, copies the descriptors into generated/, then checks all against their schemas
# and the references between them (crosscheck.py).
#   templates/sync.sh            regenerate and validate
#   templates/sync.sh --check    only fail when a generated file is out of date (CI), then validate
# Runs with the Python of the repository's .venv (Windows or POSIX layout), else `python`; PYTHON=<interpreter> overrides.
# The validator needs the jsonschema package in that Python.
set -e
cd "$(dirname "$0")/.."
if [ -n "$PYTHON" ]; then PY="$PYTHON"
elif [ -x .venv/Scripts/python.exe ]; then PY=.venv/Scripts/python.exe
elif [ -x .venv/bin/python ]; then PY=.venv/bin/python
else PY=python
fi

"$PY" templates/generate_enums.py "$@"
"$PY" templates/generate_limits.py "$@"
"$PY" templates/generate_errors.py "$@"
"$PY" templates/generate_bundle.py "$@"
"$PY" templates/validate_schemas.py
"$PY" templates/crosscheck.py
