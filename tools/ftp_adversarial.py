#!/usr/bin/env python3
"""Adversarial FTP servers for mega-ftp: one port per misbehaviour.

    python3 tools/ftp_adversarial.py [advertised-ip]

Each scenario is a tiny FTP server on its own port; the client is pointed
at the port and its status line says how it coped. The advertised IP goes
into PASV replies (default: the machine's LAN address).

  2201 stall-list   LIST: 150, then no data, ever      -> inactivity timeout
  2202 drip         LIST data one byte every 2 s       -> completes, slowly
  2203 rst-data     RETR: half the file, then RST      -> error, partial removed
  2204 drop-ctrl    control closed after the first LIST -> "server closed"; next action reconnects
  2205 refuse       421 banner, close                  -> connect error with the text
  2206 silent       accept, never speak                -> connect timeout
  2207 big-list     150 entries                        -> "listing truncated", 64 kept
  2208 odd-list     DOS lines, spaces, symlink, junk   -> parsed sanely
  2209 bad-pasv     227 with no numbers                -> PASV error
  2210 retr-550     RETR 550, STOR 553                 -> the server's words
"""
import socket, sys, threading, time, random

SCENARIOS = {
    2201: "stall-list", 2202: "drip", 2203: "rst-data", 2204: "drop-ctrl", 2205: "refuse",
    2206: "silent", 2207: "big-list", 2208: "odd-list", 2209: "bad-pasv", 2210: "retr-550",
}

def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1)); return s.getsockname()[0]
    finally:
        s.close()

ADV_IP = sys.argv[1] if len(sys.argv) > 1 else lan_ip()

def unix_line(name, size=1234, d=False):
    return f"{'d' if d else '-'}rw-r--r--   1 user  group  {size:8d} Sep  5 01:00 {name}\r\n"

NORMAL_LIST = unix_line("docs", 96, True) + unix_line("hello.txt", 400) + unix_line("pattern.bin", 20000)

ODD_LIST = (
    "total 5\r\n"
    "\r\n"
    "09-05-26  01:00PM       <DIR>          Old Stuff\r\n"
    "09-05-26  01:00PM                 4321 report v2.txt\r\n"
    "lrwxrwxrwx   1 user  group        11 Sep  5 01:00 link -> target.txt\r\n"
    + unix_line("a" * 100 + ".bin", 99)
    + "this is not a listing line at all\r\n"
)

class Session(threading.Thread):
    def __init__(self, conn, port):
        super().__init__(daemon=True); self.c = conn; self.port = port; self.mode = SCENARIOS[port]
        self.pasv = None; self.lists = 0
    def say(self, s):
        try: self.c.sendall((s + "\r\n").encode())
        except OSError: pass
    def open_pasv(self):
        ls = socket.socket(); ls.bind(("0.0.0.0", 0)); ls.listen(1); ls.settimeout(20)
        p = ls.getsockname()[1]; self.pasv = ls
        h = ADV_IP.replace(".", ",")
        self.say(f"227 Entering Passive Mode ({h},{p >> 8},{p & 255}).")
    def data_conn(self):
        try: d, _ = self.pasv.accept()
        except OSError: return None
        self.pasv.close(); self.pasv = None
        return d
    def run(self):
        m = self.mode
        if m == "silent":
            time.sleep(120); self.c.close(); return
        if m == "refuse":
            self.say("421 Service not available, try later."); time.sleep(0.5); self.c.close(); return
        self.say(f"220 adversarial {m}")
        f = self.c.makefile("rb")
        for raw in f:
            line = raw.decode(errors="replace").rstrip("\r\n")
            verb, _, arg = line.partition(" ")
            verb = verb.upper()
            if verb == "USER": self.say("331 give any password")
            elif verb == "PASS": self.say("230 logged in")
            elif verb == "SYST": self.say("215 UNIX Type: L8")
            elif verb == "TYPE": self.say("200 ok")
            elif verb == "NOOP": self.say("200 ok")
            elif verb == "PWD": self.say('257 "/" is the current directory')
            elif verb in ("CWD", "CDUP"): self.say("250 ok")
            elif verb == "QUIT": self.say("221 bye"); break
            elif verb == "PASV":
                if m == "bad-pasv": self.say("227 Entering Passive Mode (no numbers here).")
                else: self.open_pasv()
            elif verb == "LIST":
                self.lists += 1
                self.say("150 here it comes")
                d = self.data_conn()
                if not d: self.say("425 no data connection"); continue
                if m == "stall-list":
                    time.sleep(90); d.close(); self.say("226 done"); continue
                if m == "drip":
                    for ch in NORMAL_LIST[:40].encode():
                        d.sendall(bytes([ch])); time.sleep(2)
                    d.sendall(NORMAL_LIST[40:].encode())
                elif m == "big-list":
                    d.sendall("".join(unix_line(f"file{i:03d}.dat", i * 7) for i in range(150)).encode())
                elif m == "odd-list":
                    d.sendall(ODD_LIST.encode())
                else:
                    d.sendall(NORMAL_LIST.encode())
                d.close(); self.say("226 done")
                if m == "drop-ctrl" and self.lists == 1:
                    time.sleep(1); self.c.close(); return
            elif verb == "RETR":
                if m == "retr-550": self.say("550 " + arg + ": no such file, says the adversary"); continue
                self.say("150 sending")
                d = self.data_conn()
                if not d: self.say("425 no data connection"); continue
                if m == "rst-data":
                    d.sendall(bytes(random.getrandbits(8) for _ in range(10000)))
                    d.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\x01\x00\x00\x00\x00\x00\x00\x00")
                    d.close(); self.say("426 connection reset by the adversary"); continue
                d.sendall(b"x" * 400); d.close(); self.say("226 done")
            elif verb == "STOR":
                if m == "retr-550": self.say("553 not allowed here, says the adversary"); continue
                self.say("150 go ahead")
                d = self.data_conn()
                if not d: self.say("425 no data connection"); continue
                n = 0
                while True:
                    b = d.recv(4096)
                    if not b: break
                    n += len(b)
                d.close(); self.say(f"226 got {n} bytes")
            else:
                self.say("502 not here")
        try: self.c.close()
        except OSError: pass

def listen(port):
    ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("0.0.0.0", port)); ls.listen(5)
    while True:
        c, _ = ls.accept(); Session(c, port).start()

if __name__ == "__main__":
    print("advertising", ADV_IP)
    for p, name in SCENARIOS.items():
        threading.Thread(target=listen, args=(p,), daemon=True).start(); print(f"  {p} {name}")
    while True: time.sleep(3600)
