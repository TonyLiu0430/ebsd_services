import os
import re
import uuid
import asyncio
from functools import partial
from datetime import datetime, timedelta, timezone
from typing import Any, Dict, Optional

import bcrypt
import jwt
from fastapi import HTTPException, Request, Response
from fastapi.responses import JSONResponse
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError
from app.db.store import SessionLocal, User


SESSION_COOKIE = "ginSession"
SESSION_LIFETIME = timedelta(hours=48)
USERNAME_RE = re.compile(r"^[a-zA-Z0-9_]+$")
PASSWORD_RE = re.compile(r"^\S+$")


def _secret() -> str:
    secret = os.getenv("JWT_SECRET", "")
    if not secret:
        raise RuntimeError("JWT_SECRET isn't set")
    return secret


def valid_credentials(username: Any, password: Any) -> bool:
    if not isinstance(username, str) or not isinstance(password, str):
        return False
    try:
        username_size = len(username.encode("utf-8"))
        password_size = len(password.encode("utf-8"))
    except UnicodeError:
        return False
    return (
        3 <= username_size <= 30
        and 4 <= password_size <= 72
        and bool(USERNAME_RE.fullmatch(username))
        and bool(PASSWORD_RE.fullmatch(password))
    )


def make_session_token(user_id: str, now: Optional[datetime] = None) -> str:
    issued_at = now or datetime.now(timezone.utc)
    return jwt.encode(
        {"sub": user_id, "iat": issued_at, "exp": issued_at + SESSION_LIFETIME},
        _secret(),
        algorithm="HS256",
    )


def validate_auth_config() -> None:
    _secret()


def _cookie_secure() -> bool:
    return os.getenv("COOKIE_SECURE", "false").lower() in {"1", "true", "yes"}


def _set_session_cookie(response: Response, token: str) -> None:
    response.set_cookie(
        SESSION_COOKIE, token,
        httponly=True, secure=_cookie_secure(), samesite="lax", path="/",
    )


def _clear_session_cookie(response: Response) -> None:
    response.delete_cookie(
        SESSION_COOKIE, httponly=True, secure=_cookie_secure(),
        samesite="lax", path="/",
    )


def current_user(request: Request) -> Dict[str, str]:
    token = request.cookies.get(SESSION_COOKIE)
    if not token:
        raise HTTPException(status_code=401, detail="not logged in")
    try:
        claims = jwt.decode(
            token, _secret(), algorithms=["HS256"],
            options={"require": ["sub", "iat", "exp"]},
        )
        user_id = uuid.UUID(claims["sub"])
    except (jwt.PyJWTError, ValueError, TypeError, KeyError):
        raise HTTPException(status_code=401, detail="invalid session")

    with SessionLocal() as db:
        found = db.get(User, user_id)
        if found is None:
            raise HTTPException(status_code=401, detail="invalid session")
        return {"id": str(found.id), "username": found.username}


async def _credentials(request: Request):
    try:
        body = await request.json()
    except Exception:
        return None, JSONResponse({"error": "invalid request body"}, status_code=400)
    if not isinstance(body, dict):
        return None, JSONResponse({"error": "invalid request body"}, status_code=400)
    return (body.get("username"), body.get("password")), None


async def _run_blocking(function, *args):
    return await asyncio.get_running_loop().run_in_executor(None, partial(function, *args))


def check(request: Request):
    try:
        user = current_user(request)
    except HTTPException as exc:
        response = JSONResponse({"error": exc.detail}, status_code=exc.status_code)
        if exc.detail == "invalid session":
            _clear_session_cookie(response)
        return response
    return {"user": user}


def logout():
    response = JSONResponse({"msg": "ok"})
    _clear_session_cookie(response)
    return response


async def register(request: Request):
    credentials, error = await _credentials(request)
    if error:
        return error
    username, password = credentials
    if username == "" or password == "":
        return JSONResponse(
            {"error": "username and password cannot be empty"}, status_code=400
        )
    if not valid_credentials(username, password):
        return JSONResponse({"error": "invalid username or password"}, status_code=400)
    try:
        password_hash = await _run_blocking(
            bcrypt.hashpw, password.encode("utf-8"), bcrypt.gensalt(rounds=10)
        )
        password_hash = password_hash.decode("ascii")
    except (ValueError, TypeError):
        return JSONResponse({"error": "failed to hash password"}, status_code=500)

    return await _run_blocking(_create_user, username, password_hash)


def _create_user(username: str, password_hash: str):
    with SessionLocal() as db:
        created = User(username=username, password=password_hash)
        db.add(created)
        try:
            db.commit()
            db.refresh(created)
        except IntegrityError:
            db.rollback()
            return JSONResponse({"error": "username already exists"}, status_code=409)
        token = make_session_token(str(created.id))
        payload = {"user": {"id": str(created.id), "username": created.username}}
    response = JSONResponse(payload, status_code=201)
    _set_session_cookie(response, token)
    return response


async def login(request: Request):
    credentials, error = await _credentials(request)
    if error:
        return error
    username, password = credentials
    if not valid_credentials(username, password):
        return JSONResponse({"error": "invalid username or password"}, status_code=400)
    return await _run_blocking(_login_user, username, password)


def _login_user(username: str, password: str):
    with SessionLocal() as db:
        found = db.execute(select(User).where(User.username == username)).scalar_one_or_none()
        valid = found is not None
        if valid:
            try:
                valid = bcrypt.checkpw(
                    password.encode("utf-8"), found.password.encode("ascii")
                )
            except (ValueError, TypeError, UnicodeError):
                valid = False
        if not valid:
            return JSONResponse({"error": "invalid username or password"}, status_code=401)
        token = make_session_token(str(found.id))
        payload = {"user": {"id": str(found.id), "username": found.username}}
    response = JSONResponse(payload)
    _set_session_cookie(response, token)
    return response
