#!/bin/sh
cd "$(dirname "$0")/../.." || exit 1
count() {
	for f in "$@"; do
		[ -e "$f" ] || continue
		n=$(grep -cvE '^[[:space:]]*(//.*)?$' "$f")
		printf '%6d %s\n' "$n" "$f"
	done | sort -rn
}
count source/*.cpp source/[!pv]*/*.h source/[!pv]*/*.cpp | awk '{ print; t += $1 } END { printf "%6d core\n", t }'
count source/plugins/*/*/*.h source/plugins/*/*/*.cpp | awk '{ print; t += $1 } END { printf "%6d plugins\n", t }'
