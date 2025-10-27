import sys
from pathlib import Path
from collections import defaultdict

STATS_DIR = Path.home() / 'data' / 'paper-football'
TOURNEY_DIR = STATS_DIR / 'tournaments'

class Player:
    def __init__(self, name):
        self.name = name
        self.score = 0
        self.games_played = 0
        self.results = {}

def parse_tourney(fn):
    players = {}

    with open(fn, 'r') as f:
        for line in f:
            if line.startswith('GAME'):
                continue

            parts = line.strip().split('\t')
            if len(parts) >= 6:
                match_id, round_id, game_id, e1, e2, result = parts[:6]
                e1, e2 = e1.strip(), e2.strip()

                if e1 not in players:
                    players[e1] = Player(e1)
                if e2 not in players:
                    players[e2] = Player(e2)

                if result == '1-0':
                    players[e1].results[e2] = players[e1].results.get(e2, '') + '1'
                    players[e2].results[e1] = players[e2].results.get(e1, '') + '0'
                    players[e1].score += 1
                    players[e1].games_played += 1
                    players[e2].games_played += 1

                if result == '0-1':
                    players[e1].results[e2] = players[e1].results.get(e2, '') + '0'
                    players[e2].results[e1] = players[e2].results.get(e1, '') + '1'
                    players[e2].score += 1
                    players[e1].games_played += 1
                    players[e2].games_played += 1

    return list(players.values())

def max_result_len(player):
    if not player.results:
        return 0
    return max(len(results) for results in player.results.values())

def pp_tourney(tourney_name):
    fn = TOURNEY_DIR / f"{tourney_name}.txt"
    players = parse_tourney(fn)

    if not players:
        print("No players found")
        return

    players.sort(key=lambda p: p.score, reverse=True)

    max_name = max(len(p.name) for p in players) if players else 0
    max_results = max(max_result_len(p) for p in players)
    name_width = max(max_name, 6)

    for i, p1 in enumerate(players, 1):
        results = []
        for p2 in players:
            if p1.name == p2.name:
                if len(players) > 2:
                    results.append('=' * max_results)
            else:
                results.append(p1.results.get(p2.name, '').ljust(max_results, '.'))
        results = ' '.join(results)
        win_rate = f"{p1.score / p1.games_played:.3f}" if p1.games_played > 0 else "-"
        print(f"{i:2d}. {p1.name:<{name_width}}   {results} {p1.score:5d}    {win_rate:>5}")

if __name__ == "__main__":
    tourney_name = sys.argv[1]
    pp_tourney(tourney_name)
