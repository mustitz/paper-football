import os
import subprocess

from time import time
from select import select
from random import randrange
from collections import namedtuple

from utils import counter

MAX_LINES = 3000
BUF_SIZE = 4096

Status = namedtuple('Status', ['active', 'winner'])

class Reader:
    def __init__(self, file_obj):
        self.fd = file_obj.fileno()
        self.buffer = b''

    def _wait_input(self, timeout):
        return select([self.fd], [], [], timeout)[0]

    def _eat_available(self):
        while self._wait_input(0):
            chunk = os.read(self.fd, BUF_SIZE)
            if not chunk:  # EOF
                break
            self.buffer += chunk

    def _read_line(self):
        self._eat_available()
        if b'\n' not in self.buffer:
            return None
        line, self.buffer = self.buffer.split(b'\n', 1)
        return line.decode('utf-8').strip()

    def readline(self, timeout=None):
        line = self._read_line()
        if line is not None:
            return line

        if timeout is None:
            timeout = 86400 # 24 hours chunks

        start_time = time()
        remaining = timeout

        while remaining > 0:
            if not self._wait_input(remaining):
                return None

            line = self._read_line()
            if line is not None:
                return line

            elapsed = time() - start_time
            remaining = timeout - elapsed

        return None

class Engine:
    def __init__(self, name, path, dims, **params):
        self.name = name
        self.path = path
        self.dims = dims
        self.params = params
        self.process = None
        self.stdout_reader = None
        self.stderr_reader = None
        self.seed = randrange(1000000000)
        self._start_process()

    def _start_process(self):
        self.process = subprocess.Popen(
            [self.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        self.stdout_reader = Reader(self.process.stdout)
        self.stderr_reader = Reader(self.process.stderr)

        self._send(f"srand {self.seed}")
        self._send(f"new {self.dims.width} {self.dims.height} {self.dims.goal_width} {self.dims.free_kick}")
        self._send("set ai mcts")

        for param, value in self.params.items():
            self._send(f"set ai.{param} {value}")

    def _send(self, text, timeout=None):
        if not self.process:
            raise RuntimeError("Engine process not running")

        n = counter
        marker = f"SYNC_{n}"

        buf = f"{text}\nping {marker}\n".encode('utf-8')
        self.process.stdin.write(buf)
        self.process.stdin.flush()

        lines, errors = [], []
        while True:
            line = self.stdout_reader.readline(timeout)
            if line is None:
                errors.append(f"ERR: engine hanged, {timeout} sec without response")
                break

            if line == f"pong {marker}":
                break

            lines.append(line)
            qlines = len(lines)
            if qlines > MAX_LINES:
                errors.append(f"ERR: engine produced too many spam, {qlines} generated")
                break

        # Read all available stderr
        while True:
            err = self.stderr_reader.readline(timeout=0)
            if err is None:
                break
            errors.append(err)

        return lines, errors

    def status(self):
        lines, errs = self._send("status")

        active = None
        winner = None

        for line in lines:
            if ':' not in line:
                continue
            name, val = line.split(':', maxsplit=1)
            name = name.lower().strip()
            val = val.lower().strip()

            if name == 'active player':
                active = int(val)
            elif name == 'status':
                if val == 'in progress':
                    pass  # active already set
                elif 'player 1 win' in val:
                    winner = 1
                    active = None
                elif 'player 2 win' in val:
                    winner = 2
                    active = None

        return Status(active=active, winner=winner), errs

    def go(self, with_stats=False):
        stat_list = ['time', 'score', 'steps', 'cache']
        stats = ' ' + ','.join(stat_list) if with_stats else ''

        cmd = "ai go" + stats
        lines, errs = self._send(cmd, timeout=60)
        errs = [ f"E: {err}" for err in errs ]

        if errs:
            return '?', lines + errs

        if not lines:
            return '?', lines + errs

        moves = lines[-1]

        explanation = None
        if with_stats:
            explanation = lines[:-1]

        return moves, explanation + errs

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
