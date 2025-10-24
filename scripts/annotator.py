from pathlib import Path
from utils import Dims
from deathmatch import load_engine

STATS_DIR = Path.home() / 'data' / 'paper-football'
GAMES_DIR = STATS_DIR / 'games'
ANNOTATIONS_DIR = STATS_DIR / 'annotations'

DIMS = Dims(21, 31, 6, 5)

ANNOTATOR_ENGINES = [
    'dev-0007/1M-C1.0',
    2,
]

GAMES_TO_ANALYZE = [
    5432,
]

STEPS = {'NW', 'N', 'NE', 'W', 'E', 'SW', 'S', 'SE'}

def find_game(n):
    pattern = f"**/{n:06d}.txt"
    matches = list(GAMES_DIR.glob(pattern))

    if len(matches) == 0:
        raise FileNotFoundError(f"Game {n} not found")
    if len(matches) > 1:
        raise ValueError(f"Multiple files found for game {n}: {matches}")

    return matches[0]

def parse_game(path):
    with open(path, 'r') as f:
        lines = f.readlines()

    attrs = {}
    steps = []

    for line in lines:
        line = line.strip()
        if not line:
            continue

        name, tail = line.split(maxsplit=1)
        if name in ('1', '2'):
            player = int(name)
            steps += [(player, s) for s in tail.split()]
        else:
            attrs[name] = tail

    return attrs, steps

def parse_debug(debug_lines):
    summary = None
    alternatives = []

    for line in debug_lines:
        lexems = line.split()
        if len(lexems) < 3:
            continue

        if lexems[0] in STEPS and lexems[1] == 'in':
            summary = {
                'step': lexems[0],
                'time': lexems[2],
                'score': lexems[4] if len(lexems) > 4 else None,
                'cache': lexems[6] if len(lexems) > 6 else None,
                'from': lexems[8] if len(lexems) > 8 else None,
            }
        elif lexems[0] in STEPS and len(lexems[1]) > 0 and lexems[1][-1] == '%' and lexems[2].isdigit():
            try:
                alternatives.append({
                    'step': lexems[0],
                    'score': float(lexems[1][:-1]),
                    'visits': int(lexems[2])
                })
            except:
                pass

    return summary, alternatives

def annotate(engine, steps):
    analysis = []

    for player, step in steps:
        st, errs = engine.status()
        if errs:
            print(f"ERROR status: {errs}")
            break

        if st.active != player:
            print(f"ERROR: expected player {player}, got {st.active}")
            break

        debug_lines, debug_errs = engine.debug()

        if debug_errs:
            print(f"ERROR debug: {debug_errs}")
            break

        summary, alternatives = parse_debug(debug_lines)

        match = summary and summary['step'] == step

        analysis.append({
            'player': player,
            'step': step,
            'debug': debug_lines,
            'summary': summary,
            'alternatives': alternatives,
            'match': match
        })

        success, errs = engine.move(step)
        if not success:
            print(f"ERROR move: {errs}")
            break

    return analysis

def save_annotation(game_id, engine_name, engine, attrs, steps, analysis, player_num=None):
    from datetime import datetime

    date_str = datetime.now().strftime('%Y-%m-%d')
    date_dir = ANNOTATIONS_DIR / date_str
    date_dir.mkdir(parents=True, exist_ok=True)

    engine_slug = engine_name.replace('/', '-')
    filename = f"{game_id:06d}-{engine_slug}.txt"
    filepath = date_dir / filename

    with open(filepath, 'w') as f:
        for key, val in attrs.items():
            f.write(f"{key} {val}\n")

        annotator_name = str(player_num) if player_num else engine_name
        annotator_line = f"ANNOTATOR {annotator_name} seed={engine.seed} {str(engine.params)}"
        f.write(f"{annotator_line}\n")

        analysis_idx = 0
        for player, step in steps:
            f.write(f"{player} {step}\n")

            if analysis_idx < len(analysis):
                item = analysis[analysis_idx]
                if item['player'] == player and item['step'] == step:
                    for line in item['debug']:
                        indent = '> ' if 'score' in line else '>>  '
                        f.write(f"  {indent}{line}\n")
                    analysis_idx += 1

    return filepath

if __name__ == "__main__":
    for game_id in GAMES_TO_ANALYZE:
        path = find_game(game_id)
        print(f"Game {game_id}: {path}")

        attrs, steps = parse_game(path)

        print("\nAttributes:")
        for key, val in attrs.items():
            print(f"  {key}: {val}")

        print(f"\nTotal steps: {len(steps)}")
        print("\nFirst 10 steps:")
        for i, (player, step) in enumerate(steps[:10]):
            print(f"  {i+1}. Player {player}: {step}")

        print("\nLoading engines:")
        for engine_name in ANNOTATOR_ENGINES:
            if engine_name in (1, 2):
                player_key = f'PLAYER{engine_name}'
                if player_key not in attrs:
                    print(f"  Error: {player_key} not found")
                    continue
                actual_engine_name = attrs[player_key].split()[0]
                print(f"  Using {player_key}: {actual_engine_name}")
            else:
                actual_engine_name = engine_name

            with load_engine(actual_engine_name, DIMS) as engine:
                print(f"  Loaded: {engine.name}")

                print(f"\nAnnotating with {engine.name}...")
                analysis = annotate(engine, steps)

                print(f"\nAnalysis summary:")
                print(f"  Total moves analyzed: {len(analysis)}")

                matches = sum(1 for a in analysis if a['match'])
                total = len(analysis)
                if total > 0:
                    print(f"  Matches: {matches}/{total} ({100*matches/total:.1f}%)")

                player_num = engine_name if engine_name in (1, 2) else None
                annotation_path = save_annotation(game_id, actual_engine_name, engine, attrs, steps, analysis, player_num)
                print(f"  Saved to: {annotation_path}")

                print(f"\nRandom sample from analysis:")
                import random
                sample = random.choice(analysis) if analysis else None
                if sample:
                    print(f"  Player {sample['player']}: {sample['step']} (match: {sample['match']})")
                    if sample['summary']:
                        s = sample['summary']
                        print(f"  Summary: {s['step']} in {s['time']} score {s['score']} cache {s['cache']} from {s['from']}")
                    print(f"  Alternatives ({len(sample['alternatives'])}):")
                    for alt in sample['alternatives'][:5]:
                        print(f"    {alt['step']:3s} {alt['score']:5.1f}% visits {alt['visits']}")
                    print(f"  Debug output:")
                    for line in sample['debug'][:10]:
                        print(f"    {line}")
