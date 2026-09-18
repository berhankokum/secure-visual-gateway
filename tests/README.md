# Tests

Automated tests for Secure Visual Gateway.

## Test Categories

### Backend

Tests FastAPI endpoints including:

- health endpoint,
- JPEG upload,
- JPEG storage,
- image history,
- latest image,
- duplicate image handling,
- invalid JPEG rejection,
- static JPEG serving,
- telemetry upload,
- latest telemetry,
- invalid telemetry rejection.

### Protocol

Protocol contract tests validate:

- SGP version,
- header size,
- big-endian wire format,
- message type values,
- image packet constants,
- telemetry constants.

These tests protect the documented wire protocol against accidental changes.

## Run Tests

From the repository root:

```powershell
python -m pytest


## 8. `.gitignore` güncelle

Root `.gitignore` dosyasına ekle:

```gitignore
# ============================================================
# Tests
# ============================================================

.pytest_cache/
.test-venv/
.coverage
htmlcov/