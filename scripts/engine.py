import subprocess
from typing import List, Optional
from select import select
from random import randrange

from utils import counter

class Engine:
    def __init__(self, name, path, dims, **params):
        self.name = name
        self.path = path
        self.dims = dims
        self.params = params
        self.process = None
        self.seed = randrange(1000000000)
        self._start_process()

    def _start_process(self):
        self.process = subprocess.Popen(
            [self.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self._send(f"srand {self.seed}")
        self._send(f"new {self.dims.width} {self.dims.height} {self.dims.goal_width} {self.dims.free_kick}")
        self._send("set ai mcts")

        for param, value in self.params.items():
            self._send(f"set ai.{param} {value}")

    def _send(self, text):
        if not self.process:
            raise RuntimeError("Engine process not running")

        n = counter
        marker = f"SYNC_{n}"

        buf = f"{text}\nping {marker}\n".encode('utf-8')
        self.process.stdin.write(buf)
        self.process.stdin.flush()

        lines = []
        while True:
            line = self.process.stdout.readline().decode('utf-8').strip()
            if line == f"pong {marker}":
                break
            lines.append(line)

        errors = []
        while select([self.process.stderr], [], [], 0)[0]:
            errors.append(self.process.stderr.readline().decode('utf-8').strip())

        return lines, errors

    def status(self):
        return self._send("status")

    def go(self, with_stats=False):
        stat_list = ['time', 'score', 'steps', 'cache']
        stats = ' ' + ','.join(stat_list) if with_stats else ''

        cmd = "ai go" + stats
        lines, errs = self._send(cmd)

        if not lines:
            print(*errs)
            raise RuntimeError("No response from ai go command")

        moves = lines[-1]

        explanation = None
        if with_stats:
            explanation = lines[:-1]

        return moves, explanation

    def move(self, moves_str):
        lines, errs = self._send(f"step {moves_str}")
        return not bool(errs), errs

    def close(self):
        if self.process:
            try:
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
                self.process.wait(timeout=5)
            except:
                self.process.terminate()
                self.process.wait(timeout=2)
                if self.process.poll() is None:
                    self.process.kill()
            finally:
                self.process = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
