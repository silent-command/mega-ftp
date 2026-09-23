/* The current directory listing: one entry per LIST line the server sent,
 * parsed into a name, a kind and a size. Filled during the transfer and
 * painted afterwards, so nothing writes the screen while the stack is
 * being polled (the rule carried from the gopher client). */
#ifndef DIRLIST_H
#define DIRLIST_H

/* 64 entries of 53 bytes: the biggest thing in the program. 100 x 69
 * plus the upload picker overflowed the ~44 KB below the stack. */
#define DL_MAX 64           /* entries kept; a longer listing is truncated */
#define DL_NAME_MAX 47      /* the name as the server wrote it, for CWD/RETR */

#define DL_FILE 0
#define DL_DIR 1
#define DL_LINK 2           /* a symlink: opened as a directory first, fetched if that fails */

struct dl_entry {
  unsigned char kind;
  unsigned long size;
  char name[DL_NAME_MAX + 1];
};

extern struct dl_entry dl_entries[DL_MAX];
extern unsigned char dl_count;
extern unsigned char dl_overflow;   /* set when lines were dropped */

void dl_clear(void);

/* Parses one line of LIST output, Unix ("drwxr-xr-x 2 u g 4096 Sep 3 12:00 name")
 * or DOS ("09-03-26  12:00PM  <DIR>  name") style, and appends it.
 * Lines it cannot read ("total 12", blanks) are ignored. */
void dl_add_line(const char *line);

/* Directories first, each group in the server's order. */
void dl_sort(void);

#endif
