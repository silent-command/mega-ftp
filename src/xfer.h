/* Moving files between the server and a drive: the drive chooser, RETR
 * to a disk, STOR from one. Prompts use the info and status rows; the
 * caller redraws them afterwards. */
#ifndef XFER_H
#define XFER_H

#include "dirlist.h"

/* Where downloads go: F011 drive 0 (unit 8) or 1 (unit 9). */
extern unsigned char xfer_drive;
/* The image attached to unit 9 by name, or empty. */
extern char xfer_image[32];

/* The D key. Returns 1 if the choice changed. */
unsigned char xfer_choose_drive(void);

/* Text for the info row: "save to unit 9 (NAME.D81)". */
void xfer_drive_text(char *out);

/* The G key: fetches `e` onto the chosen drive. Returns 1 when a file
 * was written; the status row says what happened either way. */
unsigned char xfer_get(const struct dl_entry *e);

/* The P key: sends a file from unit 8 or 9 (picked from that disk's
 * directory) or from the SD card (by name) to the current directory on
 * the server. Uses the listing rows for the picker; the caller redraws
 * its page afterwards. Returns 1 when the server confirmed the file. */
unsigned char xfer_put(void);

#endif
