# mega-ftp

An FTP client for the MEGA65. Browse a server's directories, download
files to a disk, upload files from a disk or the SD card, and bookmark
the servers you use. It uses passive mode, so it works from behind a
home router.

## Starting

Mount `FTPC.D81` and type `RUN "FTPC"`, or choose FTP from the
[Net-Tools](https://github.com/silent-command/mega-menu) menu. It needs a
network cable.

## Keys

At the Host prompt (then Port, User and Password):

| Key | Does |
|---|---|
| RETURN | accept; Port defaults to 21, User to anonymous |
| a number | open that bookmark |
| D and a number | delete that bookmark |
| RUN/STOP | quit to BASIC (at Host); abandon the login (at the others) |

In the file list:

| Key | Does |
|---|---|
| CRSR up / down | move |
| CRSR left / right | previous / next page |
| CLR/HOME | first entry |
| RETURN | open a directory, or download a file |
| G | download the file under the cursor |
| P | upload a file to this directory |
| U or INST/DEL | up one directory |
| R | list again |
| D | choose the drive: 8, 9, or a `.D81` on the SD card |
| M | bookmark this server and directory, or remove the bookmark |
| MEGA+F / MEGA+B | change the text / background colour |
| HELP or H | close the connection, back to the Host prompt |
| RUN/STOP | up one directory; at the top, back to the Host prompt |

RUN/STOP cancels any prompt and any transfer in progress.

## Credits

Created by Justin Cameron. Apache License 2.0; see [LICENSE](LICENSE)
and [NOTICE](NOTICE).
