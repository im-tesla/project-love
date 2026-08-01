#!/bin/sh
# Keeps a DuckDNS record pointing at this machine's current public IP.
#
# Install:
#   sudo cp deploy/duckdns-update.sh /usr/local/bin/duckdns-update
#   sudo chmod +x /usr/local/bin/duckdns-update
#   printf 'DUCKDNS_DOMAIN=milena-led\nDUCKDNS_TOKEN=your-token-here\n' | sudo tee /etc/duckdns.conf
#   sudo chmod 600 /etc/duckdns.conf
#   sudo /usr/local/bin/duckdns-update          # test it once
#
# Then every five minutes, via crontab -e as root:
#   */5 * * * * /usr/local/bin/duckdns-update
#
# The token lives in /etc/duckdns.conf rather than in this file, so the script
# stays safe to keep in the repository. Anyone with that token can repoint your
# domain, so it is chmod 600 and never committed.
#
# DUCKDNS_DOMAIN is the subdomain only. For milena-led.duckdns.org, it is
# "milena-led".

set -eu

CONFIG=/etc/duckdns.conf
LOG=/var/log/duckdns.log

if [ ! -r "$CONFIG" ]; then
    echo "duckdns: cannot read $CONFIG" >&2
    exit 1
fi

# shellcheck source=/dev/null
. "$CONFIG"

if [ -z "${DUCKDNS_DOMAIN:-}" ] || [ -z "${DUCKDNS_TOKEN:-}" ]; then
    echo "duckdns: DUCKDNS_DOMAIN and DUCKDNS_TOKEN must both be set in $CONFIG" >&2
    exit 1
fi

# Leaving ip= empty tells DuckDNS to use the address the request came from,
# which is what we want and saves guessing at it locally.
RESPONSE=$(curl -fsS --max-time 20 \
    "https://www.duckdns.org/update?domains=${DUCKDNS_DOMAIN}&token=${DUCKDNS_TOKEN}&ip=" \
    || echo "REQUEST_FAILED")

STAMP=$(date '+%Y-%m-%d %H:%M:%S')

case "$RESPONSE" in
    OK)
        echo "$STAMP OK" >> "$LOG"
        ;;
    KO)
        # DuckDNS says KO for a bad token or an unknown domain -- it does not
        # distinguish, so check both.
        echo "$STAMP KO -- check DUCKDNS_DOMAIN and DUCKDNS_TOKEN" >> "$LOG"
        exit 1
        ;;
    *)
        echo "$STAMP unexpected response: $RESPONSE" >> "$LOG"
        exit 1
        ;;
esac
