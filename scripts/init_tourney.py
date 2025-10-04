import sys
from pathlib import Path
from tournament import make_schedule
from deathmatch import load_engine
from utils import Dims

FILLME = None
BAD_TYPE = True

CMATCH, TPLAY = 1, 2
TYPE = FILLME

DIMS = Dims(21, 31, 6, 5)

if TYPE == CMATCH:
    c = FILLME
    qthink = FILLME
    engine1, engine2 = FILLME, FILLME

    test_engine = max(engine1, engine2)
    name = f'c-{test_engine:04d}-{qthink}M'
    engines = [ f'dev-{n:04}/{qthink}M-C{c:.1f}' for n in (engine1, engine2) ]
    cycles = 100
    BAD_TYPE = False

if TYPE == TPLAY:
    engine = FILLME
    cycles = FILLME
    qthink = FILLME

    name = f't-{engine:04d}-{qthink}M'
    engine_type = f'dev-{engine:04d}'
    qthinks = [qthink]
    engines = []
    Cs = [0.7, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.6]
    BAD_TYPE = False


STATS_DIR = Path.home() / 'data' / 'paper-football'
TOURNEY_DIR = STATS_DIR / 'tournaments'

def test_engine(name, dims):
    try:
        with load_engine(name, dims) as engine:
            engine.status()
        return True
    except Exception as e:
        print(f"Engine {name} failed: {e}")
        return False

def create_tourney(name, qcycles, dims, *engines):
    print(f"Testing {len(engines)} engines...")
    for engine in engines:
        if not test_engine(engine, dims):
            print(f"Engine {engine} failed, aborting")
            return

    print(f"All {len(engines)} engines OK")

    TOURNEY_DIR.mkdir(exist_ok=True)
    fn = TOURNEY_DIR / f"{name}.txt"

    with open(fn, 'w') as f:
        f.write(f"GAME {dims.width} {dims.height} {dims.goal_width} {dims.free_kick}\n")
        counter = 1
        for r, e1, e2 in make_schedule(qcycles, *engines):
            f.write(f"{counter:4d}\t{r:3d}\t{0:6d}\t{e1:16s}\t{e2:16s}\t***\n")
            counter += 1

    print(f"Tournament '{name}' created with {r} matches")
    print(f"File: {fn}")

if __name__ == "__main__":
    if BAD_TYPE:
        raise Exception(f"Wrong TYPE value ({TYPE}), recheck settings")

    if not engines:
        for qthink in qthinks:
            for C in Cs:
                qthink_str = f"{qthink}M" if qthink != int(qthink) else f"{int(qthink)}M"
                engines.append(f"{engine_type}/{qthink_str}-C{C}")

    create_tourney(name, cycles, DIMS, *engines)
