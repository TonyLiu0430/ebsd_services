import hashlib
import json
import os
import uuid
import urllib.error
import urllib.request
from datetime import datetime, timezone
from typing import Any, Dict, Generator, Optional, Tuple

from fastapi import HTTPException, Request, UploadFile
from sqlalchemy import (
    Column,
    DateTime,
    ForeignKey,
    Index,
    Integer,
    LargeBinary,
    String,
    UniqueConstraint,
    create_engine,
    func,
    select,
)
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session, declarative_base, deferred, relationship, sessionmaker


DATABASE_URL = os.getenv("DATABASE_URL")
GO_BACKEND_URL = os.getenv("GO_BACKEND_URL", "http://go_backend:8080").rstrip("/")

if not DATABASE_URL:
    libpq_dsn = os.getenv("DATABASE_CONNECTION_STRING", "")
    parts = dict(item.split("=", 1) for item in libpq_dsn.split() if "=" in item)
    if parts:
        DATABASE_URL = (
            f"postgresql+psycopg2://{parts.get('user', '')}:{parts.get('password', '')}"
            f"@{parts.get('host', 'localhost')}:{parts.get('port', '5432')}/{parts.get('dbname', '')}"
        )

if not DATABASE_URL:
    DATABASE_URL = "sqlite:///./ebsd_store.db"

engine = create_engine(DATABASE_URL, pool_pre_ping=True)
SessionLocal = sessionmaker(bind=engine, autoflush=False, autocommit=False)
Base = declarative_base()


class EbsdFile(Base):
    __tablename__ = "ebsd_files"

    id = Column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    content_hash = Column(String(64), nullable=False, index=True)
    extension = Column(String(8), nullable=False)
    original_filename = Column(String(255), nullable=False)
    size_bytes = Column(Integer, nullable=False)
    content = deferred(Column(LargeBinary, nullable=False))
    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)

    __table_args__ = (
        UniqueConstraint("content_hash", "extension", name="uq_ebsd_files_hash_extension"),
    )


class EbsdPair(Base):
    __tablename__ = "ebsd_pairs"

    id = Column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    pair_hash = Column(String(64), nullable=False, unique=True, index=True)
    crc_file_id = Column(String(36), ForeignKey("ebsd_files.id"), nullable=False)
    cpr_file_id = Column(String(36), ForeignKey("ebsd_files.id"), nullable=False)
    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)

    crc_file = relationship("EbsdFile", foreign_keys=[crc_file_id])
    cpr_file = relationship("EbsdFile", foreign_keys=[cpr_file_id])

    __table_args__ = (
        UniqueConstraint("crc_file_id", "cpr_file_id", name="uq_ebsd_pairs_crc_cpr"),
    )


class UserEbsdTarget(Base):
    __tablename__ = "user_ebsd_targets"

    id = Column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    user_id = Column(String(36), nullable=False)
    name = Column(String(255), nullable=False)
    updated_at = Column(
        DateTime(timezone=True),
        server_default=func.now(),
        onupdate=func.now(),
        nullable=False,
    )
    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)

    __table_args__ = (
        UniqueConstraint("user_id", "name", name="uq_user_ebsd_targets_user_name"),
        Index("ix_user_ebsd_targets_user", "user_id"),
    )


class UserEbsdPair(Base):
    __tablename__ = "user_ebsd_pairs"

    id = Column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    user_id = Column(String(36), nullable=False)
    pair_id = Column(String(36), ForeignKey("ebsd_pairs.id"), nullable=False)
    display_name = Column(String(255), nullable=False)
    sample = Column(String(255), nullable=False)
    position = Column(String(32), nullable=False)
    folder = Column(String(255), nullable=True)
    version_key = Column(String(64), nullable=True)
    version_label = Column(String(255), nullable=True)
    version_num = Column(Integer, nullable=True)
    updated_at = Column(
        DateTime(timezone=True),
        server_default=func.now(),
        onupdate=func.now(),
        nullable=False,
    )
    created_at = Column(DateTime(timezone=True), server_default=func.now(), nullable=False)

    pair = relationship("EbsdPair")

    __table_args__ = (
        UniqueConstraint("user_id", "pair_id", name="uq_user_ebsd_pairs_user_pair"),
        Index("ix_user_ebsd_pairs_user_sample", "user_id", "sample"),
    )


def init_db() -> None:
    Base.metadata.create_all(bind=engine)


def get_db() -> Generator[Session, None, None]:
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def _json_request(url: str, cookie_header: str) -> Dict[str, Any]:
    req = urllib.request.Request(url, headers={"Cookie": cookie_header})
    with urllib.request.urlopen(req, timeout=5) as res:
        return json.loads(res.read().decode("utf-8"))


def current_user_from_go_session(request: Request) -> Dict[str, str]:
    cookie_header = request.headers.get("cookie", "")
    if not cookie_header:
        raise HTTPException(status_code=401, detail="not logged in")

    urls = [f"{GO_BACKEND_URL}/user/check"]
    if GO_BACKEND_URL != "http://127.0.0.1:8051":
        urls.append("http://127.0.0.1:8051/user/check")

    last_error: Optional[Exception] = None
    for url in urls:
        try:
            data = _json_request(url, cookie_header)
            user = data.get("user", {})
            user_id = str(user.get("id", ""))
            username = str(user.get("username", ""))
            if user_id:
                return {"id": user_id, "username": username}
        except urllib.error.HTTPError as exc:
            if exc.code == 401:
                raise HTTPException(status_code=401, detail="not logged in")
            last_error = exc
        except Exception as exc:
            last_error = exc

    raise HTTPException(status_code=503, detail=f"auth service unavailable: {last_error}")


async def read_upload(upload: UploadFile, expected_extension: str) -> Tuple[bytes, str]:
    filename = upload.filename or ""
    if not filename.lower().endswith(expected_extension):
        raise HTTPException(status_code=400, detail=f"expected {expected_extension} file")

    content = await upload.read()
    if not content:
        raise HTTPException(status_code=400, detail=f"{expected_extension} file is empty")
    return content, filename


def sha256_hex(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def pair_hash(crc_hash: str, cpr_hash: str) -> str:
    return hashlib.sha256(f"crc:{crc_hash}:cpr:{cpr_hash}".encode("utf-8")).hexdigest()


def get_or_create_ebsd_file(
    db: Session,
    content: bytes,
    filename: str,
    extension: str,
) -> Tuple[EbsdFile, bool]:
    content_hash = sha256_hex(content)
    found = db.execute(
        select(EbsdFile).where(
            EbsdFile.content_hash == content_hash,
            EbsdFile.extension == extension,
        )
    ).scalar_one_or_none()
    if found:
        return found, False

    record = EbsdFile(
        content_hash=content_hash,
        extension=extension,
        original_filename=filename[:255],
        size_bytes=len(content),
        content=content,
    )
    try:
        with db.begin_nested():
            db.add(record)
            db.flush()
        return record, True
    except IntegrityError:
        found = db.execute(
            select(EbsdFile).where(
                EbsdFile.content_hash == content_hash,
                EbsdFile.extension == extension,
            )
        ).scalar_one()
        return found, False


def get_or_create_pair(db: Session, crc_file: EbsdFile, cpr_file: EbsdFile) -> Tuple[EbsdPair, bool]:
    p_hash = pair_hash(crc_file.content_hash, cpr_file.content_hash)
    found = db.execute(select(EbsdPair).where(EbsdPair.pair_hash == p_hash)).scalar_one_or_none()
    if found:
        return found, False

    record = EbsdPair(pair_hash=p_hash, crc_file_id=crc_file.id, cpr_file_id=cpr_file.id)
    try:
        with db.begin_nested():
            db.add(record)
            db.flush()
        return record, True
    except IntegrityError:
        found = db.execute(select(EbsdPair).where(EbsdPair.pair_hash == p_hash)).scalar_one()
        return found, False


def upsert_user_pair(
    db: Session,
    user_id: str,
    pair: EbsdPair,
    *,
    display_name: str,
    sample: str,
    position: str,
    folder: Optional[str],
    version_key: Optional[str],
    version_label: Optional[str],
    version_num: Optional[int],
) -> Tuple[UserEbsdPair, bool]:
    metadata = {
        "display_name": display_name[:255],
        "sample": sample[:255],
        "position": position[:32],
        "folder": folder[:255] if folder else None,
        "version_key": version_key[:64] if version_key else None,
        "version_label": version_label[:255] if version_label else None,
        "version_num": version_num,
        "updated_at": datetime.now(timezone.utc),
    }
    found = db.execute(
        select(UserEbsdPair).where(
            UserEbsdPair.user_id == user_id,
            UserEbsdPair.pair_id == pair.id,
        )
    ).scalar_one_or_none()

    created = False
    if not found:
        found = UserEbsdPair(user_id=user_id, pair_id=pair.id, **metadata)
        try:
            with db.begin_nested():
                db.add(found)
                db.flush()
            created = True
        except IntegrityError:
            found = db.execute(
                select(UserEbsdPair).where(
                    UserEbsdPair.user_id == user_id,
                    UserEbsdPair.pair_id == pair.id,
                )
            ).scalar_one()

    for key, value in metadata.items():
        setattr(found, key, value)
    db.flush()
    return found, created


def upsert_user_target(db: Session, user_id: str, name: str) -> Tuple[UserEbsdTarget, bool]:
    clean_name = name.strip()[:255]
    if not clean_name:
        raise HTTPException(status_code=400, detail="target name is required")

    found = db.execute(
        select(UserEbsdTarget).where(
            UserEbsdTarget.user_id == user_id,
            UserEbsdTarget.name == clean_name,
        )
    ).scalar_one_or_none()
    if found:
        found.updated_at = datetime.now(timezone.utc)
        db.flush()
        return found, False

    record = UserEbsdTarget(user_id=user_id, name=clean_name, updated_at=datetime.now(timezone.utc))
    try:
        with db.begin_nested():
            db.add(record)
            db.flush()
        return record, True
    except IntegrityError:
        found = db.execute(
            select(UserEbsdTarget).where(
                UserEbsdTarget.user_id == user_id,
                UserEbsdTarget.name == clean_name,
            )
        ).scalar_one()
        return found, False
