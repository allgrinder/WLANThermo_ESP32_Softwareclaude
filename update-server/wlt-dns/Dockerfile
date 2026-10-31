# wlt-dns: minimal dnsmasq that answers only api.wlanthermo.de locally (GPL-3.0)
# BASE can be any Alpine based image; dnsmasq is installed if missing.
ARG BASE=alpine:3.20
FROM ${BASE}
RUN command -v dnsmasq >/dev/null || apk add --no-cache dnsmasq
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
EXPOSE 53/udp 53/tcp
HEALTHCHECK --interval=60s --timeout=5s CMD nslookup "$OVERRIDE_HOST" 127.0.0.1 | grep -q "$OVERRIDE_IP" || exit 1
ENTRYPOINT ["/entrypoint.sh"]
