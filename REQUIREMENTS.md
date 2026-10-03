# mega-ftp: requirements, decisions, and record

## 1. Goal

An FTP client for the MEGA65 that a person uses from the keyboard: connect
to a server, walk its directories, download files to a disk of their
choosing, upload files from a disk. The first real application on
mega-net after the gopher client, and the one meant to exercise the
stack's sockets and sender under sustained use.

## 2. Decisions

| Decision | Choice, and why |
|---|---|
| **Transfer mode** | Passive only. Active mode needs the server to open a connection back to the MEGA65, which a home router will not pass. |
| **Shape** | A browser, in the gopher client's style: a listing you move through with the cursor, Return to enter a directory, `G` to get, `P` to put, `D` to choose the drive. Not a command line. |
| **Where downloads go** | A drive, 8 or 9, holding a disk image: the one mounted now, or any image on the SD card attached by name at runtime through Hyppo. Files are written through the CBM DOS layer the gopher client proved. |
| **Not offered** | The real 3.5" floppy: the F011 driver reaches it, but reads from physical media stayed intermittent after two real bugs were fixed (gopher 2.48), and a half-working save is worse than none. The SD card's FAT filesystem directly: Hyppo cannot create files (`hyppo_mkfile` is not implemented), only write into existing ones sector by sector. |
| **Uploads come from** | A drive holding an image, or any file on the SD card, which Hyppo can read. |
| **Network** | mega-net's socket 0 for the control connection, socket 1 for each data connection. Anonymous login by default; a user name and password can be typed. Binary mode for everything. |
| **Platform code** | The gopher client's screen, F011, CBM DOS and boot modules, copied and renamed `m65_*`. Same license; a copy rather than a dependency, because the two programs will diverge. |
| **Build** | `build.py`, Python, all three development hosts (the mega-net rule). It builds mega65-libc and mega-net if they are missing. |
| **Testing** | Public anonymous servers for reading; a local server on the development machine for uploads and for adversarial cases; every claim verified on the MEGA65. |

## 3. Requirements

- **F-1** Connect to a named or numbered host, log in (anonymous or given).
- **F-2** List the current directory; enter directories; go up.
- **F-3** Download a file to the chosen drive, as PRG or SEQ, showing progress.
- **F-4** Upload a file from a drive or from the SD card.
- **F-5** Choose the destination drive, including attaching another disk image by name.
- **F-6** Every failure explained in words on screen; nothing hangs.
- **N-1** No wait unbounded; the frame counter paces everything.
- **N-2** Runs from one `.d81` carrying the client and `MEGANET`.

## 4. Plan

| Step | What | Status |
|---|---|---|
| 1 | Project skeleton; platform modules; a spike that attaches a disk image by name through Hyppo and writes a file to it | **Done** — 5.1 |
| 2 | Control connection: connect, log in, `PWD`, `LIST` over a passive data connection, on screen | **Done** — 5.2 |
| 3 | The browser: listing, cursor, directories | **Done** — 5.3 |
| 4 | `RETR` to a drive, with the drive chooser and progress | **Done** — 5.4 |
| 5 | `STOR` from a drive and from the SD card | **Done** — 5.5 |
| 6 | The long run: a large transfer both ways, and the adversarial servers | **Done** — 5.6 |

## 5. Findings

### 5.1 A disk image from the SD card, attached by name, written to (2026-09-04)

`src/spike/spike_attach.c`, through `src/platform/m65_hyppo.*`: the
machine reports Hyppo 1.2 and HDOS 1.3, and the 1.3 `attach` service
(`$4A`, drive in X) is present and works; the helper falls back to the
1.2 pair if it ever isn't. A blank `FTPTEST.D81` put on the SD card was
attached to drive 9, `FTPTEST` written to it through the CBM DOS module,
and the image fetched back to the Mac shows `ftptest seq`, one block.
So the save-target design holds: the client's own disk stays on drive 8
and any image on the card can be the destination.

Two things learned on the way. An image already on the other drive is
refused with error `$8F` ("double attach"): the reset re-applies the
auto-mount, so the last disk `MOUNT`ed from BASIC was on drive 8 when
the spike asked for it on drive 9. The client's drive chooser must show
that as "already mounted on drive 8", not as a failure. And the copied
directory reader returns 1 for an entry and 0 for an empty slot, the
opposite of the module's other calls, which return 0 for success; the
spike had it backwards for one run.
### 5.2 The control connection and a passive listing (2026-09-04)

`src/ftp.c` (the protocol) and `src/spike/spike_ftp.c`: against a
`pyftpdlib` server on the Mac, the client connects on socket 0, logs in
anonymously, reads `PWD`, opens a passive data connection on socket 1
from the `227` reply's address and port, and lists the root directory:
three entries, 195 bytes, shown on screen. `tools/ftp_test_server.py`
is the server; `build/venv` holds pyftpdlib.

**The bug worth recording: a transfer command's reply is 1xx, not 2xx.**
`LIST`, `RETR` and `STOR` are answered with a `150`/`125` preliminary
reply while the data flows and a `226` completion after the data
connection closes. The first `ftp_start_transfer` sent the command
through `ftp_command`, which accepts only the `2xx`/`3xx` a normal
command gets and rejected the `150`, so the transfer never ran.
`ftp_start_transfer` now sends the line itself and accepts class 1.

**Two process notes, both time sinks.** Diagnostics were first written
to `$1400`, which is `SCR_MENU_LINE` in the platform scratch map: the
CBM DOS and screen modules overwrite it, so every status byte read back
as garbage and sent the hunt in the wrong direction for an hour.
Diagnostics moved to `$C000`. And `mega65_ftp put` over an existing file
did not reliably replace it; a stale `SPIKE.D81` meant several "runs"
were of an older build showing a frozen screen. The fix, and the rule:
`del` then `put`, and put a visible version marker in the program so the
screen proves which build is running.

### 5.3 The browser (2026-09-05)

`src/ftpc.c` connects, logs in, lists, and moves a highlighted cursor
through the listing: RETURN opens a directory, U (or INST/DEL) goes up,
cursor left/right page, HOME goes to the top, R refreshes, H picks
another host, Q leaves. `src/dirlist.c` parses each `LIST` line, Unix or
DOS style, into a name, a kind and a size, and sorts directories first.
`src/ui.c` reads the keyboard queue at `$D610` without blocking and
polls the stack while waiting, so a session stays alive while the user
reads; a `NOOP` goes out after two idle minutes, and a session the
server drops is reopened by the next command (`ensure_session`). Seen
on hardware against the test server: the three-entry root, `docs`
entered and left again, all rows where they belong.

**A zero-page zeroing, seen once, not reproduced.** On the first run
every prompt landed on row 0. The zero-page dump showed conio's screen
width and height (`$45`/`$46`, placed in zero page by LTO) both zero,
with the neighbouring `colours_known` byte also zero although
`m65_screen_init` sets it to 1, so a run of at least three bytes was
zeroed after screen init and before the host prompt. Snapshots of zero
page at seven stages of start-up, in the same build and again in a
byte-identical reconstruction of it, never showed the bytes change; ten
further starts with a canary (`ui_check`, which records the first moment
the width is not 80 at `$C900` with a phase code, the frame counter and
a zero-page copy) never tripped. mega-net's contract is that it never
touches the caller's zero page, and its own zero-page use is `$B0`-`$D5`
inside the range its trampoline swaps. The event is recorded, the canary
stays in the build (two zero-page reads per poll), and if the symptom
recurs the record at `$C900` says when.

**The virtual keyboard holds a key.** `m65 -t` with a `~z` pause after
`~M` leaves RETURN pressed for the pause, and the keyboard auto-repeats
it, so one RETURN submitted two prompts. Two changes: the line editor no
longer flushes the queue on entry (a flush also ate keys typed ahead of a
prompt), and typing now replaces the offered default instead of
appending to it, which is friendlier at the keyboard as well. Driven
tests send one field per `m65 -T` call, so every key is released before
the next.

### 5.4 A file to a drive, and the drive chooser (2026-09-05)

`src/xfer.c`. G on a file asks for the name on the disk (suggested from
the server's name, upper case, sixteen characters, characters a
directory entry cannot hold replaced) and the type (SEQ unless the name
ends `.PRG`, S or P overrides), opens it through the CBM DOS writer,
then `PASV` + `RETR` and every byte into `cbmdos_put`, with a progress
line every 2 KB, a spinner, RUN/STOP to cancel (partial file removed),
an inactivity timeout, and a summary line with bytes and blocks. A name
already on the disk asks before overwriting. D sets where downloads go:
`8`, `9`, or the name of a `.D81` on the SD card, which is attached to
unit 9 through Hyppo (`hyppo_attach`, the 1.3 service on this machine)
and remembered on the info row.

Seen on hardware, one driven session: `hello.txt` to unit 8 (the
client's own disk), then `FTPTEST.D81` attached to unit 9 by name and
`pattern.bin` written there. Both images pulled back from the SD card
and both files extracted by following their block chains: 400 and 20000
bytes, identical to the originals. The unit 9 write succeeded on the
first attempt after the attach with no reset in between, which is what
5.1 was for. c1541 cannot read a file whose name contains a dot ("invalid
filename"), so the extraction is a Python chain-follower in the test
notes, not c1541.

### 5.5 A file to the server, from a disk or from the SD card (2026-09-05)

`xfer_put` in `src/xfer.c`. P asks where from: `8` or `9` lists that
disk's directory in the listing rows (a second cursor picker, RETURN
sends the file under the cursor) and reads it through a new streaming
reader in the CBM DOS module (`cbmdos_open_read`, `cbmdos_read_next`,
one block of up to 254 bytes per call, the mirror of create/put/close);
`SD` asks for a name on the SD card and reads it through mega65-libc's
`open`/`read512`/`close`, which wrap Hyppo's file services. Either way
the name to give it on the server is offered and editable, then `PASV`
+ `STOR` and `send_all`, which keeps pushing a chunk until the stack has
taken it all, with a spinner, RUN/STOP, an inactivity timeout and a
check that the data connection is still up. After a good transfer the
listing is refreshed so the new file shows.

Seen on hardware: `MEGANET` picked from unit 8 and sent as
`meganet.up`, 32210 bytes, byte-identical to `bin/meganet`; then
`PATTERN.BIN` from the SD card by name sent as `pattern.up`, 20000
bytes, identical to the original. The full directory walk
(`cbmdos_dir_first`/`next`/`end`) follows the chain past the first
block, which the old diagnostic reader did not.

**A compiler surprise, worth a rule.** The first picker showed every
file as its first letter: `F`, `M`. The directory block in memory was
right, the parsed `lp[]` array was right (`FTPC`, `MEGANET`, dumped from
the running machine), but the row buffer after `strcpy(text, "      ")`
+ `strcat(text, lp[idx].name)` held `" M"`: both calls had copied
exactly one byte. The identical pattern in the browser's `draw_entry`
(`strcpy(line, "[DIR] ")` + `strcat(line, folded)`) is fine, as are the
other strcpy/strcat pairs on the same `text` buffer in the same file.
Not chased into the LTO output; the row is now built with plain byte
loops (`put_str`/`put_pad`) and is correct. Rule: when a string built
by strcpy/strcat comes out wrong on the MEGA65 and the inputs are
verified, suspect the libcall simplification before the data, and
rewrite as loops.

**Test-side lessons.** The Mac's filesystem is case-insensitive: a
cleanup `rm PATTERN.BIN` deleted `pattern.bin`, and an upload named the
same as its source would silently overwrite the source and "compare
equal". Driven tests now send under distinct names (`*.up`). And
`mega65_ftp` refuses to run while a program is in memory, so images are
pulled back only after the client is left and the machine reset.

Memory: the listing model shrank from 100 x 69 to 64 x 53 bytes and the
get/put buffer is shared, after the picker and its buffer overflowed the
RAM below the stack by 2.3 KB. The program is now 33 KB with `.noinit`
ending at `$C6E7`.

### 5.6 The long run, the adversaries, and a public server (2026-09-05)

**300 KB each way.** A 300000-byte pseudo-random file (`big.bin`)
fetched from the test server onto `FTPTEST.D81` attached to unit 9 by
name: 1182 blocks, byte-identical when the image was pulled back and the
chain followed. Then the same file picked from unit 9 and sent back as
`big.up`: byte-identical. Each direction finished inside the few
seconds of scripted delay after the command, so the rate is at least
tens of KB/s including the disk I/O; a stopwatch run is a later nicety.

**Ten adversarial servers** (`tools/ftp_adversarial.py`, one port each),
all on hardware, all handled with the intended words on the status row:

| Port | Misbehaviour | The client said |
|---|---|---|
| 2205 | `421` banner then close | `connect: Service not available, try later.` |
| 2206 | accepts, never speaks | `connect: no reply (timed out)` after 30 s |
| 2209 | `227` with no numbers | `PASV: could not read the PASV reply` |
| 2201 | `150` then no listing data, ever | `LIST: no reply (timed out)` after 30 s |
| 2202 | listing dripped one byte per two seconds | the listing, complete, after about a minute (the timeout is inactivity, not total) |
| 2207 | 150 entries | `64 entries (listing truncated)   page 1 of 4` |
| 2208 | DOS lines, a name with spaces, a symlink, `total`, a blank, junk | 4 entries: `{DIR} Old Stuff`, `report v2.txt 4321`, `{LNK} link`, the 100-character name clipped to 47 |
| 2210 | `550` on RETR, `553` on STOR | `get: hello.txt: no such file, says the adversary` / `put: not allowed here, says the adversary` |
| 2203 | half a file, then RST on the data socket, `426` | `get: connection reset by the adversary`, and the partial file removed |
| 2204 | control connection closed after the first listing | `the server closed the connection; the next action reconnects`, and R relisted through a fresh session |

The RST case found a real gap: a reset data connection looks like an end
of file to the reader, and the first `xfer_get` announced a 10000-byte
file as saved. It now checks the server's completion reply and, when
the listing gave a size, the byte count, and removes a short file. The
100-character name shows the one known limitation: names longer than
`DL_NAME_MAX` (47) are clipped, so such a file cannot be fetched by its
displayed name.

**A public server, by name.** `ftp.gnu.org` resolved through mega-net's
DNS, listed (20 entries, seven directories first, the real vsftpd
format), and its `README` fetched onto unit 8: 2814 bytes, 12 blocks,
identical to the same file fetched by curl on the Mac.

**One unexplained connect.** The first attempt at the long run sat at
`connecting...` for over fifteen seconds and then fell back to the host
screen, against the same server and address every other run reached in
under a second; a retry from a clean reset connected at once. Not seen
again in the fourteen starts since. Recorded, like the zero-page event
in 5.3, as something to watch for.

### 5.7 Q now reaches BASIC (2026-09-05)

The user reported that Q printed "bye" and the machine went dead. It
had: this llvm-mos runtime's `exit()` is `jsr fini; bra .`, found by
disassembling `libcrt0.a` and by the serial monitor showing the CPU
parked on that branch. So returning from `main()` never reaches BASIC
on this toolchain, whatever an older runtime did for the gopher client.

The way out is `m65_exit_to_basic` in `src/platform/m65_exit.c`: hold
the 45E100 in reset, drain the keyboard queue, copy a 20-byte stub to
`$1FB0` (spare low RAM in the platform scratch map) and jump to it. The
stub clears the MAP, restores `$01` and `$D030` to the values the
runtime's `.fini` uses, and jumps through the KERNAL reset vector. The
ROM then re-initialises the screen editor, the DOS and BASIC's zero
page as at power-on, and Hyppo does not run again, so the disk mounted
from BASIC stays mounted. Measured: after Q the boot banner and
`READY.`, `PRINT 1+1` answers `2`, and `RUN "FTPC"` starts the client
with no `MOUNT` in between.

Two wrong turns on the way, both instructive. Restoring `$01`/`$D030`
in place, from code inside `$2000-$BFFF`, mapped the BASIC ROM over
the program's own code and parked the CPU at `$908D` in ROM: anything
that changes the map must run from outside the window, which is why
the stub lives at `$1FB0`. And calling the KERNAL's `CINT` (`$FF81`)
before returning was pointless, since the return itself never comes
back. The gopher client's "break after every line" (its REQUIREMENTS,
"Quit to BASIC 65") fits the same picture: the program's zero-page
sections at `$02-$8F` sit on BASIC's own variables, and only a
re-initialisation, not a return, puts them right. The same routine
would fix it there.

Applied to the gopher client the same day (its 0.3), with one
difference worth knowing here: the C version of this routine, merely
referenced from gopher's code, made that program's start-up crash into
zero page, with identical zero-page layouts in the booting and crashing
builds, so link-time code generation was reshaping the rest of the
program (the family of fault gopher records as 2.34 and this file as
the one-byte strcpy in 5.5). Gopher carries the routine as assembly,
`gopher_exit.S`, which LTO cannot touch. This client's C module works
and stays, but if start-up ever misbehaves after an unrelated change,
swapping in the assembly version is the first thing to try.

One more finding from the gopher port applies here. The C65 ROM enters
its machine-language monitor when RUN/STOP is held at reset, and a
software reset a few milliseconds after a keypress finds the key still
held, which is what gopher saw with RUN/STOP as its quit key. This
client's quit key is Q, and `m65_exit_to_basic` now also settles for
half a second (frame counter, interrupts still on) after quieting the
ethernet controller before it jumps, so a held key is released and an
event the controller had in flight is taken by the program's handler.
Verified on hardware after the change.

### 5.8 Colours (2026-09-05)

The user asked for the gopher client's colour handling. The screen
module already had it, being that client's: the border and background
are read once at start from what BASIC had set (`$D020`/`$D021`), the
text starts white (the ROM's current-colour byte at `$0286` does not
say what the screen shows, gopher's finding), and two routines cycle
the text colour, skipping the background, and the background with the
border, kept identical. Only the keys were missing: F cycles the text
colour and redraws the listing, since colour RAM keeps what was drawn
before; B cycles background and border, which the VIC shows at once.
The key row was tightened to fit ("R list", "F/B colour"), 76
columns. Seen on hardware: two presses of F and one of B, the screen
recoloured, the listing intact.

### 5.9 The unreadable screen after Q (2026-09-05)

The user reported that after Q BASIC took commands but nothing on the
screen could be read. The screenshot tool had hidden this: it reads
screen memory and renders the characters itself, so it showed the
banner and `READY.` while the real display showed nonsense. The proof
came from dumping the VIC's registers (`$D000-$D07F`) on a clean boot
and again after Q: one byte differed, `$D06A`, the bank of the
character-set pointer, 0 clean and 2 after. `conioinit()` points the
font at the ROM's copy in bank 2 and turns the VIC-IV hot registers
off, so the ROM's reset, which sets the font by writing the classic
`$D018`, no longer recomputes the pointer, and the display fetched its
glyphs from the wrong 64 KB. `m65_exit_to_basic` now sets `$D06A` to
0 and turns the hot registers back on before the jump; the dump after
Q then matches a clean boot in every register but the two raster
position counters. The same fix went into `gopher_exit.S`, checked the
same way. Rule for the record: a screenshot proves what is in screen
memory, not what is on the screen; for the picture, compare the video
registers.

Addendum: the order of the two writes matters. Turning the hot
registers on re-derives the character-set pointer from the classic
registers, so a bank write made before it can be undone; the gopher
client's menu screen showed exactly that (its 0.3.2). Hot registers
first, the bank last, and once more inside the stub as the final write
before the reset vector, here too.

### 5.10 Bookmarks, and the room for them (2026-09-06)

The gopher client's bookmarks, carried over. `FTPC.CFG` on the disk the
client booted from: a line `FTPC1`, then four lines per entry, host,
port, user, directory. No passwords, since the disk is also how the
program is shared. Up to eight. In the browser, M toggles a bookmark
for the host, port, user and the directory being viewed, as the gopher
client's M does for a page. The host screen lists them under the
prompts; a number typed at the Host prompt opens one (the port and
user fill in, only the password is asked, and after login the client
changes to the saved directory, or says so if it has gone); D and the
number deletes one. The text stays in bank 1 at `$11800`, above the
font copy, read field by field into the caller's buffers; the program
below `$C900`, where the ZPDBG canary lives, had no room for a parsed
list.

It had no room for the code either: the first build overflowed by
2 KB. The client was still compiled with -Os; -Oz, which the SSH client
uses throughout (and mega-net found in 5.17: 32-bit arithmetic
inflates -Os on this CPU), took the program from 2 KB over to 4 KB
free with nothing else changed. Kept.

On the machine: M in `/docs` bookmarked it, the host screen listed
`192.168.1.232:2121  anonymous  /docs`, `1` and the password opened
the session in `/docs` (its single README listed), M again removed it,
a bookmark of the root deleted with `d1`, and a bookmark made before a
reset was listed after it.

### 5.11 The keys made uniform with the gopher and SSH clients (2026-09-07)

One scheme for the three (ssh REQUIREMENTS.md 5.28): plain letters
belong to the content, RUN/STOP backs out one level at a time and
quits from the top, HELP is the way to the start screen from anywhere,
and the application's own functions take MEGA with a letter. Here:
RUN/STOP in the browser goes up a directory and, at the root, to the
host screen (it quit before); HELP goes to the host screen (H stays as
an alias); Q in the browser is gone; MEGA+M, MEGA+F and MEGA+B are
read alongside M, F and B, through `ui_last_mods`, the modifiers
captured before the key is popped; RUN/STOP at the Host prompt quits
at once rather than through a second prompt. Verified on the machine:
docs, RUN/STOP, the root, RUN/STOP, the host screen, RUN/STOP, READY.
The MEGA forms wait for a finger, as the typing tool cannot hold MEGA.

### 5.13 The selected row stopped being highlighted (2026-09-23)

The user found it: the listing could be navigated, but nothing on screen
said which entry was selected.

`ui_line_rev()` drew the row between conio's `revers(1)` and
`revers(0)`. That sets `ATTRIB_REVERSE` in conio's own `g_curTextColor`,
which only `cputs()` and `cputc()` read -- and this module stopped
drawing through them in 5.24, when a row became two DMA transfers, the
cells copied and the colour filled with the text colour. From that
commit the call set a flag nothing consulted. It has been broken in the
repository since 2026-09-22 21:47 and reached the user only today,
because that commit was newer than 0.4.3 and 0.4.4 was the first release
to carry it.

**The fix.** The shared screen module gains `m65_screen_reverse(on)`,
which the family's Gemini client has had all along, and `xlate()` ORs
`$80` into each screen code while it is set: the font installed at start
is 2048 bytes, all 256 glyphs, and the upper half is the reversed set.
`ui_line_rev()` calls that instead of conio's.

Measured on the machine against `tools/ftp_test_server.py`, by reading
screen RAM rather than looking at it. Six entries listed: exactly one
row carries 16 of 16 reversed cells, and it moves one row down per
cursor press -- row 2 (`[DIR] docs`), then row 3 (`alpha.txt`), then row
4 (`beta.txt`) -- with every other row at zero on all three passes.

**A second bug, found on the way, and worse than the first.** The ZPDBG
canary's `ui_check()` called conio's `getscreensize()`, which returns
the globals `conioinit()` fills in. Since the screen module stopped
calling `conioinit()` (irc 5.27) nothing set them, so they read zero,
the canary judged the screen wrong on the first row drawn, and copied
256 bytes of zero page to `$C910`. That is inside the program's own
region, in the gap the soft stack grows down into from `$D000`. It fires
once and the client kept running, but it is a write into live memory on
every run. The canary was watching a conio global that no longer
exists, so it, its seven call sites and ui.c's conio include are gone.
This client is now conio-free like its siblings, and 151 bytes smaller.

**An instrument nearly answered for the machine, again.** The first scan
reported `reversed rows: NONE` across all 25 rows. It had matched
nothing: the monitor marks a memory reply with **eight** hex digits
(`:00010000:`) and the pattern was built with `%07X`. Taken at face
value it would have said the fix failed. The scanner now prints every
row's text, not only the reversed ones, and exits non-zero when no row
reads at all. Third time in this project that a silent instrument came
close to being read as a result; the rule stands -- give every probe a
control whose answer is known.

### 5.12 M marks, and American spelling (2026-09-07)

Reported from a hand test: MEGA+M on the browser did nothing, and no
bookmark appeared. The plain M had always worked (5.10, 5.11: the
driver types it), so MEGA+M does not reach the client as `m`; what it
delivers is to be read from the key probe. M is the key, as it was,
and the key row says so; the MEGA forms of M, F and B stay accepted
for whoever holds the key. The screen and the documents spell color
the American way, which is the rule for user-visible text from now on.
Version 0.4.1.

Measured the same day (ssh 5.29): MEGA with a letter arrives as the
capital with bit 7 set, which is why MEGA+M did nothing. The browser
masks the bit off when MEGA is held, so the MEGA forms work too; M
stays the documented key. Version 0.4.2.


The shared disk loader handed an empty file's zero length to lcopy, a
64 KB copy (ssh 5.30); guarded here too, 2026-09-07. Version 0.4.3.
