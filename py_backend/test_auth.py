import json
import os
import tempfile
import unittest
import uuid
from datetime import datetime, timedelta, timezone
from http.cookies import SimpleCookie
from unittest.mock import patch

import jwt
from fastapi import HTTPException
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker
from starlette.requests import Request

import auth
import main
from ebsd_store import Base, UserEbsdTarget

GO_BCRYPT_FIXTURE = "$2a$10$/JSgMcK3vySYB6c8dUkIM.LnXAFJ0FbFR1aiBZp9hDsr5H7ftL61K"


def request(method="GET", body=None, token=None):
    payload = b"" if body is None else json.dumps(body).encode()
    headers = [] if token is None else [(b"cookie", f"{auth.SESSION_COOKIE}={token}".encode())]
    sent = False

    async def receive():
        nonlocal sent
        if sent:
            return {"type": "http.request", "body": b"", "more_body": False}
        sent = True
        return {"type": "http.request", "body": payload, "more_body": False}

    return Request({"type": "http", "method": method, "path": "/", "headers": headers}, receive)


def response_token(response):
    cookie = SimpleCookie()
    cookie.load(response.headers["set-cookie"])
    return cookie[auth.SESSION_COOKIE].value


class AuthMigrationTest(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory()
        cls.engine = create_engine("sqlite:///" + os.path.join(cls.tempdir.name, "auth.db"))
        cls.sessions = sessionmaker(bind=cls.engine, autoflush=False, autocommit=False)
        Base.metadata.create_all(cls.engine)
        cls.session_patch = patch.object(auth, "SessionLocal", cls.sessions)
        cls.env_patch = patch.dict(os.environ, {
            "JWT_SECRET": "test-only-secret-with-sufficient-entropy", "COOKIE_SECURE": "false"
        })
        cls.session_patch.start()
        cls.env_patch.start()

    @classmethod
    def tearDownClass(cls):
        cls.env_patch.stop()
        cls.session_patch.stop()
        cls.engine.dispose()
        cls.tempdir.cleanup()

    def setUp(self):
        with self.sessions() as db:
            db.query(UserEbsdTarget).delete()
            db.query(auth.User).delete()
            db.commit()

    def _insert_go_user(self, username="legacy"):
        user_id = uuid.uuid4()
        with self.sessions() as db:
            db.add(auth.User(id=user_id, username=username, password=GO_BCRYPT_FIXTURE))
            db.commit()
        return user_id

    async def test_existing_go_bcrypt_hash_and_uuid_login_without_data_rewrite(self):
        user_id = self._insert_go_user()
        response = await auth.login(request("POST", {"username": "legacy", "password": "go-password"}))
        self.assertEqual(response.status_code, 200)
        self.assertEqual(json.loads(response.body)["user"]["id"], str(user_id))
        self.assertIn("HttpOnly", response.headers["set-cookie"])
        with self.sessions() as db:
            self.assertEqual(db.get(auth.User, user_id).password, GO_BCRYPT_FIXTURE)

    async def test_register_check_duplicate_logout_and_validation_semantics(self):
        body = {"username": "new_user", "password": "safe-pass"}
        created = await auth.register(request("POST", body))
        self.assertEqual(created.status_code, 201)
        token = response_token(created)
        self.assertEqual(auth.check(request(token=token))["user"]["username"], "new_user")
        duplicate = await auth.register(request("POST", body))
        self.assertEqual(duplicate.status_code, 409)
        self.assertEqual(json.loads(duplicate.body), {"error": "username already exists"})
        too_long = await auth.register(request("POST", {"username": "longpass", "password": "x" * 73}))
        self.assertEqual(too_long.status_code, 400)
        self.assertEqual(json.loads(auth.logout().body), {"msg": "ok"})
        bad_password = await auth.login(request("POST", {"username": "new_user", "password": "wrong"}))
        self.assertEqual(bad_password.status_code, 401)

    def test_tampered_expired_missing_claim_and_deleted_user_sessions_are_rejected(self):
        user_id = self._insert_go_user()
        good = auth.make_session_token(str(user_id))
        now = datetime.now(timezone.utc)
        tokens = [
            good + "tampered",
            jwt.encode({"sub": str(user_id), "iat": now - timedelta(hours=2),
                        "exp": now - timedelta(hours=1)}, os.environ["JWT_SECRET"], algorithm="HS256"),
            jwt.encode({"sub": str(user_id)}, os.environ["JWT_SECRET"], algorithm="HS256"),
        ]
        for token in tokens:
            response = auth.check(request(token=token))
            self.assertEqual(response.status_code, 401)
            self.assertEqual(json.loads(response.body), {"error": "invalid session"})
        with self.sessions() as db:
            db.query(auth.User).delete()
            db.commit()
        self.assertEqual(auth.check(request(token=good)).status_code, 401)

    def test_production_targets_route_filters_associated_user_data(self):
        first_id = self._insert_go_user("first")
        second_id = self._insert_go_user("second")
        with self.sessions() as db:
            db.add_all([
                UserEbsdTarget(user_id=str(first_id), name="first-target"),
                UserEbsdTarget(user_id=str(second_id), name="private-second-target"),
            ])
            db.commit()
        with self.sessions() as db:
            result = main.list_ebsd_targets(
                request(token=auth.make_session_token(str(first_id))), db
            )
        self.assertEqual([item["name"] for item in result], ["first-target"])
        with self.sessions() as db, self.assertRaises(HTTPException) as error:
            main.list_ebsd_targets(request(), db)
        self.assertEqual(error.exception.status_code, 401)


if __name__ == "__main__":
    unittest.main()
