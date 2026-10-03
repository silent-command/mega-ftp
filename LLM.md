# mega-ftp

An FTP client for the MEGA65 on mega-net: passive mode, anonymous or
named login, a directory browser, get and put through attic RAM to a
chosen drive, bookmarks in `FTPC.CFG`. Native mode, llvm-mos, 80x25,
`-Oz`. Version in `src/ftpc.c` (`FTPC_VERSION`). 0BSD. Releases carry
`bin/FTPC.D81`.

The platform modules under `src/platform/` (`m65_*`: screen, font,
F011, CBM DOS, hyppo, boot, scratch, exit) are shared with
`../mega-gopher` (their origin) and `../mega-ssh` (which copied them from here).
A fix to one belongs in all three.

## Read first

1. This file.
2. `../mega-net/docs/PLATFORM.md`: the machine, the family's memory
   map, the traps, the tools, the test driver.
3. `REQUIREMENTS.md`: section 2 decisions, section 4 the plan, section
   5 the findings, 5.1 to 5.13.

## Commands

```
python3 build.py          the client and bin/FTPC.D81
python3 build.py spike    the spikes under src/spike/
build/venv/bin/python tools/ftp_test_server.py DIR 2121   a local server (pyftpdlib) serving DIR, read and write
build/venv/bin/python tools/ftp_adversarial.py            the ten adversarial servers
```

On the machine: the disks live in `net-tools` on the card and **nothing
mounts from a subdirectory** (2026-09-23), so start it from the
Freezer's disk-image browser, which does walk directories. With the
driver, `boot_prg` stages a copy at the root for the run; call
`unstage_d81 ftpc.d81` when finished:

```
source ../mega-net/tools/m65lib.sh
python3 tools/deploy.py                            FTPC.D81 onto the card, carrying FTPC.CFG (the bookmarks) over; the only way to redeploy
boot_prg ftpc.d81 ftpc 'ost:'                      the host screen ("Host:" renders as "?OST:")
type_line '192.168.1.232'; type_line '2121'; type_line 'anonymous'; type_line 'x'
wait_for 'entr' 20                                 "N entries" or "1 entry"
type_keys '~C'                                     back one level: up a directory, then the host screen, then BASIC
```

Never a plain `put_d81`: it replaces `FTPC.CFG` with an empty list and
the user's bookmarks are gone. `tools/deploy.py` carries the file across
and keeps the card's old disk under `build/deploy` first, the way the
SSH and NTP clients have done since one of them lost its settings that
way (mega-ntp 5.3). This client asked the reader to do that by hand for
a long time and now does it itself.

## What is mine in memory

Bank 1 `$11800-$11FFF`: the bookmark text (5.10), above the font copy
at `$11000`. Transfers pass through attic RAM. The screen is at
`$10000`, where the shared module puts it, not `$0800`. The disk
layer's sector buffer and BAM copy are at `$1100` and `$1300`
(`F011_BUF_AT`, `BAM2_AT`), which is safe only because this client
leaves through the ROM's reset. The program ends near `$C000`; the
ZPDBG canary that sat at `$C900` is gone, and with it the last of
conio (5.13). Everything else is in `PLATFORM.md`'s table.

## Rules

- Every wait is bounded on `$D7FA`; poll mega-net from every loop; the
  peer closing is not the end of the data: drain, then close.
- Confirm which drive a disk test addressed, by the disk's name, before
  believing the result; a reset re-applies the auto-mount.
- Test with a built program on disk, never by typing through `m65 -T`
  (it drops lines); one field per call.
- Screen codes are written directly through `src/platform/m65_screen.c`,
  a row at a time by DMA; conio is not linked at all. Its `revers()`
  therefore does nothing here -- use `m65_screen_reverse()`, which draws
  from the font's reversed half (5.13).
- The exit is a software reset from the stub at `$1FB0`: hot registers
  on, then the font pointer `$D068-$D06A` back to the ROM's set, the
  bank byte once more inside the stub (5.9, ssh 5.25).
- The build is `-Oz`; `-Os` overflowed by 2 KB (5.10).
- Record every hardware finding in `REQUIREMENTS.md`, numbered.
- Commits are local until the user asks for a push or a release. A
  release bumps `FTPC_VERSION`, rebuilds, tags `vX.Y.Z`, and attaches
  `FTPC.D81` with a short note ending in how to run it.

## Not to reopen

Passive mode only. One control and one data socket. Bookmarks without
passwords (the disk is the distribution medium). Uploads pick a file
from unit 8 or 9 or a name on the SD card through hyppo, which is
read-only for the FAT side.
