import base64
import hashlib
import hmac
import json
import os
import time
from typing import Any, Dict, Optional


JWT_SECRET = os.getenv("JWT_SECRET", "ebsd-dev-secret")
JWT_EXPIRES_IN = int(os.getenv("JWT_EXPIRES_IN", "86400"))
AUTH_USERNAME = os.getenv("AUTH_USERNAME", "admin")
AUTH_PASSWORD = os.getenv("AUTH_PASSWORD", "admin1234")


def _b64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def _b64url_decode(data: str) -> bytes:
    padding = "=" * (-len(data) % 4)
    return base64.urlsafe_b64decode(data + padding)


def create_token(subject: str) -> str:
    now = int(time.time())
    header = {"alg": "HS256", "typ": "JWT"}
    payload = {
        "sub": subject,
        "iat": now,
        "exp": now + JWT_EXPIRES_IN,
    }
    header_part = _b64url_encode(json.dumps(header, separators=(",", ":")).encode("utf-8"))
    payload_part = _b64url_encode(json.dumps(payload, separators=(",", ":")).encode("utf-8"))
    signing_input = f"{header_part}.{payload_part}"
    signature = hmac.new(
        JWT_SECRET.encode("utf-8"),
        signing_input.encode("utf-8"),
        hashlib.sha256,
    ).digest()
    return f"{signing_input}.{_b64url_encode(signature)}"


def verify_token(token: str) -> Dict[str, Any]:
    parts = token.split(".")
    if len(parts) != 3:
        raise ValueError("Invalid token format")

    header_part, payload_part, signature_part = parts
    signing_input = f"{header_part}.{payload_part}"
    expected_signature = hmac.new(
        JWT_SECRET.encode("utf-8"),
        signing_input.encode("utf-8"),
        hashlib.sha256,
    ).digest()

    try:
        actual_signature = _b64url_decode(signature_part)
    except Exception as exc:
        raise ValueError("Invalid token signature") from exc

    if not hmac.compare_digest(actual_signature, expected_signature):
        raise ValueError("Invalid token signature")

    try:
        payload = json.loads(_b64url_decode(payload_part))
    except Exception as exc:
        raise ValueError("Invalid token payload") from exc

    exp = payload.get("exp")
    if not isinstance(exp, int):
        raise ValueError("Invalid token expiration")
    if exp < int(time.time()):
        raise ValueError("Token has expired")

    return payload


def parse_bearer_token(authorization: Optional[str]) -> str:
    if not authorization:
        raise ValueError("Missing Authorization header")

    scheme, _, token = authorization.partition(" ")
    if scheme.lower() != "bearer" or not token:
        raise ValueError("Invalid Authorization header")

    return token
