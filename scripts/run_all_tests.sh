#!/bin/bash

# Configuration
TEST_DIR="cxx/build/bin"
TESTS=("audio_check" "metronome_test" "filter_tests" "stereo_poly_test")

echo "========================================"
echo "   Audio Engine Test Suite Runner"
echo "========================================"
echo "Press Ctrl+C to skip a test or exit."
echo ""

# Ensure we are in the root directory
if [ ! -d "$TEST_DIR" ]; then
    echo "Error: Test directory not found. Please build the project first."
    exit 1
fi

for test_name in "${TESTS[@]}"; do
    binary="$TEST_DIR/$test_name"
    if [ -f "$binary" ]; then
        echo ">>> Running: $test_name"
        # Run the test. TestHelper will handle SIGINT for clean shutdown.
        "$binary"
        
        # Check if the user wants to continue or exit
        # If the test exited with a specific status or we want a small gap
        sleep 1
    else
        echo ">>> Skipping: $test_name (Binary not found)"
    fi
done

echo ""
echo "========================================"
echo "   All tests completed."
echo "========================================"
