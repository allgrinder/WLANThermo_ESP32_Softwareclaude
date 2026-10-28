#!/bin/sh
# Builds both images and runs the integration test (no internet needed).
set -e
cd "$(dirname "$0")"
docker build -q $DOCKER_BUILD_ARGS -t wlt-update:local ../wlt-update >/dev/null
docker build -q $DOCKER_BUILD_ARGS -t wlt-dns:local ${WLT_DNS_BASE:+--build-arg BASE=$WLT_DNS_BASE} ../wlt-dns >/dev/null
TEST_DATA=$(mktemp -d)
mkdir -p "$TEST_DATA/fw/import"
printf '\351fake-gas-firmware' > "$TEST_DATA/fw/import/nanoV3-v1.2.4-gas.1.bin"
printf '\351fake-official-firmware' > "$TEST_DATA/fw/import/nanoV3-v1.2.4.bin"
chmod -R 777 "$TEST_DATA"
export TEST_DATA
docker compose -p wlttest -f docker-compose.test.yml up --abort-on-container-exit --exit-code-from tester tester; rc=$?
docker compose -p wlttest -f docker-compose.test.yml logs wlt-update wlt-dns | tail -40
docker compose -p wlttest -f docker-compose.test.yml down -v >/dev/null 2>&1
exit $rc
