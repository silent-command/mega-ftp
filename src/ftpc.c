/* MEGA65 FTP client: the browser. Connects, logs in, lists a directory
 * and lets the cursor move through it; RETURN enters a directory, U goes
 * up. Getting and putting files are the next steps (REQUIREMENTS.md 4). */
#include <string.h>
#include "mega65/memory.h"
#include "meganet.h"
#include "m65_screen.h"
#include "ftp.h"
#include "ui.h"
#include "marks.h"
#include "m65_boot.h"
#include "dirlist.h"
#include "netutil.h"
#include "xfer.h"
#include "m65_exit.h"

#define FTPC_VERSION "0.4.5"
#define ROW_FIRST 2
#define ROWS_PER_PAGE 20
#define ROW_INFO 22

#define KEEPALIVE_FRAMES 6000       /* a NOOP after two idle minutes */
#define NAME_COLS 58                /* the name column, before the size */

static char host[64];
static char port_s[6];
static char user[32];
static char pass[32];
static unsigned char ip[4];
static unsigned int port;
static char path[80];
static char mark_path[80];            /* a bookmark's directory, to change to after login (5.10) */
static unsigned char selected, top;
static unsigned char session_open;   /* logged in (or believed to be) */
static unsigned char lost;           /* the server closed the control connection */
static char line[81];

/* ---- feedback --------------------------------------------------------- */

static void fail(const char *what)
{
  const char *why = ftp_strerror();
  ui_spin_clear();
  ui_status(what, why);
}

/* ---- session ---------------------------------------------------------- */

static void update_path(void)
{
  const char *p;
  unsigned char n = 0;
  if (!ftp_command("PWD")) return;                  /* keep what we had */
  for (p = ftp_reply; *p && *p != '"'; p++) ;
  if (!*p) return;
  for (p++; *p && *p != '"' && n < sizeof path - 1; p++) path[n++] = *p;
  path[n] = 0;
}

static unsigned char open_session(void)
{
  ui_status("connecting...", 0);
  if (!ftp_connect(ip, port)) { fail("connect: "); return 0; }
  ui_status("logging in...", 0);
  if (!ftp_login(user, pass)) { fail("login: "); return 0; }
  ftp_command("TYPE I");
  if (path[0]) ftp_command2("CWD", path);
  session_open = 1;
  lost = 0;
  return 1;
}

/* Every command starts here: a session the server has dropped while the
 * user was reading is reopened in place. */
static unsigned char ensure_session(void)
{
  if (session_open && ftp_ctrl_alive()) return 1;
  ui_status("reconnecting...", 0);
  session_open = 0;
  return open_session();
}

static unsigned int idle_frames;
static unsigned char idle_check;

static void on_idle(void)
{
  if (!session_open) return;
  if (++idle_check >= 60) {                         /* once a second */
    idle_check = 0;
    if (!lost && !ftp_ctrl_alive()) {
      lost = 1;
      ui_status("the server closed the connection; the next action reconnects", 0);
    }
  }
  if (lost) return;
  if (++idle_frames >= KEEPALIVE_FRAMES) {
    idle_frames = 0;
    ftp_command("NOOP");
  }
}

/* ---- drawing ---------------------------------------------------------- */

static void draw_title(void)
{
  strcpy(line, "ftp://");
  strncat(line, host, 40);
  if (port != 21) { strcat(line, ":"); strcat(line, port_s); }
  strncat(line, path, 78 - strlen(line));
  ui_line(UI_ROW_TITLE, line, 0);
}

static void draw_keys(void)
{
  ui_line(UI_ROW_KEYS, "RETURN open U up G get P put D drive R list M mark F/B color HELP host", 0);
}

static void draw_entry(unsigned char idx, unsigned char row)
{
  static char folded[NAME_COLS + 1];
  struct dl_entry *e = &dl_entries[idx];
  unsigned char n;

  switch (e->kind) {
  case DL_DIR: strcpy(line, "[DIR] "); break;
  case DL_LINK: strcpy(line, "[LNK] "); break;
  default: strcpy(line, "      "); break;
  }
  m65_fold_utf8(folded, e->name, NAME_COLS);
  strcat(line, folded);
  n = (unsigned char)strlen(line);
  while (n < 6 + NAME_COLS + 2) line[n++] = ' ';
  line[n] = 0;
  if (e->kind == DL_FILE) ui_put_size(line + n, e->size);
  if (idx == selected) ui_line_rev(row, line, 0);
  else ui_line(row, line, 0);
}

static void draw_info(void)
{
  char num[8];
  ui_put_ulong(num, dl_count);
  strcpy(line, num);
  strcat(line, dl_count == 1 ? " entry" : " entries");
  if (dl_overflow) strcat(line, " (listing truncated)");
  if (dl_count > ROWS_PER_PAGE) {
    strcat(line, "   page ");
    ui_put_ulong(num, (unsigned long)(top / ROWS_PER_PAGE + 1));
    strcat(line, num);
    strcat(line, " of ");
    ui_put_ulong(num, (unsigned long)((dl_count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE));
    strcat(line, num);
  }
  strcat(line, "   ");
  xfer_drive_text(line + strlen(line));
  ui_line(ROW_INFO, line, 0);
}

static void draw_page(void)
{
  unsigned char r, idx;
  draw_title();
  for (r = 0; r < ROWS_PER_PAGE; r++) {
    idx = (unsigned char)(top + r);
    if (idx < dl_count) draw_entry(idx, (unsigned char)(ROW_FIRST + r));
    else ui_line((unsigned char)(ROW_FIRST + r), 0, 0);
  }
  draw_info();
  draw_keys();
}

static void move_to(unsigned char idx)
{
  unsigned char old = selected;
  if (idx >= dl_count) return;
  selected = idx;
  if (idx < top || idx >= top + ROWS_PER_PAGE) {
    top = (unsigned char)(idx - idx % ROWS_PER_PAGE);
    draw_page();
    return;
  }
  draw_entry(old, (unsigned char)(ROW_FIRST + old - top));
  draw_entry(selected, (unsigned char)(ROW_FIRST + selected - top));
}

/* ---- listing ---------------------------------------------------------- */

static unsigned char fetch_listing(void)
{
  static unsigned char buf[512];
  static char text[160];
  unsigned int n, i;
  unsigned char ll = 0, done;

  if (!ensure_session()) return 0;
  ui_status("listing...", 0);
  if (!ftp_pasv()) { fail("PASV: "); return 0; }
  if (!ftp_start_transfer("LIST", 0)) { fail("LIST: "); return 0; }
  dl_clear();
  ftp_frames = 0;
  do {
    n = ftp_data_read(buf, sizeof buf, &done);
    if (n) ftp_frames = 0;                          /* an inactivity timeout, not a total one */
    for (i = 0; i < n; i++) {
      char c = (char)buf[i];
      if (c == '\r') continue;
      if (c == '\n') { text[ll] = 0; dl_add_line(text); ll = 0; }
      else if (ll < sizeof text - 1) text[ll++] = c;
    }
    ui_spin();
    if (ftp_frames > FTP_TIMEOUT_FRAMES) {
      meganet_tcp_abort_s(FTP_DATA_SOCK);
      ftp_error = FTP_E_NOREPLY;
      fail("LIST: ");
      return 0;
    }
  } while (!done);
  if (ll) { text[ll] = 0; dl_add_line(text); }
  ftp_finish_transfer();                            /* the listing stands whatever the 226 says */
  dl_sort();
  ui_spin_clear();
  selected = top = 0;
  draw_page();
  ui_status(0, 0);
  return 1;
}

static void enter_directory(const char *name)
{
  if (!ensure_session()) return;
  if (!ftp_command2("CWD", name)) { fail("CWD: "); return; }
  update_path();
  fetch_listing();
}

static void go_up(void)
{
  if (!ensure_session()) return;
  if (!ftp_command("CDUP")) { fail("CDUP: "); return; }
  update_path();
  fetch_listing();
}

/* ---- the host screen -------------------------------------------------- */

#define ROW_MARKS 8

/* The bookmarks under the prompts: number, host and port, user, directory. */
static void draw_marks(void)
{
  static char text[81];
  unsigned char i, n;
  if (!marks_count) return;
  ui_line(ROW_MARKS, "Bookmarks: a number at the Host prompt opens one, D and the number deletes it", 0);
  for (i = 0; i < marks_count && ROW_MARKS + 1 + i < ROW_INFO; i++) {
    if (!marks_get(i, marks_h, marks_p, marks_u, marks_d)) break;
    text[0] = ' '; text[1] = (char)('1' + i); text[2] = '.'; text[3] = ' '; n = 4;
    { const char *q = marks_h; while (*q && n < 40) text[n++] = *q++; }
    if (strcmp(marks_p, "21")) { text[n++] = ':'; { const char *q = marks_p; while (*q && n < 46) text[n++] = *q++; } }
    text[n++] = ' '; text[n++] = ' ';
    { const char *q = marks_u; while (*q && n < 60) text[n++] = *q++; }
    text[n++] = ' '; text[n++] = ' ';
    { const char *q = marks_d; while (*q && n < 79) text[n++] = *q++; }
    text[n] = 0;
    ui_line((unsigned char)(ROW_MARKS + 1 + i), text, 0);
  }
}


static unsigned char session_setup(void)
{
  unsigned char i;
  const char *err;
  unsigned int v = 0;

  unsigned char picked = 0;

  ui_clear_rows(1, ROW_INFO);
  ui_line(UI_ROW_TITLE, "MEGA65 FTP Client", 0);
  ui_line(UI_ROW_KEYS, "RETURN accepts a line   RUN/STOP goes back, and quits from the Host prompt", 0);
  ui_status("passive mode; anonymous unless you give a user name", 0);
  draw_marks();
  mark_path[0] = 0;
  for (;;) {
    unsigned char n = 0, del = 0, digits = 0;
    if (!ui_read_line(3, "Host: ", host, sizeof host - 1, 0)) return 2;
    if (!host[0]) continue;
    /* A number picks a bookmark; D and a number deletes one (5.10). */
    i = 0;
    if (host[0] == 'd' || host[0] == 'D') { del = 1; i = 1; }
    while (host[i] >= '0' && host[i] <= '9') { n = (unsigned char)(n * 10 + (host[i] - '0')); i++; digits++; }
    if (!digits || host[i]) break;                  /* a host name */
    if (n < 1 || n > marks_count) { ui_status("no such bookmark", 0); host[0] = 0; continue; }
    if (del) {
      ui_status(marks_remove((unsigned char)(n - 1)) ? "bookmark removed" : "could not write FTPC.CFG", 0);
      ui_clear_rows(ROW_MARKS, ROW_INFO); draw_marks();
      host[0] = 0; continue;
    }
    marks_get((unsigned char)(n - 1), host, port_s, user, mark_path);
    ui_line(3, "Host: ", host); ui_line(4, "Port: ", port_s); ui_line(5, "User: ", user);
    picked = 1;
    break;
  }
  if (!picked) {
    if (!ui_read_line(4, "Port: ", port_s, sizeof port_s - 1, 0)) return 0;
    if (!ui_read_line(5, "User: ", user, sizeof user - 1, 0)) return 0;
  }
  for (i = 0; port_s[i] >= '0' && port_s[i] <= '9'; i++) v = v * 10 + (unsigned int)(port_s[i] - '0');
  port = v ? v : 21;
  if (!user[0]) strcpy(user, "anonymous");
  if (!ui_read_line(6, "Password: ", pass, sizeof pass - 1, 1)) return 0;

  ui_status("resolving host...", 0);
  if (!net_resolve(host, ip, &err)) { ui_status("resolve: ", err); return 0; }
  path[0] = 0;
  if (!open_session()) return 0;
  if (mark_path[0] && !ftp_command2("CWD", mark_path)) ui_status("the bookmarked directory is gone; at the login directory", 0);
  update_path();
  return fetch_listing();
}

/* ---- the browser ------------------------------------------------------ */

static unsigned char browse(void)
{
  unsigned char k;
  struct dl_entry *e;

  for (;;) {
    k = ui_wait_key();
    if (k >= 0xc1 && k <= 0xda && (ui_last_mods & MOD_MEGA)) k = (unsigned char)(k & 0x7f);   /* MEGA+letter: the capital with bit 7 set (ssh 5.29) */
    idle_frames = 0;
    switch (k) {
    case KEY_DOWN: move_to((unsigned char)(selected + 1)); break;
    case KEY_UP: if (selected) move_to((unsigned char)(selected - 1)); break;
    case KEY_RIGHT:
      if (top + ROWS_PER_PAGE < dl_count) move_to((unsigned char)(top + ROWS_PER_PAGE));
      break;
    case KEY_LEFT:
      if (top) move_to((unsigned char)(top - ROWS_PER_PAGE));
      else move_to(0);
      break;
    case KEY_HOME: move_to(0); break;
    case KEY_RETURN:
      if (!dl_count) break;
      e = &dl_entries[selected];
      if (e->kind == DL_FILE) { if (ensure_session()) xfer_get(e); draw_info(); }
      else enter_directory(e->name);
      break;
    case 'u': case 'U': case KEY_DEL: go_up(); break;
    case 'r': case 'R': fetch_listing(); break;
    case 'g': case 'G':
      if (!dl_count) break;
      if (ensure_session()) xfer_get(&dl_entries[selected]);
      draw_info();
      break;
    case 'p': case 'P':
      if (ensure_session()) {
        if (xfer_put()) { fetch_listing(); ui_status("sent; the listing shows it now", 0); }
        else draw_page();                           /* the picker used the listing rows */
      }
      break;
    case 'd': case 'D': xfer_choose_drive(); draw_info(); break;
    case 'f': case 'F':
      /* The text colour applies to what is drawn from now on, so redraw;
       * the status row is left as it was, in the old colour, on purpose:
       * it is the one line the user was reading. */
      m65_screen_cycle_text_colour();
      draw_page();
      break;
    case 'b': case 'B':
      /* Background and border move together and stay identical, so the
       * screen reads as a single surface (the gopher client's rule). */
      m65_screen_cycle_background();
      break;
    case 'm': case 'M': {
      /* Toggle, as the gopher client's M: this host, port, user and
       * directory in, or out if they are already there (5.10). */
      unsigned char at = marks_find(host, port_s, user, path);
      if (at != MARKS_NONE) ui_status(marks_remove(at) ? "bookmark removed" : "could not write FTPC.CFG", 0);
      else if (marks_count >= MARKS_MAX) ui_status("the bookmark list is full: D and its number on the host screen deletes one", 0);
      else ui_status(marks_add(host, port_s, user, path) ? "bookmarked, with this directory; a number on the host screen opens it" : "could not write FTPC.CFG", 0);
      break;
    }
    case KEY_HELP: case 'h': case 'H':               /* the host screen, from anywhere */
      if (session_open) ftp_quit();
      session_open = 0;
      return 1;
    case KEY_STOP:                                  /* back one level: up a directory; at the root, the host screen */
      if (path[0] == '/' && path[1] == 0) { if (session_open) ftp_quit(); session_open = 0; return 1; }
      go_up();
      break;
    default: break;
    }
  }
}

int main(void)
{
  const char *err;

  mega65_io_enable();
  m65_screen_init();
  ui_line(UI_ROW_TITLE, "MEGA65 FTP Client - version " FTPC_VERSION, 0);
  ui_status("starting the network...", 0);
  if (!net_up(&err)) { ui_status("network: ", err); for (;;) ; }
  ui_idle = on_idle;
  marks_load(boot_drive);
  strcpy(port_s, "21");
  strcpy(user, "anonymous");

  for (;;) {
    {
      unsigned char r = session_setup();            /* 1 to browse, 0 to try again, 2 to quit */
      if (r == 2) break;
      if (!r) {
        ui_line(UI_ROW_KEYS, "any key to try again   RUN/STOP to quit", 0);
        if (ui_wait_key() == KEY_STOP) break;
        continue;
      }
    }
    if (!browse()) break;
  }
  m65_exit_to_basic();                             /* BASIC's READY, disk still mounted; never returns */
  return 0;
}
