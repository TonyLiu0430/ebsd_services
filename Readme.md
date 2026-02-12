# run
docker compose up --build

# run with cloudflared tunnel
docker compose --profile tunnel up --build

# delete volumes
docker compose down -v
