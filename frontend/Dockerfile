# use the official Bun image
# see all versions at https://hub.docker.com/r/oven/bun/tags
FROM oven/bun:1.3.14 AS build
WORKDIR /app/frontend
COPY frontend/package.json frontend/bun.lock* ./

# use ignore-scripts to avoid building node modules like better-sqlite3
RUN bun install --frozen-lockfile --ignore-scripts

# Copy the frontend and the framework-independent chart package.
COPY shared/charts /app/shared/charts
COPY frontend /app/frontend

RUN bun --bun run build

# copy production dependencies and source code into final image
FROM oven/bun:1.3.14 AS production
WORKDIR /app

# Only `.output` folder is needed from the build stage
COPY --from=build /app/frontend/.output /app

# run the app
EXPOSE 3000/tcp
ENTRYPOINT [ "bun", "--bun", "run", "/app/server/index.mjs" ]
