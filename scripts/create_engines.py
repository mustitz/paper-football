import os
from pathlib import Path

FILLME = None
engine_name = FILLME
commit = FILLME
qthink_values = [1, 2, 4, 9]
C_values = [0.7, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.6]

STATS_DIR = Path.home() / 'data' / 'paper-football'
ENGINES_DIR = STATS_DIR / 'engines' / engine_name

template = """binary: paper-football
commit: {commit}
params:
  qthink: {qthink}
  max_depth: 128
  C: {C}"""

for qthink in qthink_values:
    for C in C_values:
        # Create engine name: 0.5M-C0.3, 1M-C1.4, etc.
        qthink_str = f"{qthink}M" if qthink != int(qthink) else f"{int(qthink)}M"
        C_str = f"C{C}"
        engine_name = f"{qthink_str}-{C_str}"

        # Create directory
        engine_dir = ENGINES_DIR / engine_name
        engine_dir.mkdir(parents=True, exist_ok=True)

        # Create info.yaml
        config_file = engine_dir / 'info.yaml'
        if config_file.exists():
            print(f"Skipped: {engine_name} (already exists)")

        content = template.format(commit=commit, qthink=qthink, C=C)

        with open(config_file, 'w') as f:
            f.write(content)
            f.write('\n')

        print(f"Created: {engine_name}")

print(f"\nTotal: {len(qthink_values) * len(C_values)} engines created")
