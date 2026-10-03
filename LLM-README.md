# mega-ftp

An FTP client for the [MEGA65](https://mega65.org), written in C for
llvm-mos, running in native MEGA65 mode on [mega-net](../mega-net).

**Status: working.** Browsing, downloading, uploading and bookmarks are verified on hardware, including a 300 KB transfer each way, ten adversarial servers and a public server by name (REQUIREMENTS.md 5.6). See
`REQUIREMENTS.md` for the decisions, the plan and what was seen on
hardware.

## Using it

`bin/FTPC.D81` holds the client and mega-net. Mount it, `RUN "FTPC"`,
and give a host, a port, a user name and a password (RETURN keeps the
defaults: port 21, anonymous). The client uses passive mode, so it works
behind an ordinary home router.

The border and background are whatever you had set in BASIC before
running; the text starts white. F and B change them while the client
runs, as in the gopher client.

In the listing:

| Key | Does |
|---|---|
| cursor up/down, left/right, HOME | move; page; top |
| RETURN | open a directory |
| U or INST/DEL | up one directory |
| G | get the file under the cursor onto the chosen drive |
| P | put a file: pick one from unit 8 or 9, or name one on the SD card, and send it to the current directory |
| D | choose the drive: `8`, `9`, or the name of a `.D81` on the SD card, which is attached to unit 9 |
| F | next text color |
| B | next background and border color (the first press: black) |
| R | list again |
| M | bookmark this host, user and directory, or remove the bookmark if it is one |
| RUN/STOP | back one level: up a directory; at the root, the host screen |
| HELP, or H | the host screen, from anywhere |

On the host screen RUN/STOP goes back a prompt, and from the Host prompt
leaves for BASIC's READY, the disk still mounted, so `RUN "FTPC"` starts
it again. The same keys mean the same in the gopher and SSH clients.

A download asks for the name on the disk and whether it is SEQ or PRG,
shows progress, and RUN/STOP cancels it. Files are written straight into
the disk image, so a `.D81` anywhere on the SD card can be a download
target: press D and type its name.

An upload asks where from (`8`, `9`, or `SD`), shows the disk's directory to pick from (or asks for the name on the SD card), then the name to give it on the server.

## Building

Needs [llvm-mos](https://llvm-mos.org), CMake, `c1541` from VICE, Python 3,
and two sibling checkouts: `../mega-net` and
`../mega65-libc` (github.com/MEGA65/mega65-libc).

```
python3 build.py
```

## License

0BSD, see `LICENSE`. mega-net is a separate project under the same
license; mega65-libc is under its own. The screen, F011 and CBM DOS
modules under `src/platform/` began life in the MEGA65 gopher client,
under the same license.

## Bookmarks

M in the browser bookmarks the host, port, user and the directory
being viewed, or removes it if it is already bookmarked. The host
screen lists them; a number at the Host prompt opens one, D and the
number deletes it. They live in `FTPC.CFG` on the program disk, without
passwords, eight at most (REQUIREMENTS.md 5.10).
