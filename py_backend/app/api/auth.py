"""Authentication HTTP routes."""

from fastapi import APIRouter, Request

from app.services import auth


router = APIRouter(prefix="/user")


@router.get("/check")
def check(request: Request):
    return auth.check(request)


@router.post("/logout")
def logout():
    return auth.logout()


@router.post("/register")
async def register(request: Request):
    return await auth.register(request)


@router.post("/login")
async def login(request: Request):
    return await auth.login(request)
