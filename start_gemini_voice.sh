#!/usr/bin/env bash
# Starts the Gemini Live voice relay for Demo 02.1 (head movements, moods, LEDs & dance by voice)
# directly over USB-C (or Wi-Fi UDP) without re-flashing the robot.
set -euo pipefail
cd "$(dirname "$0")"
if [ -f .env ]; then
    set -a
    # shellcheck disable=SC1091
    source .env
    set +a
fi
exec ./.venv/bin/python demos/02.1-voice-moves/relay/main.py "$@"
