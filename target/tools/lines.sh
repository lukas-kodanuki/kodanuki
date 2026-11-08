#!/bin/sh
cd "$(dirname "$0")/../.." || exit 1
count() {
	for f in "$@"; do
		[ -e "$f" ] || continue
		n=$(grep -cvE '^[[:space:]]*(//.*)?$' "$f")
		printf '%6d %s\n' "$n" "$f"
	done | sort -rn
}
count source/*.cpp source/[!ev]*/*.h source/[!ev]*/*.cpp | awk '{ print; t += $1 } END { printf "%6d core\n", t }'
count source/extras/*/*.h source/extras/*/*.cpp | awk '{ print; t += $1 } END { printf "%6d extras\n", t }'
count source/viewer/*.h source/viewer/*.cpp | awk '{ print; t += $1 } END { printf "%6d viewer\n", t }'
