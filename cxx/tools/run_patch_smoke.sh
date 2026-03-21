#!/usr/bin/env bash
# run_patch_smoke.sh — run patch_test --smoke for every patch/midi pair.
#
# Usage (from repo root or build/bin):
#   bash tools/run_patch_smoke.sh [build/bin]
#
# Defaults to build/bin if no argument given.
# Exits 0 if all tests pass, 1 if any fail.

BIN_DIR="${1:-build/bin}"
PASS=0
FAIL=0
FAILED_NAMES=()

if [[ ! -x "${BIN_DIR}/patch_test" ]]; then
    echo "ERROR: patch_test not found at ${BIN_DIR}/patch_test"
    echo "       Build the project first: cmake --build --preset desktop_full"
    exit 1
fi

# Discover all midi/*.mid files relative to BIN_DIR
MIDI_DIR="${BIN_DIR}/midi"
PATCH_DIR="${BIN_DIR}/patches"

if [[ ! -d "${MIDI_DIR}" ]]; then
    echo "ERROR: midi/ directory not found at ${MIDI_DIR}"
    exit 1
fi

echo "Running patch smoke tests from ${BIN_DIR}"
echo "──────────────────────────────────────────────────"

for midi_file in "${MIDI_DIR}"/*.mid; do
    name=$(basename "${midi_file}" .mid)
    patch_file="${PATCH_DIR}/${name}.json"

    if [[ ! -f "${patch_file}" ]]; then
        echo "  SKIP  ${name}  (no matching ${patch_file})"
        continue
    fi

    result=$(cd "${BIN_DIR}" && ./patch_test --smoke \
             --patch "patches/${name}.json" \
             --midi  "midi/${name}.mid" 2>&1)
    peak=$(echo "${result}" | grep "Peak amplitude" | awk '{print $NF}')

    if echo "${result}" | grep -q "\[smoke\] PASS"; then
        printf "  PASS  %-30s  peak=%s\n" "${name}" "${peak}"
        ((PASS++))
    else
        printf "  FAIL  %-30s\n" "${name}"
        echo "${result}" | grep -E "ERROR|FAIL|error" | sed 's/^/         /'
        ((FAIL++))
        FAILED_NAMES+=("${name}")
    fi
done

echo "──────────────────────────────────────────────────"
echo "  Passed: ${PASS}   Failed: ${FAIL}"

if [[ ${FAIL} -gt 0 ]]; then
    echo "  Failed patches: ${FAILED_NAMES[*]}"
    exit 1
fi
exit 0
