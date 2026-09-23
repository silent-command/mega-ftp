/* Bookmarks: FTPC.CFG on the disk the client booted from, as the gopher
 * client keeps its list, so the file travels with the program. A line
 * "FTPC1", then four lines per entry: host, port, user, directory. No
 * passwords: the disk is also the distribution medium. The text stays in
 * bank 1 above the font copy, which mega-net leaves to the program, and
 * is read field by field into the caller's buffers; the client's memory
 * below $C900 is nearly full (REQUIREMENTS.md 5.10). */
#ifndef MARKS_H
#define MARKS_H

#define MARKS_MAX 8
#define MARKS_NONE 0xff

extern unsigned char marks_count;
/* Scratch for one entry's fields, shared with the host screen's list. */
extern char marks_h[64], marks_p[6], marks_u[32], marks_d[80];

void marks_load(unsigned char drive);
/* Entry i into the four buffers (host 64, port 6, user 32, path 80); 1 if it exists. */
unsigned char marks_get(unsigned char i, char *host, char *port_s, char *user, char *path);
/* The index of the entry with these four fields, or MARKS_NONE. */
unsigned char marks_find(const char *host, const char *port_s, const char *user, const char *path);
/* Appends and writes the file; 0 when the list is full or the write failed. */
unsigned char marks_add(const char *host, const char *port_s, const char *user, const char *path);
/* Removes entry i and writes the file; 0 when the write failed. */
unsigned char marks_remove(unsigned char i);

#endif
