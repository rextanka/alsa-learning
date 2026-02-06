#!/bin/bash

# Test script for osc_cli 
# Targets: Sine, Square (Anti-aliased), Sawtooth, and Triangle

OSC_BIN="./bin/osc_cli"

if [ ! -f "$OSC_BIN" ]; then
    echo "Error: $OSC_BIN not found. Please run 'make' first."
    exit 1
fi

echo "--- Starting Oscillator CLI Tests ---"

# 1. Test Sine Wave (Static)
echo "Testing: Static Sine Wave (440Hz)"
$OSC_BIN -w sine -f 440 -d 1

# 2. Test Square Wave (Anti-aliased)
echo "Testing: Static Square Wave (220Hz)"
$OSC_BIN -w square -f 220 -d 1

# 3. Test Sawtooth Sweep
echo "Testing: Sawtooth Sweep (100Hz to 1000Hz)"
$OSC_BIN -w saw -f 100 -t 1000 -d 2

# 4. Test Triangle Glide (Portamento)
echo "Testing: Triangle Glide (880Hz down to 440Hz)"
$OSC_BIN -w triangle -f 880 -t 440 -g 0.5 -d 2

echo "--- Tests Completed ---"