from __future__ import annotations

import hashlib
import hmac


def token_digest(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def tokens_match(candidate: str, expected: str) -> bool:
    return hmac.compare_digest(candidate.encode(), expected.encode())

