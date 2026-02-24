#!/bin/bash

# Get the script's directory and then the cxx root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CXX_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"
TEST_DIR="$CXX_ROOT/build/bin"

TESTS=("audio_check" "metronome_test" "filter_tests" "stereo_poly_test" "processor_check" "engine_tests" "Phase10Tests" "TimingValidation")

echo "========================================"
echo "   Audio Engine Test Suite Runner"
echo "========================================"
echo "Searching for binaries in: $TEST_DIR"
echo "Press Ctrl+C to skip a test or exit."
echo ""

# Ensure test directory exists
if [ ! -d "$TEST_DIR" ]; then
    echo "Error: Test directory not found at $TEST_DIR. Please build the project first."
    exit 1
fi

# Function to run with timeout on macOS/Linux
run_with_timeout() {
    local duration=$1
    local cmd=$2
    
    if command -v timeout &> /dev/null; then
        timeout "$duration" "$cmd"
    elif command -v gtimeout &> /dev/null; then
        gtimeout "$duration" "$cmd"
    else
        # Fallback for systems without timeout/gtimeout
        "$cmd" &
        local pid=$!
        (sleep "$duration"; kill "$pid" 2>/dev/null) &
        wait "$pid"
    fi
}

for test_name in "${TESTS[@]}"; do
    binary="$TEST_DIR/$test_name"
    if [ -f "$binary" ]; then
        echo ">>> Running: $test_name"
        
        if [[ "$test_name" == "metronome_test" || "$test_name" == "audio_check" ]]; then
            # Limit long-running functional tests to 3 seconds for validation
            run_with_timeout 3 "$binary"
        else
            "$binary"
        fi
        
        echo ""
        sleep 0.5
    else
        echo ">>> Skipping: $test_name (Binary not found at $binary)"
    fi
done

echo "========================================"
echo "   All tests completed."
echo "========================================"
