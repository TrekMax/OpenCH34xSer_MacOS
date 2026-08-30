#!/usr/bin/env bash

set -euo pipefail

expected_count="${1:-1}"
if [[ ! "$expected_count" =~ ^[1-4]$ ]]; then
    echo "expected node count must be between 1 and 4" >&2
    exit 2
fi
shopt -s nullglob
callout_nodes=(/dev/cu.OpenCH34x*)
dialin_nodes=(/dev/tty.OpenCH34x*)

if [[ "${#callout_nodes[@]}" -ne "$expected_count" ]]; then
    echo "expected $expected_count callout node(s), found ${#callout_nodes[@]}" >&2
    printf '%s\n' "${callout_nodes[@]-}" >&2
    exit 1
fi
if [[ "${#dialin_nodes[@]}" -ne "$expected_count" ]]; then
    echo "expected $expected_count dial-in node(s), found ${#dialin_nodes[@]}" >&2
    printf '%s\n' "${dialin_nodes[@]-}" >&2
    exit 1
fi

for suffix in $(seq $((5 - expected_count)) 4); do
    test -c "/dev/cu.OpenCH34x$suffix"
    test -c "/dev/tty.OpenCH34x$suffix"
done

echo "found $expected_count OpenCH34x serial node pair(s)"
