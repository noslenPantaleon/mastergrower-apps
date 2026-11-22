"""Regenera contract/openapi.json a partir de la API (B6).

Uso: uv run python scripts/export_contract.py

Un test (tests/test_contract.py) falla si el archivo quedó desactualizado.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.contract import OPENAPI_PATH, openapi_text  # noqa: E402

text = openapi_text()
changed = not OPENAPI_PATH.exists() or OPENAPI_PATH.read_text() != text
OPENAPI_PATH.write_text(text)
print(f"{OPENAPI_PATH} {'actualizado' if changed else 'sin cambios'}")
