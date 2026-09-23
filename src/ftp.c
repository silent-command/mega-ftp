#include <string.h>
#include "mega65/memory.h"
#include "meganet.h"
#include "ftp.h"
#include "ui.h"

unsigned int ftp_code;
char ftp_reply[FTP_REPLY_MAX];
unsigned char ftp_error;
unsigned int ftp_frames;

static unsigned char last_frame;
static unsigned char inbuf[256];        /* control bytes not yet consumed */
static unsigned char in_len, in_pos;
static char line[FTP_REPLY_MAX];
static unsigned char line_len;
static unsigned char data_done;

void ftp_poll(void)
{
  meganet_poll();
  if (PEEK(0xd7fa) != last_frame) { last_frame = PEEK(0xd7fa); ftp_frames++; }
}

/* One byte of the control connection, or -1 if none is waiting. */
static int ctrl_byte(void)
{
  if (in_pos >= in_len) {
    in_len = (unsigned char)meganet_tcp_recv_s(FTP_CTRL_SOCK, inbuf, sizeof inbuf);
    in_pos = 0;
    if (in_len == 0) return -1;
  }
  return inbuf[in_pos++];
}

/* Waits for a complete reply: lines until one begins "ddd " (a space
 * after the code; "ddd-" is a continuation). Bounded. */
static unsigned char wait_reply(void)
{
  unsigned char fl, st;
  int c;
  ftp_code = 0; line_len = 0; ftp_frames = 0;
  for (;;) {
    ftp_poll();
    st = meganet_tcp_state_s(FTP_CTRL_SOCK, &fl, 0);
    while ((c = ctrl_byte()) >= 0) {
      if (c == '\r') continue;
      if (c != '\n') { if (line_len < FTP_REPLY_MAX - 1) line[line_len++] = (char)c; continue; }
      line[line_len] = 0;
      if (line_len >= 4 && line[0] >= '0' && line[0] <= '9' && line[1] >= '0' && line[1] <= '9' &&
          line[2] >= '0' && line[2] <= '9' && line[3] == ' ') {
        ftp_code = (unsigned int)((line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0'));
        strcpy(ftp_reply, line + 4);
        return 1;
      }
      line_len = 0;                                        /* a continuation line */
    }
    if (st == MEGANET_TCP_CLOSED || (fl & (MEGANET_TCP_F_RESET | MEGANET_TCP_F_EOF))) { ftp_error = FTP_E_RESET; return 0; }
    if (ftp_frames > FTP_TIMEOUT_FRAMES) { ftp_error = FTP_E_NOREPLY; return 0; }
  }
}

static unsigned char wait_connected(unsigned char sock)
{
  unsigned char fl, st;
  ftp_frames = 0;
  for (;;) {
    ftp_poll();
    st = meganet_tcp_state_s(sock, &fl, 0);
    if (st == MEGANET_TCP_ESTABLISHED) return 1;
    if (st == MEGANET_TCP_CLOSED) { ftp_error = (fl & MEGANET_TCP_F_REFUSED) ? FTP_E_REFUSED : FTP_E_NOREPLY; return 0; }
    if (ftp_frames > FTP_TIMEOUT_FRAMES) { meganet_tcp_abort_s(sock); ftp_error = FTP_E_NOREPLY; return 0; }
  }
}

unsigned char ftp_connect(const unsigned char *ip4, unsigned int port)
{
  unsigned char i;
  ftp_error = FTP_E_NONE; in_len = in_pos = 0;
  meganet_tcp_abort_s(FTP_CTRL_SOCK);
  meganet_tcp_abort_s(FTP_DATA_SOCK);
  for (i = 0; i < 3; i++) ftp_poll();
  meganet_tcp_connect_s(FTP_CTRL_SOCK, ip4, port);
  if (!wait_connected(FTP_CTRL_SOCK)) return 0;
  if (!wait_reply()) return 0;
  if (ftp_code / 100 != 2) { ftp_error = FTP_E_REPLY; return 0; }
  return 1;
}

static unsigned char send_line(const char *s)
{
  static char out[FTP_REPLY_MAX + 2];
  unsigned int n = strlen(s), sent = 0, k;
  if (n > FTP_REPLY_MAX - 1) n = FTP_REPLY_MAX - 1;
  memcpy(out, s, n); out[n] = '\r'; out[n + 1] = '\n'; n += 2;
  ftp_frames = 0;
  while (sent < n) {
    k = meganet_tcp_send_s(FTP_CTRL_SOCK, out + sent, n - sent);
    sent += k;
    ftp_poll();
    if (ftp_frames > FTP_TIMEOUT_FRAMES) { ftp_error = FTP_E_NOREPLY; return 0; }
  }
  return 1;
}

unsigned char ftp_command(const char *cmd)
{
  ftp_error = FTP_E_NONE;
  if (!send_line(cmd)) return 0;
  if (!wait_reply()) return 0;
  if (ftp_code / 100 == 2 || ftp_code / 100 == 3) return 1;
  ftp_error = FTP_E_REPLY;
  return 0;
}

unsigned char ftp_command2(const char *verb, const char *arg)
{
  static char cmd[FTP_REPLY_MAX];
  strcpy(cmd, verb); strcat(cmd, " ");
  strncat(cmd, arg, FTP_REPLY_MAX - strlen(cmd) - 1);
  return ftp_command(cmd);
}

unsigned char ftp_login(const char *user, const char *pass)
{
  if (!ftp_command2("USER", user[0] ? user : "anonymous")) return 0;
  if (ftp_code / 100 == 2) return 1;                       /* no password wanted */
  return ftp_command2("PASS", pass[0] ? pass : "mega65@");   /* anonymous convention: an address */
}

const char *ftp_strerror(void)
{
  switch (ftp_error) {
  case FTP_E_REPLY: return ftp_reply;
  case FTP_E_REFUSED: return "connection refused";
  case FTP_E_NOREPLY: return "no reply (timed out)";
  case FTP_E_RESET: return "connection closed by the server";
  case FTP_E_PASV: return "could not read the PASV reply";
  default: return "unknown error";
  }
}

unsigned char ftp_ctrl_alive(void)
{
  unsigned char fl;
  if (meganet_tcp_state_s(FTP_CTRL_SOCK, &fl, 0) != MEGANET_TCP_ESTABLISHED) return 0;
  return (fl & (MEGANET_TCP_F_RESET | MEGANET_TCP_F_EOF)) ? 0 : 1;
}

/* "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)." */
unsigned char ftp_pasv(void)
{
  unsigned char v[6], n = 0, ip[4];
  unsigned int acc = 0; unsigned char in_num = 0;
  const char *p;
  if (!ftp_command("PASV")) return 0;
  for (p = ftp_reply; *p && n < 6; p++) {
    if (*p >= '0' && *p <= '9') { acc = acc * 10 + (unsigned int)(*p - '0'); in_num = 1; }
    else if (in_num) { v[n++] = (unsigned char)acc; acc = 0; in_num = 0; }
  }
  if (in_num && n < 6) v[n++] = (unsigned char)acc;
  if (n < 6) { ftp_error = FTP_E_PASV; return 0; }
  ip[0] = v[0]; ip[1] = v[1]; ip[2] = v[2]; ip[3] = v[3];
  meganet_tcp_abort_s(FTP_DATA_SOCK);
  ftp_poll();
  meganet_tcp_connect_s(FTP_DATA_SOCK, ip, (unsigned int)((unsigned int)v[4] << 8 | v[5]));
  data_done = 0;
  return wait_connected(FTP_DATA_SOCK);
}

/* A transfer command (LIST, RETR, STOR) is answered with a 1xx
 * preliminary reply while the data flows, then a 2xx completion after the
 * data connection closes. ftp_command is for the 2xx/3xx commands and
 * rejects 1xx, so the transfer verbs send the line themselves and accept
 * class 1. */
unsigned char ftp_start_transfer(const char *verb, const char *arg)
{
  static char cmd[FTP_REPLY_MAX];
  ftp_error = FTP_E_NONE;
  strcpy(cmd, verb);
  if (arg && arg[0]) { strcat(cmd, " "); strncat(cmd, arg, FTP_REPLY_MAX - strlen(cmd) - 1); }
  if (!send_line(cmd)) return 0;
  if (!wait_reply()) return 0;
  if (ftp_code / 100 == 1) return 1;               /* transfer under way */
  ftp_error = FTP_E_REPLY;                          /* e.g. 550 no such file */
  return 0;
}

unsigned int ftp_data_read(unsigned char *buf, unsigned int cap, unsigned char *done)
{
  unsigned char fl, st;
  unsigned int n;
  ftp_poll();
  n = meganet_tcp_recv_s(FTP_DATA_SOCK, buf, cap);
  st = meganet_tcp_state_s(FTP_DATA_SOCK, &fl, 0);
  if (n == 0 && (st == MEGANET_TCP_CLOSED || (fl & (MEGANET_TCP_F_EOF | MEGANET_TCP_F_RESET))))
    data_done = 1;
  *done = data_done;
  return n;
}

unsigned int ftp_data_write(const unsigned char *buf, unsigned int len)
{
  ftp_poll();
  return meganet_tcp_send_s(FTP_DATA_SOCK, buf, len);
}

unsigned char ftp_finish_transfer(void)
{
  unsigned char fl, st;
  meganet_tcp_close_s(FTP_DATA_SOCK);
  ftp_frames = 0;
  do {                                                     /* let the close complete, briefly */
    ftp_poll();
    st = meganet_tcp_state_s(FTP_DATA_SOCK, &fl, 0);
  } while (st != MEGANET_TCP_CLOSED && ftp_frames < 150);
  if (!wait_reply()) return 0;
  if (ftp_code / 100 != 2) { ftp_error = FTP_E_REPLY; return 0; }
  return 1;
}

void ftp_quit(void)
{
  send_line("QUIT");
  wait_reply();
  meganet_tcp_close_s(FTP_CTRL_SOCK);
}
