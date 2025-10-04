#!/usr/bin/env python3

import sys
import os
import re
from pathlib import Path
import statistics

def analyze_engine_timing(engine_name):
    engines_dir = Path.home() / 'data' / 'paper-football' / 'engines'
    engine_path = engines_dir / engine_name

    if not engine_path.exists():
        print(f"Engine path not found: {engine_path}")
        return

    times = []
    pattern = re.compile(r'> \w+ in (\d+\.\d+)s')

    for log_file in engine_path.rglob('*.txt'):
        try:
            with open(log_file, 'r') as f:
                for line in f:
                    match = pattern.search(line)
                    if match:
                        time_val = float(match.group(1))
                        times.append(time_val)
        except Exception as e:
            print(f"Error reading {log_file}: {e}")
            continue

    if not times:
        print(f"No timing data found for {engine_name}")
        return

    print(f"Engine: {engine_name}")
    print(f"Total moves analyzed: {len(times)}")
    print(f"")
    print(f"Timing (seconds):")
    print(f"  Average: {statistics.mean(times):.3f}s")
    print(f"  Median:  {statistics.median(times):.3f}s")
    print(f"  Min:     {min(times):.3f}s")
    print(f"  Max:     {max(times):.3f}s")
    print(f"  StdDev:  {statistics.stdev(times):.3f}s")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python engine_timing.py <engine_name>")
        print("Example: python engine_timing.py origin/1M-C1.4")
        sys.exit(1)

    engine_name = sys.argv[1]
    analyze_engine_timing(engine_name)
