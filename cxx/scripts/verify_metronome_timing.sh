#!/bin/bash

# Configuration
METRONOME_BIN="./cxx/build/bin/metronome_test"
LOG_TMP="metronome_test.log"
STABILITY_REPORT="stability_report.txt"

if [ ! -f "$METRONOME_BIN" ]; then
    echo "Error: metronome_test binary not found at $METRONOME_BIN"
    echo "Please build the project first."
    exit 1
fi

# Clear stability report
echo "SCENARIO    | IDEAL (ms) | ACTUAL AVG (ms) | JITTER (ms) | STATUS" > "$STABILITY_REPORT"
echo "----------------------------------------------------------------------" >> "$STABILITY_REPORT"

run_scenario() {
    local label=$1
    local bpm=$2
    local bars=$3
    local sig=$4
    local expectation=$5
    local expected_beats=$6

    # Defaults for duration calculation if args are empty
    local d_bpm=${bpm:-80}
    local d_bars=${bars:-2}
    local d_sig=${sig:-4}

    # Ideal Interval (ms) = 60000 / BPM
    local ideal_interval=$(echo "scale=2; 60000 / $d_bpm" | bc)

    # Duration = (60 / BPM) * BeatsPerBar * Bars
    local dur=$(echo "scale=2; (60 / $d_bpm) * $d_sig * $d_bars" | bc)
    local wait_dur=$(echo "scale=0; ($dur + 1.5) / 1" | bc)

    echo "===================================================="
    echo "SCENARIO: $label"
    echo "COMMAND: $METRONOME_BIN $bpm $bars $sig --analyze"
    echo "EXPECTATION: $expectation"
    echo "IDEAL INTERVAL: ${ideal_interval}ms"
    echo "===================================================="
    
    # Run in background and capture output with --analyze flag
    $METRONOME_BIN $bpm $bars $sig --analyze > "$LOG_TMP" 2>&1 &
    PID=$!
    
    sleep $wait_dur
    kill -9 $PID 2>/dev/null || true
    
    # Extract ANALYSIS timestamps
    # [BeatTrigger] [ANALYSIS] Beat X Triggered at <timestamp>us
    timestamps=$(grep "ANALYSIS" "$LOG_TMP" | awk '{print $NF}' | sed 's/us//')
    
    local actual_beats=$(echo "$timestamps" | wc -l)
    echo "Found $actual_beats beats."

    # Jitter Calculation
    if [ "$actual_beats" -gt 1 ]; then
        local sum_deltas=0
        local count=0
        local prev_ts=""
        
        for ts in $timestamps; do
            if [ -n "$prev_ts" ]; then
                local delta=$(echo "$ts - $prev_ts" | bc)
                sum_deltas=$(echo "$sum_deltas + $delta" | bc)
                count=$((count + 1))
            fi
            prev_ts=$ts
        done
        
        # Avg Delta in microseconds
        local avg_delta_us=$(echo "scale=2; $sum_deltas / $count" | bc)
        local avg_delta_ms=$(echo "scale=2; $avg_delta_us / 1000" | bc)
        
        # Jitter = abs(Avg - Ideal)
        local jitter_ms=$(echo "scale=2; if ($avg_delta_ms > $ideal_interval) $avg_delta_ms - $ideal_interval else $ideal_interval - $avg_delta_ms" | bc)
        
        # Status (1% tolerance)
        local tolerance=$(echo "scale=2; $ideal_interval * 0.01" | bc)
        local status="PASS"
        if (( $(echo "$jitter_ms > $tolerance" | bc -l) )); then
            status="FAIL"
        fi
        
        printf "%-11s | %-10s | %-15s | %-11s | %s\n" "$label" "$ideal_interval" "$avg_delta_ms" "$jitter_ms" "$status" >> "$STABILITY_REPORT"
    else
        printf "%-11s | %-10s | %-15s | %-11s | %s\n" "$label" "$ideal_interval" "N/A" "N/A" "FAIL (Too few beats)" >> "$STABILITY_REPORT"
    fi
    
    echo -e "\n"
    rm -f "$LOG_TMP"
}

# 1. Default Scenario
run_scenario "Default" "" "" "" "6.0s duration. 8 beats at a steady 80 BPM." 8

# 2. High Speed Scenario
run_scenario "High Speed" "240" "4" "4" "4.0s duration. Rapid-fire clicks." 16

# 3. Odd Meter Scenario
run_scenario "Odd Meter" "110" "2" "7" "~7.6s duration. 7/4 meter." 14

# 4. Slow Burn Scenario
run_scenario "Slow Burn" "40" "1" "4" "6.0s duration. Slow clicks." 4

# Final Report Summary
echo -e "\n\nSTABILITY REPORT"
cat "$STABILITY_REPORT"
rm -f "$STABILITY_REPORT"
