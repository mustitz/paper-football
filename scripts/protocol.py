import fcntl
from datetime import datetime
from collections import namedtuple

Move = namedtuple('Move', ['player', 'text', 'stats'])

class Protocol:
    def __init__(self, engine1, engine2):
        self.engine1 = engine1
        self.engine2 = engine2
        self.moves = []
        self.failure = None
        self.result = None
        self.qmoves = 0
        self.game_id = None

    def move(self, player, move, stats):
        self.moves.append(Move(player, move, stats))
        self.qmoves += len(move.split())

    def fail(self, text):
        self.failure = text

    def set_result(self, result):
        self.result = result

    def update(self, path):
        games_dn = path / 'games'
        games_dn.mkdir(exist_ok=True)

        games_fn = games_dn / 'stats.txt'
        games_fn.touch(exist_ok=True)

        sresult = '???'
        if self.result == +1:
            sresult = '1-0'
        if self.result == -1:
            sresult = '0-1'
        sfail = self.failure if self.failure else 'OK'

        now = datetime.now()
        ts = now.isoformat(timespec='seconds')

        def header(f, player=None):
            f.write(f"DATE {ts}\n")
            f.write(f"PLAYER1 {e1.name} seed={e1.seed} {str(e1.params)}\n")
            f.write(f"PLAYER2 {e2.name} seed={e2.seed} {str(e2.params)}\n")
            f.write(f"RESULT {sresult}\n")
            f.write(f"GAME {e1.dims.width} {e1.dims.height} {e1.dims.goal_width} {e1.dims.free_kick}\n")

            if self.failure and player is not None:
                loser = None
                if self.result == +1:
                    loser = 2
                elif self.result == -1:
                    loser = 1

                if player == loser:
                    f.write(f"FAIL {self.failure}\n")

        e1, e2 = self.engine1, self.engine2
        with open(games_fn, 'a+') as f:
            fcntl.flock(f, fcntl.LOCK_EX)
            try:
                f.seek(0)
                lines = f.read().splitlines()
                n = len(lines) + 1
                f.write(f"{n:6d}\t{e1.name:15s}\t{e2.name:15s}\t{sresult}\t{self.qmoves:3d}\t{ts}\t{sfail}\n")
            finally:
                fcntl.flock(f, fcntl.LOCK_UN)

        dn = games_dn / now.date().isoformat()
        dn.mkdir(exist_ok=True)

        fn = dn / f"{n:06d}.txt"
        with open(fn, 'w') as f:
            header(f)
            for m in self.moves:
                f.write(f"{m.player} {m.text}\n")

        dn = path / 'engines' / e1.name / now.date().isoformat()
        dn.mkdir(exist_ok=True)
        fn = dn / f"{n:06d}-1.txt"
        with open(fn, 'w') as f:
            header(f, 1)
            self.detailed(1, f)

        dn = path / 'engines' / e2.name / now.date().isoformat()
        dn.mkdir(exist_ok=True)
        fn = dn / f"{n:06d}-2.txt"
        with open(fn, 'w') as f:
            header(f, 2)
            self.detailed(2, f)

        self.game_id = n

    def detailed(self, player, f):
        for m in self.moves:
            f.write(f"{m.player} {m.text}\n")
            if m.player == player:
                if m.stats:
                    for line in m.stats:
                        indent = '> ' if 'score' in line else '>>  '
                        f.write(f"  {indent}{line}\n")
