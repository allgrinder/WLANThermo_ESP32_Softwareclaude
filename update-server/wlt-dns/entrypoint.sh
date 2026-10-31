#!/bin/sh
# Renders the dnsmasq configuration from the environment and starts dnsmasq.
#   OVERRIDE_HOST     host to redirect (default api.wlanthermo.de)
#   OVERRIDE_IP       IP of wlt-update (required)
#   UPSTREAM_SERVERS  public resolvers, NOT the FritzBox (loop), default 1.1.1.1,9.9.9.9
#   OVERRIDE_TTL      TTL of the override in s (default 60)
#   LOG_QUERIES       1 = log every query
set -e
: "${OVERRIDE_HOST:=api.wlanthermo.de}"
: "${UPSTREAM_SERVERS:=1.1.1.1,9.9.9.9}"
: "${OVERRIDE_TTL:=60}"
if [ -z "$OVERRIDE_IP" ]; then
  echo "OVERRIDE_IP is required" >&2
  exit 1
fi
CONF=/etc/dnsmasq.d/wlt.conf
mkdir -p /etc/dnsmasq.d
{
  echo "no-resolv"
  echo "no-hosts"
  echo "domain-needed"
  echo "bogus-priv"
  echo "cache-size=1000"
  echo "local-ttl=${OVERRIDE_TTL}"
  # A record for the override, no AAAA (the device is IPv4 only)
  echo "host-record=${OVERRIDE_HOST},${OVERRIDE_IP}"
  echo "address=/${OVERRIDE_HOST}/::"
  for s in $(echo "$UPSTREAM_SERVERS" | tr ',' ' '); do
    echo "server=${s}"
  done
  [ "$LOG_QUERIES" = "1" ] && echo "log-queries"
  echo "log-facility=-"
} > "$CONF"
echo "wlt-dns: ${OVERRIDE_HOST} -> ${OVERRIDE_IP} (ttl ${OVERRIDE_TTL}), upstream ${UPSTREAM_SERVERS}"
cat "$CONF"
exec dnsmasq --keep-in-foreground --conf-file="$CONF" --user=dnsmasq --group=dnsmasq
