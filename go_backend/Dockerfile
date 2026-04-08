FROM golang:1.26-alpine AS builder
WORKDIR /app

RUN apk add --no-cache build-base git ca-certificates

# Cache dependencies first.
COPY go.mod go.sum ./
RUN --mount=type=cache,target=/go/pkg/mod \
    go mod download

# Copy the full Go service source tree and build the real entrypoint.
COPY . .
RUN --mount=type=cache,target=/go/pkg/mod \
    --mount=type=cache,target=/root/.cache/go-build \
    CGO_ENABLED=1 \
    go build -trimpath -ldflags='-s -w' -o /out/app .

FROM alpine:3.23
RUN apk add --no-cache ca-certificates
WORKDIR /
COPY --from=builder /out/app /app
EXPOSE 8080
ENTRYPOINT ["/app"]
