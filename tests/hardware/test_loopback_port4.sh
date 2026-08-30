#!/bin/bash

set -euo pipefail

probe=$1
payload='00017f80ff4348393334342d544444a5'
output=$("$probe" loopback --port 4 --baud 115200 --payload-hex "$payload")

grep -Fxq 'PASS port=4 baud=115200 bytes=16' <<<"$output"

output=$("$probe" loopback --port 4 --baud 115200 --length 509)

grep -Fxq 'PASS port=4 baud=115200 bytes=509' <<<"$output"
