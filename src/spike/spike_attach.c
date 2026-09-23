/* Step 1: can the client put a disk image from the SD card onto drive 9
 * and write a file to it? Prints the Hyppo version, attaches FTPTEST.D81
 * (a blank image put on the card for this), lists it, writes FTPTEST to
 * it, lists again. An image already on the other drive is refused with
 * $8F, which is how the first run went. Status at $1400. REQUIREMENTS.md 5.1. */
#include <string.h>
#include "mega65/memory.h"
#include "m65_screen.h"
#include "m65_cbmdos.h"
#include "m65_hyppo.h"

#define ST(o) (*(volatile unsigned char *)(0x1400 + (o)))
static char line[90];

static void put_u8(char *p, unsigned char v)
{
  char tmp[4]; unsigned char n = 0;
  do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) *p++ = tmp[--n];
  *p = 0;
}

static void list(unsigned char drive, unsigned char row)
{
  unsigned char i, r;
  char entry[40];
  if (cbmdos_dir_open(drive) != CBMDOS_OK) { m65_putsxy(0, row, "  (directory unreadable)"); return; }
  for (i = 0; i < 8; i++) {
    r = cbmdos_dir_get(i, entry);              /* 1 = an entry, 0 = an empty slot */
    if (!r) { if (i == 0) m65_putsxy(0, row, "  (empty)"); break; }
    strcpy(line, "  "); strcat(line, entry);
    m65_putsxy(0, (unsigned char)(row + i), line);
  }
}

int main(void)
{
  unsigned char a, b, c, d, how, r, i;
  unsigned char row = 0;

  mega65_io_enable();
  m65_screen_init();
  for (i = 0; i < 16; i++) ST(i) = 0;

  hyppo_version(&a, &b, &c, &d);
  strcpy(line, "Hyppo "); put_u8(line + 6, a); strcat(line, "."); put_u8(line + strlen(line), b);
  strcat(line, "  HDOS "); put_u8(line + strlen(line), c); strcat(line, "."); put_u8(line + strlen(line), d);
  m65_putsxy(0, row++, line);
  ST(0) = a; ST(1) = b; ST(2) = c; ST(3) = d;

  r = hyppo_attach(1, "FTPTEST.D81", &how);
  ST(4) = r; ST(5) = how;
  if (!r) { strcpy(line, "attach failed, error "); put_u8(line + strlen(line), how); m65_putsxy(0, row++, line); goto done; }
  strcpy(line, "FTPTEST.D81 attached to drive 9 via Hyppo 1."); put_u8(line + strlen(line), (unsigned char)(how - 10));
  m65_putsxy(0, row++, line);
  m65_putsxy(0, row++, "drive 9 before:"); list(1, row); row += 9;

  r = cbmdos_create("FTPTEST", 1);
  if (r == CBMDOS_ERR_EXISTS) { cbmdos_delete("FTPTEST", 1); r = cbmdos_create("FTPTEST", 1); }
  ST(6) = r;
  if (r == CBMDOS_OK) {
    const char *msg = "written by mega-ftp spike_attach\r";
    unsigned char k;
    for (k = 0; k < 3; k++) for (i = 0; msg[i]; i++) cbmdos_put((unsigned char)msg[i]);
    r = cbmdos_close(); ST(7) = r;
  }
  strcpy(line, "write FTPTEST: result "); put_u8(line + strlen(line), r); m65_putsxy(0, row++, line);
  m65_putsxy(0, row++, "drive 9 after:"); list(1, row); row += 9;
done:
  ST(15) = 'E';
  for (;;) ;
}
