#!/usr/bin/env bash
# One command, no interaction, complete M0 report.
#
# It runs every measurement that does not need a human: build, capability report, AT-SPI prerequisites,
# the X11 mechanism self-test, and an AT-SPI closed loop against a GTK publisher this script starts
# itself (so no text has to be selected by hand). The result is written to RESULTS-auto.md together with
# a computed verdict, so a single file carries the whole answer.
#
#   ./run-m0.sh            # ~40 s, unattended
#   ./run-m0.sh --keep     # keep the publisher window open 2 s longer between phases (debugging)
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
build="$root/build"
work="$build/m0-auto"
report="$here/RESULTS-auto.md"

B="$build/m0/snip-probe"
A="$build/m0-atspi/snip-atspi-probe"
PUBLISH_MS=12000
WATCH_MS=10000

mkdir -p "$work"
: > "$report"

section() { printf '\n## %s\n\n' "$1" | tee -a "$report"; }
block() { printf '```\n' | tee -a "$report"; }
endblock() { printf '```\n' | tee -a "$report"; }
capture() { tee -a "$report"; }

{
    printf '# M0 automatic report\n\n'
    printf -- '- generated: %s\n' "$(date -u +%FT%TZ)"
    printf -- '- host: %s\n' "$(uname -srm)"
    printf -- '- session: XDG_SESSION_TYPE=%s desktop=%s\n' "${XDG_SESSION_TYPE:-?}" "${XDG_CURRENT_DESKTOP:-?}"
} | capture

# ---------------------------------------------------------------- build
section "0. Build"
if ! "$here/build.sh" > "$work/build.log" 2>&1; then
    { echo "BUILD FAILED — see $work/build.log"; echo; tail -30 "$work/build.log"; } | capture
    echo "build failed; report at $report"
    exit 1
fi
{ grep -E "^(ok|binaries|  /)" "$work/build.log" || tail -5 "$work/build.log"; } | capture

# ---------------------------------------------------------------- environment
section "1. Environment and portals"
block; "$B" caps 2>&1 | capture; endblock

section "2. AT-SPI prerequisites"
block; "$A" diag 2>&1 | capture; endblock

# ---------------------------------------------------------------- X11 mechanism
section "3. X11 mechanism self-test (no interaction)"
block; "$B" x11 selftest --count 5 2>&1 | capture; endblock
x11_pass=$(grep -c " PASS" "$report" 2>/dev/null)
x11_pass=${x11_pass:-0}

# ---------------------------------------------------------------- AT-SPI closed loop
section "4. AT-SPI closed loop against a controlled GTK publisher"
{
    echo "The publisher selects a different word every 900 ms in a GTK3 text view and prints exactly"
    echo "what it selected, so every event below has a known expected range. No human selection and no"
    echo "private content are involved."
    echo
} | capture

publisher_pid=""
(env -u NO_AT_BRIDGE /usr/bin/python3 "$here/tools/atspi_publisher.py" "$PUBLISH_MS" \
    > "$work/publisher.log" 2>&1) &
publisher_pid=$!
sleep 2

"$A" watch --duration "$WATCH_MS" --show-text --retry-ms 200 > "$work/closed-loop.log" 2>&1
block; capture < "$work/closed-loop.log"; endblock

if [ -n "$publisher_pid" ]; then
    wait "$publisher_pid" 2>/dev/null || true
fi
echo "Publisher log:" | capture
block
if [ -f "$work/publisher.log" ]; then capture < "$work/publisher.log"; else echo "(no publisher log)"; fi | capture
endblock

# ---------------------------------------------------------------- 4b: event-type probe
# Pre-read of the event count so the fallback below can decide for itself.
events=$(grep -c '^seq=' "$work/closed-loop.log" 2>/dev/null)
events=${events:-0}
types_rows=-1

if [ "$events" -eq 0 ]; then
    section "4b. Event-type probe (the subscription delivered nothing, so ask what the provider emits)"
    (env -u NO_AT_BRIDGE /usr/bin/python3 "$here/tools/atspi_publisher.py" 9000 \
        > "$work/publisher-types.log" 2>&1) &
    types_pid=$!
    sleep 2
    "$A" watch --duration 7000 --events object --group --app "publisher" \
        > "$work/closed-loop-types.log" 2>&1
    block; capture < "$work/closed-loop-types.log"; endblock
    wait "$types_pid" 2>/dev/null || true
    echo "Provider log for this phase:" | capture
    echo | capture
    block
    if [ -f "$work/publisher-types.log" ]; then capture < "$work/publisher-types.log"; fi | capture
    endblock
    types_rows=$(grep -c "^publisher\| | object" "$work/closed-loop-types.log" 2>/dev/null)
    types_rows=${types_rows:-0}
fi

# ---------------------------------------------------------------- verdict
# grep -c prints 0 and exits 1 when nothing matches; do not append a second value.
events=$(grep -c '^seq=' "$work/closed-loop.log" 2>/dev/null)
events=${events:-0}
with_text=$(awk -F'chars=' '/^seq=/{split($2,a," "); if (a[1]+0 > 0) c++} END{print c+0}' "$work/closed-loop.log")
zero_len=$(awk -F'len=' '/range\[/{split($2,a," "); if (a[1]+0 == 0) c++} END{print c+0}' "$work/closed-loop.log")
avg_text_ms=$(awk -F'text_ms=' '/^seq=/{split($2,a," "); s+=a[1]; n++} END{if (n) printf "%.1f", s/n; else printf "n/a"}' "$work/closed-loop.log")

section "5. Verdict"
{
    echo "| measure | value |"
    echo "| --- | --- |"
    echo "| X11 self-test passes | $x11_pass / 5 |"
    echo "| AT-SPI events observed (controlled provider) | $events |"
    echo "| events that carried text | $with_text |"
    echo "| zero-length ranges seen | $zero_len |"
    echo "| mean text resolve time | ${avg_text_ms} ms |"
    echo
    if [ "$with_text" -gt 0 ]; then
        echo "**AT-SPI read path: WORKS.** A known-good GTK provider delivered real text through"
        echo "\`object:text-selection-changed\`. Applications that yield nothing are an application coverage"
        echo "question, not a defect in our read path."
    elif [ "$events" -gt 0 ]; then
        echo "**AT-SPI read path: SUSPECT.** Events arrived from the controlled provider but no text was"
        echo "read, while the publisher log shows real ranges. That points at our read logic (provider"
        echo "range semantics), not at the applications."
    else
        echo "**AT-SPI read path: NO EVENTS** for \`object:text-selection-changed\`. Either the publisher's"
        echo "accessibility bridge is disabled, or the provider emits a different event type."
    fi
    if [ "${types_rows:--1}" -ge 0 ]; then
        echo
        if [ "$types_rows" -gt 0 ]; then
            echo "The event-type probe saw $types_rows application/event-type row(s) from the same provider"
            echo "(table in 4b). If a text-related row is present, the subscription — not the provider — is"
            echo "what needs changing."
        else
            echo "The event-type probe saw **no** object event at all from the publisher, which points at"
            echo "the provider's accessibility bridge, not at the event type."
        fi
    fi
    echo
    if [ "$x11_pass" -ge 5 ]; then
        echo "**X11 mechanism: WORKS** (5/5 conversions answered)."
    elif [ "$x11_pass" -gt 0 ]; then
        echo "**X11 mechanism: PARTIAL** ($x11_pass/5). Check the failure lines above for owner identity"
        echo "(\`owned by us=no\` means the compositor/XWayland took PRIMARY over)."
    else
        echo "**X11 mechanism: NOT VERIFIED** (0/5). On a Wayland session the self-test may be mediated by"
        echo "XWayland; the authoritative measurement is \`snip-probe x11 watch\` with a real application."
    fi
} | capture

echo
echo "report: $report"
echo
grep -A3 "| measure |" "$report" | tail -3
