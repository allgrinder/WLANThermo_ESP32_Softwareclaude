# wlt-update: WLANThermo update server emulator and API proxy (GPL-3.0)
FROM python:3.12-slim
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 DATA_DIR=/data
WORKDIR /srv
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt \
 && useradd --system --uid 1000 --home /srv wlt \
 && mkdir -p /data && chown wlt /data
COPY app ./app
USER wlt
EXPOSE 80
VOLUME ["/data"]
HEALTHCHECK --interval=60s --timeout=5s --start-period=20s \
  CMD python -c "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1/health', timeout=4).status == 200 else 1)"
# port 80 as non root: compose sets net.ipv4.ip_unprivileged_port_start=0
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "80", "--no-access-log"]
