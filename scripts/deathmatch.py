import yaml
import os
from pathlib import Path
from typing import Dict, Any

from engine import Engine
from protocol import Protocol
from utils import error, Dims

STATS_DIR = Path.home() / 'data' / 'paper-football'

def load_engine(name, dims):
    cfg_dn = STATS_DIR / 'engines' / name
    cfg_fn = cfg_dn / 'info.yaml'

    if not cfg_fn.exists():
        error(f"Engine config not found: {cfg_fn}")

    with open(cfg_fn, 'r') as f:
        config = yaml.safe_load(f)

    binary_name = config['binary']
    binary_path = cfg_dn.parent / binary_name

    params = config.get('params', {})

    if 'qthink' in params:
        params['qthink'] = int(1024 * 1024 * params['qthink'])

    return Engine(name=name, path=str(binary_path), dims=dims, **params)

def run_game(arbiter_config, engine1_config, engine2_config, dims):
    with load_engine(arbiter_config, dims) as arbiter:
        with load_engine(engine1_config, dims) as engine1:
            with load_engine(engine2_config, dims) as engine2:
                return _run_game(arbiter, engine1, engine2)

def _run_game(arbiter, engine1, engine2):
    def status():
        st, errs = arbiter.status()
        if errs:
            for err in errs:
                print("ERROR:", err)
            error("arbiter.status()")

        if st.active is not None:
            return 0  # in progress
        if st.winner == 1:
            return +1
        if st.winner == 2:
            return -1
        error("invalid status")

    def check(move, active):
        success, errs = arbiter.move(move)
        if not success:
            print(f"Arbiter rejected move: {move}")
            for line in errs:
                print(f"  Error: {line}")
            return False

        st, _ = arbiter.status()
        if st.winner is None and st.active != active:
            print(f"Move {move} did not change active player to {active} (got {st.active}, too long move?)")
            return False

        return True

    def opp(engine, num, move):
        success, errs = engine.move(move)
        if success:
            return True

        print(f"Engine {num} rejected move: {move}")
        for line in errs:
            print(f"  Error: {line}")
        return False

    st, errs = arbiter.status()
    if errs:
        for err in errs:
            print("ERROR:", err)
        error("initial status")

    protocol = Protocol(engine1, engine2)

    qmoves = 0
    while qmoves < 1000:
        qmoves += 1

        move, stats = engine1.go(with_stats=True)
        print(f"1> {move}")
        protocol.move(1, move, stats)

        if not check(move, 2):
            protocol.fail(f"Check failed for move {move} from engine 1.")
            protocol.set_result(-1)
            return protocol

        result = status()
        if result:
            protocol.set_result(result)
            return protocol

        if not opp(engine2, 2, move):
            protocol.fail(f"Move {move} rejected by engine 2.")
            protocol.set_result(+1)
            return protocol

        qmoves += 1

        move, stats = engine2.go(with_stats=True)
        print(f"2> {move}")
        protocol.move(2, move, stats)

        if not check(move, 1):
            protocol.fail(f"Check failed for move {move} from engine 2.")
            protocol.set_result(+1)
            return protocol

        result = status()
        if result:
            protocol.set_result(result)
            return protocol

        if not opp(engine1, 1, move):
            protocol.fail(f"Move {move} rejected by engine 1.")
            protocol.set_result(-1)
            return protocol

    protocol.fail("Infinite game")
    return protocol

def deathmatch(arbiter_config, engine1_config, engine2_config, dims):
    protocol = run_game(arbiter_config, engine1_config, engine2_config, dims)
    protocol.update(STATS_DIR)
    s = '+' if protocol.result > 0 else ''
    print(f"Result: {s}{protocol.result}")
    return protocol

if __name__ == "__main__":
    dims = Dims(21, 31, 6, 5)
    deathmatch('origin/1M-140', 'origin/1M-140', 'origin/1M-140', dims)
