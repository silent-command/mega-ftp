/* Step 2: the control connection and a passive listing. Connects to the
 * test server on the Mac, logs in anonymously, PWD, then LIST over a
 * passive connection and shows the lines. REQUIREMENTS.md 5.2. */
#include <string.h>
#include "mega65/memory.h"
#include "meganet.h"
#include "m65_screen.h"
#include "m65_boot.h"
#include "ftp.h"

static const unsigned char server[4] = { 192, 168, 1, 232 };
static char text[90];
static unsigned char buf[512];

static void say(unsigned char row, const char *a, const char *b)
{
  strcpy(text, a); if (b) strncat(text, b, 79 - strlen(text)); m65_putsxy(0, row, text);
}
static void put_u16(char *p, unsigned int v)
{ char t[6]; unsigned char n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *p++ = t[--n]; *p = 0; }

int main(void)
{
  const char *err; unsigned char row = 0, done, i;
  unsigned int n, total = 0, frames = 0; unsigned char last;
  char num[8];

  mega65_io_enable();
  m65_screen_init();
  if (!m65_boot_load(&err)) { say(row, "stack: ", err); for (;;) ; }
  meganet_call(MEGANET_INIT, 0, 0, 0, 0);
  meganet_dhcp_start();
  last = PEEK(0xd7fa);
  while (meganet_dhcp_state() != MEGANET_DHCP_BOUND) { meganet_poll(); if (PEEK(0xd7fa) != last) { last = PEEK(0xd7fa); if (++frames > 1500) { say(row, "no dhcp", 0); for (;;) ; } } }
  say(row++, "online", 0);

  if (!ftp_connect(server, 2121)) { put_u16(num, ftp_error); say(row++, "connect failed, error ", num); goto stop; }
  say(row++, "220 ", ftp_reply);
  if (!ftp_login("", "")) { say(row++, "login: ", ftp_reply); goto stop; }
  say(row++, "logged in: ", ftp_reply);
  if (ftp_command("PWD")) say(row++, "PWD: ", ftp_reply);
  if (!ftp_pasv()) { put_u16(num, ftp_error); say(row++, "PASV failed, error ", num); goto stop; }
  say(row++, "passive data connection open", 0);
  if (!ftp_start_transfer("LIST", 0)) { say(row++, "LIST: ", ftp_reply); goto stop; }
  {
    static char lst[900];
    unsigned int lp = 0;
    do {
      n = ftp_data_read(buf, sizeof buf, &done);
      for (i = 0; i < n; i++) if (lp < sizeof lst - 1) lst[lp++] = (char)buf[i];
      total += n;
      if (ftp_frames > FTP_TIMEOUT_FRAMES) break;
    } while (!done);
    ftp_finish_transfer();
    ftp_quit();
    /* Only now paint: the transfer is over, nothing is polling. */
    m65_screen_init();
    {
      unsigned char col = 0, lrow = 0; unsigned int j;
      put_u16(num, total); say(lrow++, "listing, bytes: ", num);
      for (j = 0; j < lp; j++) {
        char c = lst[j];
        if (c == '\r') continue;
        if (c == '\n') { text[col] = 0; if (lrow < 24) m65_putsxy(0, lrow++, text); col = 0; continue; }
        if (col < 78) text[col++] = c;
      }
    }
  }
stop:
  for (;;) ftp_poll();
}
