# Run

```bash
docker compose up --build
```

# run with cloudflared tunnel

```bash
docker compose --profile tunnel up --build
```

# delete volumes

> `down -v` permanently deletes the PostgreSQL volume. Back up the database
> first and do not use it for a normal restart.

```bash
docker compose down -v
```

# Shared PC / LAN deployment

Use a dedicated always-on PC as the Docker host. Other computers on the same
LAN access the service through the host PC's IP address, for example
`http://192.168.1.50`.

## Prerequisites checklist

- [ ] The host is a supported 64-bit Windows or Linux machine with hardware
      virtualization enabled in BIOS/UEFI.
- [ ] Docker Desktop (Windows) or Docker Engine (Linux) is installed, running,
      and configured to start when the host boots.
- [ ] Docker Compose v2 is available. Confirm with `docker compose version`;
      the older standalone `docker-compose` command is not required.
- [ ] The logged-in operator can run `docker version` and
      `docker compose version` without errors.
- [ ] Allocate at least 4 CPU cores, 16 GB RAM, and 40 GB free disk space for
      the first native build, images, uploaded EBSD files, and PostgreSQL data.
- [ ] The host has a stable LAN address. Reserve its IP in DHCP or configure a
      static IP so client bookmarks do not change after a reboot.
- [ ] TCP port 80 is unused and allowed through the host firewall for the local
      subnet. The application entry point is Caddy on port 80.
- [ ] Ports 3000, 8000, 8051, and 8080 are not already occupied on the host;
      the development compose file publishes these service ports as well.
- [ ] The host and client PCs are on the same reachable LAN/VLAN, and client
      isolation on the Wi-Fi access point is disabled if wireless clients are
      used.
- [ ] The repository is stored on a local disk with enough space, not in a
      temporary folder or a cloud-sync folder that may lock database files.

## Configure secrets

Copy `.env.example` to `.env` and fill every required value:

```dotenv
POSTGRES_DB=ebsd
POSTGRES_USER=ebsd
POSTGRES_PASSWORD=<long-random-password>
JWT_SECRET=<long-random-secret>
AUTH_SECRET=<different-long-random-secret>
CLOUDFLARE_TUNNEL=
```

Do not reuse the example values or commit `.env`. `CLOUDFLARE_TUNNEL` may stay
empty for LAN-only operation. Keep a protected copy of these values; changing
database credentials later does not automatically update an existing database
volume.

## First start and verification

From the repository directory on the host:

```bash
docker compose config
docker compose up -d --build
docker compose ps
docker compose logs --tail=100 caddy frontend go_backend ml_backend cpp_backend postgres
```

All containers should be running and PostgreSQL should report healthy. Find the
host's LAN IP with `ipconfig` on Windows or `hostname -I` on Linux, then verify:

1. Open `http://localhost` on the host PC.
2. Open `http://<host-LAN-IP>` from a second PC on the same network.
3. Sign in, upload a small test EBSD pair, refresh the page, and generate one
   report to confirm storage and all backends are reachable.
4. Reboot the host once and confirm Docker starts. Because not every service in
   this compose file has a restart policy, configure a Windows scheduled task
   or a Linux systemd unit to run `docker compose up -d` from this repository at
   boot if the stack does not return automatically.

If localhost works but another PC cannot connect, first check the host IP,
Windows/Linux firewall rule for TCP 80, network/VLAN isolation, and whether
another service is already bound to port 80. Do not expose database or backend
ports to the public Internet.

## Operations checklist

- Use `docker compose stop` and `docker compose start` for routine stops and
  starts. Do not use `docker compose down -v` unless database deletion is
  intentional.
- Review capacity periodically with `docker system df` and ensure the disk does
  not fill with images, build cache, uploads, or database data.
- Back up PostgreSQL and the `.env` file before upgrades. Test that the backup
  can be restored on another machine.
- Apply OS and Docker security updates during a maintenance window, then rerun
  the LAN test above.
- Restrict physical access to the shared host, use named application accounts,
  disable automatic sleep while the service is expected to be available, and
  never leave a Docker administrator session unlocked.
- Use the optional Cloudflare tunnel only when remote access is required. LAN
  deployment does not require a tunnel, and public access needs a separate
  security review.

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
