#!/bin/sh
set -eu

hostname_value="${MDNS_HOSTNAME:-ebsd}"
interface_value="${MDNS_INTERFACE:-}"

case "$hostname_value" in
    *[!A-Za-z0-9-]* | -* | *- | '')
        echo "MDNS_HOSTNAME must be a single DNS label using letters, numbers, and hyphens." >&2
        exit 1
        ;;
esac
if [ "${#hostname_value}" -gt 63 ]; then
    echo "MDNS_HOSTNAME must be 63 characters or fewer." >&2
    exit 1
fi

is_unsafe_interface() {
    case "$1" in
        lo | docker* | veth* | br-* | virbr* | cni* | flannel* | kube* | \
        tun* | tap* | wg* | tailscale* | zt* | vpn* | ppp*) return 0 ;;
        *) return 1 ;;
    esac
}

if [ -z "$interface_value" ]; then
    interface_value="$(ip -4 route show default | awk '
        { for (i = 1; i <= NF; i++) if ($i == "dev") { print $(i + 1); exit } }
    ')"
    if [ -z "$interface_value" ]; then
        echo "No default-route interface found; set MDNS_INTERFACE explicitly." >&2
        exit 1
    fi
fi

case "$interface_value" in
    *[!A-Za-z0-9_.:-]* | '')
        echo "MDNS_INTERFACE contains invalid characters." >&2
        exit 1
        ;;
esac
if is_unsafe_interface "$interface_value"; then
    echo "Refusing unsafe mDNS interface '$interface_value'; set MDNS_INTERFACE to a LAN interface." >&2
    exit 1
fi
if ! ip link show dev "$interface_value" >/dev/null 2>&1; then
    echo "mDNS interface '$interface_value' does not exist." >&2
    exit 1
fi
if ! ip -o -4 address show dev "$interface_value" scope global | grep -q .; then
    echo "mDNS interface '$interface_value' has no global IPv4 address." >&2
    exit 1
fi

cat > /etc/avahi/avahi-daemon.conf <<EOF
[server]
host-name=$hostname_value
use-ipv4=yes
use-ipv6=no
allow-interfaces=$interface_value
enable-dbus=no
use-iff-running=yes

[wide-area]
enable-wide-area=no

[publish]
publish-addresses=yes
publish-hinfo=no
publish-workstation=no
publish-domain=no
publish-resolv-conf-dns-servers=no

[reflector]
enable-reflector=no
EOF

mkdir -p /run/avahi-daemon
chown avahi:avahi /run/avahi-daemon
echo "Publishing ${hostname_value}.local on ${interface_value}."
exec avahi-daemon --no-chroot --file=/etc/avahi/avahi-daemon.conf
