#!/usr/bin/env python3
"""Puts bin/FTPC.D81 into net-tools on the MEGA65's SD card, keeping the FTPC.CFG the
client wrote on the disk already there (the bookmarks: host, port, user
and directory). Replacing the disk image outright loses them, and this
file said so for a long time without offering a way not to -- the SSH
and NTP clients have had one of these since their own settings were
lost once (mega-ntp REQUIREMENTS.md 5.3).

    python3 tools/deploy.py [--port /dev/cu.usbserial-23201] [image.d81]

Needs m65 and mega65_ftp from mega65-tools on the PATH (with or without
the .osx suffix) and c1541 from VICE. The machine is reset first, as
mega65_ftp needs; the card's disk is kept under build/deploy before it
is replaced, so a redeploy is undoable even if the carrying goes wrong.
"""
import os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DISK = "FTPC.D81"
KEEP = ["ftpc.cfg"]                    # c1541 wants lowercase; the client writes it upper-cased


def tool(name):
    for cand in (name, name + ".osx"):
        if shutil.which(cand):
            return cand
    sys.exit(f"{name} not found on the PATH")


def run(args, check=True):
    r = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if check and r.returncode:
        sys.exit(f"{' '.join(str(a) for a in args)}\n{r.stdout}{r.stderr}")
    return r


def main():
    args = sys.argv[1:]
    port = os.environ.get("MEGA65_PORT", "/dev/cu.usbserial-23201")
    if "--port" in args:
        i = args.index("--port"); port = args[i + 1]; del args[i:i + 2]
    image = Path(args[0]) if args else ROOT / "bin" / DISK
    if not image.exists():
        sys.exit(f"{image} not found: build first")
    m65, ftp, c1541 = tool("m65"), tool("mega65_ftp"), tool("c1541")
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        old, new = td / "old.d81", td / DISK
        shutil.copy(image, new)
        run([m65, "-F"], check=False); time.sleep(2)              # reset: mega65_ftp refuses a running program
        run([ftp, "-l", port, "-c", "cd net-tools", "-c", f"get {DISK} {old}"], check=False)
        kept, absent = [], []
        if old.exists() and old.stat().st_size == 819200:
            keep_dir = ROOT / "build" / "deploy"; keep_dir.mkdir(parents=True, exist_ok=True)
            backup = keep_dir / time.strftime("card-%Y%m%d-%H%M%S.d81")
            shutil.copy(old, backup)
            for name in KEEP:
                out = td / name
                run([c1541, old, "-read", f"{name},s", out], check=False)
                if out.exists() and out.stat().st_size:
                    run([c1541, new, "-delete", name], check=False)
                    run([c1541, new, "-write", out, f"{name},s"])
                    kept.append(name)
                else:
                    absent.append(name)
            print("the card's disk is kept as", backup.relative_to(ROOT))
            if absent:
                print("not on the card's disk, so not carried:", ", ".join(absent))
        else:
            print(f"no {DISK} on the card yet, nothing to keep")
        r = run([ftp, "-l", port, "-c", "cd net-tools", "-c", f"del {DISK}", "-c", f"put {new} {DISK}"], check=False)
        if "in " not in r.stdout and "bytes" not in r.stdout:
            sys.exit(f"the upload did not report success:\n{r.stdout}{r.stderr}")
        print("deployed", image.name, "keeping", ", ".join(kept) if kept else "nothing")


if __name__ == "__main__":
    main()
