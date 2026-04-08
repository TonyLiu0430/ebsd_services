FROM golang:1.26-alpine AS builder
WORKDIR /app

# Keep the image minimal and avoid CGO/sqlite-specific build deps.
RUN apk add --no-cache git ca-certificates

# Cache dependencies first.
COPY go.mod go.sum ./
RUN --mount=type=cache,target=/go/pkg/mod \
    go mod download

# Copy the full Go service source tree and build the real entrypoint.
COPY . .
RUN --mount=type=cache,target=/go/pkg/mod \
    --mount=type=cache,target=/root/.cache/go-build \
    CGO_ENABLED=0 GOOS=linux GOARCH=amd64 \
    go build -trimpath -ldflags='-s -w' -o /out/app .

FROM gcr.io/distroless/static-debian12:nonroot
WORKDIR /
COPY --from=builder /out/app /app
USER nonroot:nonroot
EXPOSE 8080
ENTRYPOINT ["/app"]
