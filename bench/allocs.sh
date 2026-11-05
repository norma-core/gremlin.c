#!/bin/sh
# Heap allocations per operation for each library, as markdown. Each
# measurement runs alone (BENCH_ONLY) at two iteration counts under
# alloc_count.c; the difference, divided by the extra iterations, leaves out
# setup.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=${BUILD_DIR:-$ROOT/build-bench}
DEEP=$B/gen/deep_nested.bin
GOLDEN=$ROOT/integration-test/binaries/golden_message

cmake -S "$ROOT/bench" -B "$B" -DCMAKE_BUILD_TYPE=Release >/dev/null
# A library no longer found must not leave its old program behind.
rm -f "$B/bench_cpp" "$B/bench_upb" "$B/bench_protobuf_c" "$B/bench_nanopb" "$B/bench_prost"
cmake --build "$B" -j4 --target bench_all >/dev/null

if [ "$(uname)" = Darwin ]; then
	LIB=$B/alloc_count.dylib
	cc -O2 -dynamiclib "$ROOT/bench/alloc_count.c" -o "$LIB"
	PRELOAD=DYLD_INSERT_LIBRARIES
else
	LIB=$B/alloc_count.so
	cc -O2 -shared -fPIC "$ROOT/bench/alloc_count.c" -o "$LIB"
	PRELOAD=LD_PRELOAD
fi
ERR=$(mktemp)
trap 'rm -f "$ERR"' EXIT

# Allocations of one run of the command; a failed run or a missing count
# stops the script rather than reading as zero.
count() {
	env "$PRELOAD=$LIB" "$@" >/dev/null 2>"$ERR" || { cat "$ERR" >&2; echo "$1 failed" >&2; exit 1; }
	n=$(awk '/^allocations / { print $2 }' "$ERR")
	[ -n "$n" ] || { echo "$1: no allocation count, preload not taken" >&2; exit 1; }
	echo "$n"
}

# The gremlin benchmark runs its eight measurements in one program, with a
# fixed warm-up, so it gets one figure: allocations over 8000 extra iterations.
a=$(count "$B/gremlin/benchmark" 1000)
b=$(count "$B/gremlin/benchmark" 2000)
echo "gremlin.c: $((b - a)) allocations over 8000 extra operations"
echo

echo "| Operation | protobuf C++ | upb | protobuf-c | nanopb | prost |"
echo "|---|---|---|---|---|---|"
for w in deep golden; do
	for op in marshal unmarshal lazy deep; do
		line="| $w $op |"
		for p in cpp upb protobuf_c nanopb prost; do
			bin=$B/bench_$p
			if [ ! -x "$bin" ]; then
				c=n/a
			else
				a=$(BENCH_ONLY=$w/$op count "$bin" 1000 "$DEEP" "$GOLDEN")
				# As in run.sh: a library that cannot parse golden_message skips it.
				if [ "$w" = golden ] && grep -q "cannot parse golden" "$ERR"; then
					c=n/a
				else
					b=$(BENCH_ONLY=$w/$op count "$bin" 2000 "$DEEP" "$GOLDEN")
					# The warm-up is fixed, so the second run has 1000 more iterations.
					c=$(awk -v a="$a" -v b="$b" 'BEGIN { printf "%.1f", (b - a) / 1000 }')
				fi
			fi
			line="$line $c |"
		done
		echo "$line"
	done
done
