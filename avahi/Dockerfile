FROM alpine:3.22.6@sha256:5291449c3df73caf6ed85e649dec1b9e818b39a5d8c871e97afc13e9cd5e8fa8

RUN apk add --no-cache avahi iproute2

COPY entrypoint.sh /usr/local/bin/avahi-entrypoint
RUN sed -i 's/\r$//' /usr/local/bin/avahi-entrypoint \
    && chmod 0755 /usr/local/bin/avahi-entrypoint

ENTRYPOINT ["/usr/local/bin/avahi-entrypoint"]
