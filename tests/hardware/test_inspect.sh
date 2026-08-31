#!/bin/bash

set -euo pipefail

probe=$1
output=$("$probe" inspect)

grep -Fxq 'device 1a86:e018' <<<"$output"
grep -Fxq 'interface 0' <<<"$output"
grep -Fxq 'data-in 0x82 bulk 512' <<<"$output"
grep -Fxq 'data-out 0x02 bulk 512' <<<"$output"
grep -Fxq 'command-in 0x81 bulk 512' <<<"$output"
grep -Fxq 'command-out 0x01 bulk 512' <<<"$output"
grep -Eq '^chip CH9344[QL] version 0x[0-9a-f]{2}$' <<<"$output"
