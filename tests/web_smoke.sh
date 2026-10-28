#!/usr/bin/env bash
set -euo pipefail

base_url="${RM_SMOKE_URL:-http://127.0.0.1:8080}"

curl -fsS "$base_url/api/v1/health" | grep -q '"status":"ok"'
curl -fsS "$base_url/api/v1/metrics" | grep -q '"counters"'
curl -fsS "$base_url/api/v1/logs?limit=1" | grep -q '"logs"'
curl -fsS "$base_url/metrics" | grep -q 'rm_rx_packets'
curl -fsS "$base_url/" | grep -q 'RM Monitor'

echo "web smoke test passed"
