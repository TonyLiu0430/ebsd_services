# run
docker compose up --build

# run with cloudflared tunnel
docker compose --profile tunnel up --build

# delete volumes
docker compose down -v

# ARM64 images and Coolify deployment

Pushing to `main` runs `.github/workflows/build-arm64-images.yml` on native
ARM64 GitHub-hosted runners. It publishes these images to GHCR with both
`main` and `sha-<commit>` tags:

- `ghcr.io/tonyliu0430/ebsd-services-frontend`
- `ghcr.io/tonyliu0430/ebsd-services-go-backend`
- `ghcr.io/tonyliu0430/ebsd-services-ml-backend`
- `ghcr.io/tonyliu0430/ebsd-services-cpp-backend`

To make Coolify pull the prebuilt images instead of building them, use:

- Docker Compose location: `/docker-compose.deploy.yml`
- Custom build command: leave empty
- Custom start command: `docker compose --profile tunnel up -d`

`docker-compose.deploy.yml` uses the `main` image tag by default. Set
`IMAGE_TAG=sha-<full-commit-sha>` in Coolify to deploy or roll back to an exact
revision.

GHCR packages are private when first created. Either make the four packages
public in GitHub or configure Coolify with GHCR credentials that can read them.

For automatic deployment after all four images finish successfully:

1. Disable the existing GitHub Auto Deploy option in Coolify. Otherwise it can
   deploy before the new images have finished publishing.
2. Enable API access in Coolify and create an API token with `deploy`
   permission.
3. Copy the application's Deploy Webhook URL.
4. Add `COOLIFY_TOKEN` and `COOLIFY_WEBHOOK` as GitHub Actions repository
   secrets.

If those two secrets are absent, CI only publishes the images and skips the
Coolify deployment without failing the workflow.
