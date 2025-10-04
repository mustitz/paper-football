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
    move_lengths = []
    timing_pattern = re.compile(r'> \w+ in (\d+\.\d+)s')
    move_pattern = re.compile(r'^([12]) ([NSEW ]+)$')

    for log_file in engine_path.rglob('*.txt'):
        try:
            # Determine our player number from filename
            filename = log_file.name
            our_player_num = None
            if filename.endswith('-1.txt'):
                our_player_num = 1
            elif filename.endswith('-2.txt'):
                our_player_num = 2
            else:
                continue

            with open(log_file, 'r') as f:
                for line in f:
                    line = line.strip()

                    move_match = move_pattern.search(line)
                    if move_match:
                        moves_str = move_match.group(2).strip()
                        move_length = len(moves_str.split())

                        # Only count penalty moves (4+ steps)
                        if move_length > 3:
                            penalty_length = move_length - 3
                            move_lengths.append(penalty_length)
                        continue

                    timing_match = timing_pattern.search(line)
                    if timing_match:
                        time_val = float(timing_match.group(1))
                        if time_val >= 0.001:
                            times.append(time_val)
        except Exception as e:
            print(f"Error reading {log_file}: {e}")
            continue

    if not times:
        return None

    return {
        'engine': engine_name,
        'total_moves': len(times),
        'avg_time': statistics.mean(times),
        'median_time': statistics.median(times),
        'min_time': min(times),
        'max_time': max(times),
        'stdev_time': statistics.stdev(times) if len(times) > 1 else 0.0,
        'avg_penalty_length': statistics.mean(move_lengths) if move_lengths else 0.0
    }

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python engine_timing.py <engine_name> [engine_name2] ...")
        print("Example: python engine_timing.py origin/1M-C1.4 dev-0003/1M-C1.1")
        sys.exit(1)

    results = []
    for engine_name in sys.argv[1:]:
        result = analyze_engine_timing(engine_name)
        if result:
            results.append(result)
        else:
            print(f"Warning: No timing data found for {engine_name}")

    if len(results) == 1:
        # Single engine - old format
        r = results[0]
        print(f"Engine: {r['engine']}")
        print(f"Total moves analyzed: {r['total_moves']}")
        print(f"")
        print(f"Timing (seconds):")
        print(f"  Average: {r['avg_time']:.3f}s")
        print(f"  Median:  {r['median_time']:.3f}s")
        print(f"  Min:     {r['min_time']:.3f}s")
        print(f"  Max:     {r['max_time']:.3f}s")
        print(f"  StdDev:  {r['stdev_time']:.3f}s")
        print(f"")
        print(f"Average Penalty Length: {r['avg_penalty_length']:.1f}")
    else:
        # Multiple engines - table format
        engine_width = max(len("Engine"), max(len(r['engine']) for r in results))

        print(f"{'Engine':<{engine_width}} {'Moves':>6} {'Avg':>7} {'Median':>7} {'Max':>7} {'StdDev':>7} {'PenLen':>7}")
        print('-' * (engine_width + 50))

        for r in results:
            print(f"{r['engine']:<{engine_width}} "
                  f"{r['total_moves']:>6} "
                  f"{r['avg_time']:>7.3f} "
                  f"{r['median_time']:>7.3f} "
                  f"{r['max_time']:>7.3f} "
                  f"{r['stdev_time']:>7.3f} "
                  f"{r['avg_penalty_length']:>7.1f}")
