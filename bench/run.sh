#!/bin/sh
# Builds gremlin.c and the other libraries' benchmarks, sizes each one to run
# for a given time, runs them in rotating order for a number of rounds and
# prints the median ns/op and the spread between rounds as markdown.
#
#   bench/run.sh [seconds per benchmark and round] [rounds]
#
# GREMLIN_ZIG_DIR (a gremlin.zig checkout, built with $ZIG) and GREMLIN_GO_DIR
# (norma-core's shared/gremlin_go/bench) add gremlin.zig, gremlin_go and
# protobuf-go.
set -eu

SECONDS_PER_RUN=${1:-30}
ROUNDS=${2:-5}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=${BUILD_DIR:-$ROOT/build-bench}
mkdir -p "$B"
B=$(cd "$B" && pwd)
ZIG=${ZIG:-zig}

# A failed build shows its log; otherwise only what explains an n/a column.
cmake -S "$ROOT/bench" -B "$B" -DCMAKE_BUILD_TYPE=Release >"$B/configure.log" 2>&1 ||
	{ cat "$B/configure.log" >&2; exit 1; }
grep "not found, skipped" "$B/configure.log" >&2 || :
cmake --build "$B" -j4 --target bench_all >"$B/build.log" 2>&1 ||
	{ cat "$B/build.log" >&2; exit 1; }

PROGS="gremlin"
for p in cpp upb protobuf_c nanopb; do
	[ ! -x "$B/bench_$p" ] || PROGS="$PROGS $p"
done
if [ -n "${GREMLIN_ZIG_DIR:-}" ]; then
	# A copy without the golden_message run, which overruns its buffer.
	rm -rf "$B/zig"
	mkdir "$B/zig"
	(cd "$GREMLIN_ZIG_DIR" && tar cf - --exclude .git --exclude .zig-cache --exclude zig-out .) |
		(cd "$B/zig" && tar xf -)
	BZ=$B/zig/integration-test/benchmark.zig
	grep -q "try runGoldenMessageBenchmarks(" "$BZ" ||
		{ echo "gremlin.zig: golden_message call not found in $BZ" >&2; exit 1; }
	sed "/try runGoldenMessageBenchmarks(/d" "$BZ" >"$BZ.new" && mv "$BZ.new" "$BZ"
	(cd "$B/zig" && "$ZIG" build benchmark -Doptimize=ReleaseFast >/dev/null)
	PROGS="$PROGS zig"
fi
if [ -n "${GREMLIN_GO_DIR:-}" ]; then
	(cd "$GREMLIN_GO_DIR" && go build -o "$B/bench_go" ./cmd/benchmark)
	PROGS="$PROGS go"
fi

DEEP=$B/gen/deep_nested.bin
GOLDEN=$ROOT/integration-test/binaries/golden_message
# Raw results stay in the build directory, so a failed summary loses no rounds.
RAW=$B/run-raw.txt
: >"$RAW"
OUT=$(mktemp)
trap 'rm -f "$OUT" "$OUT.p" "$OUT.e"' EXIT
trap 'exit 1' INT TERM

# The gremlin benchmarks print a report for people; turn it into
# "lib workload op ns". gremlin_go's report carries protobuf-go as well.
report() {
	awk -v lib="$1" '
		/DEEP NESTED/          { w = "deep" }
		/GOLDEN/               { w = "golden" }
		/Marshal \(Serialize/  { op = "marshal" }
		/Unmarshal \(Deserial/ { op = "unmarshal" }
		/Root Access/          { op = "lazy" }
		/Deep Access/          { op = "deep" }
		/ns\/op/ {
			if (lib != "go") print lib, w, op, $1
			else if ($1 == "Gremlin:") print "gremlin_go", w, op, $2
			else if ($1 == "Google:") print "protobuf_go", w, op, $2
		}' "$OUT"
}

# One run of program $1 at $2 iterations into $OUT as "lib workload op ns".
# A crashed or failing benchmark stops the run rather than leaving a gap.
run() {
	: >"$OUT.e"
	case $1 in
	gremlin) "$B/gremlin/benchmark" "$2" "$DEEP" >"$OUT" 2>&1 ;;
	zig)     "$B/zig/zig-out/bin/serialization-benchmark" "$2" >"$OUT" 2>&1 ;;
	go)      "$B/bench_go" -n "$2" >"$OUT" 2>&1 ;;
	*)       "$B/bench_$1" "$2" "$DEEP" "$GOLDEN" >"$OUT.p" 2>"$OUT.e" &&
		 sed "s/^/$1 /" "$OUT.p" >"$OUT" &&
		 # Eight measurements, or the four of DeepNested where golden is skipped.
		 { want=8; grep -q "cannot parse golden" "$OUT.e" && want=4
		   [ "$(wc -l <"$OUT")" -eq "$want" ]; } ;;
	esac || { cat "$OUT" "$OUT.e" >&2 2>/dev/null; echo "$1: benchmark failed" >&2; exit 1; }
	case $1 in
	gremlin|zig|go)
		report "$1" >"$OUT.p"
		case $1 in gremlin) want=8 ;; zig) want=4 ;; go) want=16 ;; esac
		[ "$(wc -l <"$OUT.p")" -eq "$want" ] || { echo "$1: unexpected report" >&2; exit 1; }
		mv "$OUT.p" "$OUT" ;;
	esac
}

# Size each program so one run takes about SECONDS_PER_RUN.
for p in $PROGS; do
	run "$p" 100000
	n=$(awk -v s="$SECONDS_PER_RUN" '{ t += $4 } END { n = s * 1e9 / t; print int(n < 100000 ? 100000 : n) }' "$OUT")
	eval "N_$p=$n"
	echo "calibrated $p: $n iterations" >&2
done

count=$(echo $PROGS | wc -w)
r=0
while [ "$r" -lt "$ROUNDS" ]; do
	# Rotate the order so no program always runs first on a cold machine.
	set -- $PROGS
	i=0
	while [ "$i" -lt $((r % count)) ]; do
		first=$1
		shift
		set -- "$@" "$first"
		i=$((i + 1))
	done
	for p in "$@"; do
		echo "round $((r + 1))/$ROUNDS: $p" >&2
		eval "n=\$N_$p"
		run "$p" "$n"
		cat "$OUT" >>"$RAW"
	done
	r=$((r + 1))
done

awk -v rounds="$ROUNDS" -v secs="$SECONDS_PER_RUN" '
function sorted(k, a,   n, i, j, t) {
	n = split(vals[k], a, " ")
	for (i = 2; i <= n; i++)
		for (j = i; j > 1 && a[j - 1] + 0 > a[j] + 0; j--) { t = a[j]; a[j] = a[j - 1]; a[j - 1] = t }
	return n
}
function median(k,   n, a) {
	n = sorted(k, a)
	return n % 2 ? a[(n + 1) / 2] : (a[n / 2] + a[n / 2 + 1]) / 2
}
function spread(k,   n, a, m) {
	n = sorted(k, a)
	m = median(k)
	return m > 0 ? (a[n] - a[1]) / m * 100 : 0
}
function fmt(x,   s, r) {
	if (x < 100) return sprintf("%.1f", x)
	s = sprintf("%d", x + 0.5)
	r = ""
	while (length(s) > 3) { r = "," substr(s, length(s) - 2) r; s = substr(s, 1, length(s) - 3) }
	return s r
}
function cell(lib, w, op,   k, m, g) {
	k = lib SUBSEP w SUBSEP op
	if (!(k in vals)) return "n/a"
	m = median(k)
	if (lib == "gremlin") return sprintf("**%s ns**", fmt(m))
	g = median("gremlin" SUBSEP w SUBSEP op)
	if (g <= 0) return sprintf("%s ns", fmt(m))
	return sprintf("%s ns (%.1fx)", fmt(m), m / g)
}
function table(w, libs, heads,   n, l, h, j, line, k, s, key) {
	n = split(libs, l, " ")
	split(heads, h, "|")
	line = "| Operation |"
	for (j = 1; j <= n; j++) line = line " " h[j] " |"
	print line
	line = "|---|"
	for (j = 1; j <= n; j++) line = line "---|"
	print line
	for (k = 1; k <= 4; k++) {
		line = "| " name[ops[k]] " |"
		for (j = 1; j <= n; j++) line = line " " cell(l[j], w, ops[k]) " |"
		print line
	}
	line = "| Spread |"
	for (j = 1; j <= n; j++) {
		s = -1
		for (k = 1; k <= 4; k++) {
			key = l[j] SUBSEP w SUBSEP ops[k]
			if ((key in vals) && spread(key) > s) s = spread(key)
		}
		line = line (s < 0 ? " n/a |" : sprintf(" %.1f%% |", s))
	}
	print line
	print ""
}
{ k = $1 SUBSEP $2 SUBSEP $3; vals[k] = (k in vals ? vals[k] " " : "") $4; seen[$1] = 1 }
END {
	split("marshal unmarshal lazy deep", ops, " ")
	name["marshal"] = "Marshal"; name["unmarshal"] = "Unmarshal"
	name["lazy"] = "Lazy Read"; name["deep"] = "Deep Access"
	title["deep"] = "DeepNested"; title["golden"] = "golden_message"
	printf "Median ns/op of %d rounds, each program sized to run about %d s a round.\n", rounds, secs
	print "Spread: the widest (max - min) / median between rounds over the four operations.\n"
	for (i = 1; i <= 2; i++) {
		w = i == 1 ? "deep" : "golden"
		printf "%s, same input bytes:\n\n", title[w]
		table(w, "gremlin cpp upb protobuf_c nanopb",
		    "gremlin.c|protobuf C++|upb|protobuf-c|nanopb")
		# zig runs DeepNested only; golden_message in go does other work.
		if (w == "deep" && (("zig" in seen) || ("gremlin_go" in seen))) {
			printf "%s, gremlin in other languages, each its own benchmark program:\n\n", title[w]
			table(w, "gremlin zig gremlin_go protobuf_go",
			    "gremlin.c|gremlin.zig|gremlin_go|protobuf-go")
		}
	}
}' "$RAW"
