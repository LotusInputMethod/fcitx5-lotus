#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# TEST_HOME is required by every script in this harness. The local entrypoint
# owns its creation; run-xvfb.sh --stop tears down the managed processes
# while keeping the directory for post-mortem logs.
if [ -z "${TEST_HOME:-}" ]; then
    TEST_HOME="$(mktemp -d -t fcitx5-browser-e2e-XXXXXX)"
    export TEST_HOME
    echo "TEST_HOME=${TEST_HOME}"
fi

cleanup() {
    "${SCRIPT_DIR}/run-xvfb.sh" --stop || true
}
trap cleanup EXIT

"${SCRIPT_DIR}/setup-fcitx.sh"
"${SCRIPT_DIR}/run-xvfb.sh"

cd "${SCRIPT_DIR}/.."
npm run test:chromium
npm run test:firefox
