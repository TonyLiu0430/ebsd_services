# Python backend

FastAPI serves authentication, EBSD storage and analysis, and report export.
It reads the existing PostgreSQL `users` schema (`UUID`, unique username,
bcrypt password hash) directly, alongside the EBSD tables.

`JWT_SECRET` is required at startup. Sessions expire after 48 hours and use an
HttpOnly, SameSite=Lax browser-session cookie. Set `COOKIE_SECURE=true` for an
HTTPS-only deployment; leave it false for the documented HTTP LAN setup.
