#!/usr/bin/env python3
"""
Chaba secret scanner — zero dependencies, runs anywhere with Python 3.

Scans a tree for credential-shaped strings that must never appear in
committed source, docs or generated output. Used by CI (see
.github/workflows/security-scan.yml) and locally:

    python3 scripts/secret_scan.py .          # scan the workspace
    python3 scripts/secret_scan.py <path>     # scan one file/dir

Design notes:
  - Contextual on purpose: a bare 43-char base64url run matches test
    identifiers and base64 image blobs, so the scanner only fires on
    key-SHAPED values in credential CONTEXT (assigned to a
    key/token/secret/password/error-named variable, after "Bearer ",
    inside hash_api_key(...), or with a known prefix like whsec_).
  - Matches inside backend/tests/ are reported as WARNINGS (test
    fixtures are committed by design there); use --strict to treat
    them as failures too.
  - The inert docs placeholder (43 'x's) never fires.
  - If a committed value is a deliberate non-secret fixture outside
    backend/tests/, add it to ALLOWED_VALUES with a comment.

Exit 0 on a clean scan, 1 when anything suspected-secret matches.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SKIP_DIRS = {
    ".git", "node_modules", ".venv", "dist", "build", "__pycache__",
    ".pytest_cache", ".idea", ".vite", "gui-test-screenshots",
}
SKIP_SUFFIXES = {
    ".png", ".jpg", ".jpeg", ".gif", ".webp", ".ico", ".woff", ".woff2",
    ".ttf", ".gz", ".tgz", ".tar", ".zip", ".pdf", ".wasm", ".db", ".sqlite",
    ".pem", ".key",
}

# The inert placeholder used in the public docs (43 x's) — never a secret.
_INERT = "x" * 43

_VAR = r"[A-Za-z_][A-Za-z0-9_]{0,39}"
_VAL43 = r"[A-Za-z0-9_\-]{43}=?"

# A deliberately-non-secret fixture committed outside backend/tests/
# (add ONLY values you know for certain are fake, with a comment).
ALLOWED_VALUES: set[str] = {
    _INERT,
}

PATTERNS: list[tuple[str, re.Pattern]] = [
    # key-shaped variable assignment: token = "...43..."  /  "var": "43"
    (
        "key_var",
        re.compile(
            rf"(?:{_VAR})\s*[=:]\s*['\"]?({_VAL43}|whsec_[A-Za-z0-9_\-]{{20,}})"
        ),
    ),
    # "Bearer <43 chars>"
    ("bearer_value", re.compile(rf"Bearer\s+({_VAL43})")),
    # hash_api_key("...") — the test-fixture idiom
    ("hash_api_key_call", re.compile(rf"hash_api_key\(\s*['\"]?({_VAL43})")),
    # the PWA/magic-link code shape (pwa_ + 32 url-safe chars)
    ("pwa_code", re.compile(r"pwa_[A-Za-z0-9_\-]{32}")),
    # env-var style: NAME=value
    (
        "env_value",
        re.compile(
            rf"({_VAR})\s*=\s*(?:'|\")?({_VAL43}|whsec_[A-Za-z0-9_\-]{{20,}})"
        ),
    ),
    ("private_key_block", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |PGP )?PRIVATE KEY-----")),
    ("aws_access_key", re.compile(r"AKIA[0-9A-Z]{16}")),
    ("github_token", re.compile(r"gh[pousr]_[A-Za-z0-9]{20,}")),
    ("stripe_live", re.compile(r"(?:sk|pk)_live_[A-Za-z0-9]{20,}")),
    (
        "vapid_private",
        re.compile(r"PUSH_VAPID_PRIVATE_KEY\s*=\s*[A-Za-z0-9_\-]{40,}"),
    ),
]

# Literals that were removed from the public docs (red-team M4) and
# must never reappear.
KNOWN_LEAKED: list[str] = [
    "C1lwkubJ2LvLb2kwQncJ1twukgxSYgl7jaW9tENr2ow",
]


def _is_binary(path: Path) -> bool:
    if path.suffix.lower() in SKIP_SUFFIXES:
        return True
    try:
        with path.open("rb") as fh:
            chunk = fh.read(8192)
    except OSError:
        return True
    return b"\x00" in chunk


# 43-char base64url runs are ambiguous (identifier words, yaml
# operationIds, html id=... all look the same) — a REAL key is almost
# always mixed-case with digits mixed in, so require that shape.
_CHAR43_NAMES = {"key_var", "bearer_value", "env_value", "hash_api_key_call"}


def _looks_like_key(value: str) -> bool:
    upper = sum(c.isupper() for c in value)
    digits = sum(c.isdigit() for c in value)
    return upper >= 2 and digits >= 2


def scan_file(path: Path, findings: list[str], warnings: list[str]) -> None:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except (OSError, UnicodeDecodeError):
        return
    for name, rx in PATTERNS:
        for match in rx.finditer(text):
            value = match.group(match.lastindex or 0)
            if value in ALLOWED_VALUES:
                continue
            if value in KNOWN_LEAKED:
                line = text.count("\n", 0, match.start()) + 1
                findings.append(f"{path}:{line}:{name}:KNOWN_LEAKED_LITERAL")
                continue
            if name in _CHAR43_NAMES and not _looks_like_key(value):
                continue  # identifier word / yaml id / html id — not a key
            line = text.count("\n", 0, match.start()) + 1
            item = f"{path}:{line}:{name}:{value[:16]}…"
            if "tests" in path.parts and path.parts[-3:-1] == ("backend", "tests"):
                warnings.append(item)
            else:
                findings.append(item)


def scan_tree(root: Path) -> tuple[list[str], list[str]]:
    findings: list[str] = []
    warnings: list[str] = []
    files = [root] if root.is_file() else [p for p in root.rglob("*") if p.is_file()]
    for path in files:
        if path.resolve() == Path(__file__).resolve():
            continue  # this script carries the known-leaked literal on purpose
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        if _is_binary(path):
            continue
        scan_file(path, findings, warnings)
    return findings, warnings


def main(argv: list[str]) -> int:
    roots = [Path(a) for a in argv] or [Path(".")]
    strict = "--strict" in argv
    roots = [r for r in roots if r.name != "--strict"]
    findings: list[str] = []
    warnings: list[str] = []
    for root in roots:
        f, w = scan_tree(root)
        findings.extend(f)
        warnings.extend(w)
    if warnings:
        print(f"secret scan WARNINGS ({len(warnings)}, test-fixture context — review):")
        for w in warnings:
            print(f"  - {w}")
    if findings:
        print(f"secret scan FAILED: {len(findings)} potential secret(s) found:")
        for f in findings:
            print(f"  - {f}")
        return 1
    if strict and warnings:
        print("secret scan: FAILED (--strict, warnings treated as failures)")
        return 1
    print("secret scan: clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))