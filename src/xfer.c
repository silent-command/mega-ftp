#include <string.h>
#include "mega65/memory.h"
#include "meganet.h"
#include "m65_cbmdos.h"
#include "m65_hyppo.h"
#include "ftp.h"
#include "ui.h"
#include "xfer.h"

#define ROW_PROMPT 22

unsigned char xfer_drive;
char xfer_image[32];

static char answer[32];
static char text[81];
static unsigned char buf[512];               /* shared by get and put; never both at once */

static const char *cbmdos_text(unsigned char err)
{
  switch (err) {
  case CBMDOS_ERR_IO: return "disk read or write failed (no disk in that unit?)";
  case CBMDOS_ERR_FULL: return "disk full";
  case CBMDOS_ERR_DIRFULL: return "directory full";
  case CBMDOS_ERR_EXISTS: return "a file of that name is already there";
  case CBMDOS_ERR_PROT: return "disk is write protected";
  default: return "disk error";
  }
}

static void upper(char *s)
{
  for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
}

void xfer_drive_text(char *out)
{
  strcpy(out, xfer_drive ? "save to unit 9" : "save to unit 8");
  if (xfer_drive && xfer_image[0]) { strcat(out, " ("); strcat(out, xfer_image); strcat(out, ")"); }
}

unsigned char xfer_choose_drive(void)
{
  unsigned char how;

  ui_line(ROW_PROMPT, "Save to: 8 or 9 for that unit, or the name of a .D81 on the SD card to attach to unit 9", 0);
  if (xfer_drive && xfer_image[0]) strcpy(answer, xfer_image);
  else strcpy(answer, xfer_drive ? "9" : "8");
  if (!ui_read_line(UI_ROW_STATUS, "Drive: ", answer, sizeof answer - 1, 0) || !answer[0]) {
    ui_status("drive unchanged", 0);
    return 0;
  }
  if (!strcmp(answer, "8")) { xfer_drive = 0; ui_status("saving to unit 8", 0); return 1; }
  if (!strcmp(answer, "9")) { xfer_drive = 1; ui_status("saving to unit 9", 0); return 1; }

  upper(answer);
  if (!strstr(answer, ".D81")) strcat(answer, ".D81");
  ui_status("attaching ", answer);
  if (!hyppo_attach(1, answer, &how)) {
    ui_put_ulong(text, how);
    ui_status("attach failed; is that name on the SD card? Hyppo error ", text);
    return 0;
  }
  xfer_drive = 1;
  strcpy(xfer_image, answer);
  strcpy(text, answer);
  strcat(text, " attached to unit 9; saving there");
  ui_status(text, 0);
  return 1;
}

/* A CBM name from the server's: upper case, sixteen characters, the
 * characters a directory entry cannot hold replaced. */
static void suggest_name(const char *from, char *to)
{
  unsigned char n = 0;
  char c;
  while ((c = *from++) != 0 && n < 16) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ' '))
      c = '-';
    to[n++] = c;
  }
  to[n] = 0;
  if (!n) strcpy(to, "DOWNLOAD");
}

static unsigned char ends_with_prg(const char *s)
{
  unsigned char n = (unsigned char)strlen(s);
  return n >= 4 && !strcmp(s + n - 4, ".PRG");
}

static void progress(unsigned long got, unsigned long of)
{
  char num[12];
  strcpy(text, "getting: ");
  ui_put_ulong(num, got);
  strcat(text, num);
  if (of) { strcat(text, " of "); ui_put_ulong(num, of); strcat(text, num); }
  strcat(text, " bytes   (RUN/STOP cancels)");
  ui_status(text, 0);
}

unsigned char xfer_get(const struct dl_entry *e)
{
  static char name[17];
  unsigned char type, err, done, k;
  unsigned int n, i;
  unsigned long got = 0, shown = 0;

  if (e->kind == DL_DIR) { ui_status("that is a directory; RETURN opens it", 0); return 0; }

  xfer_drive_text(text);
  strcat(text, "; D changes it. Name on the disk (16 characters):");
  ui_line(ROW_PROMPT, text, 0);
  suggest_name(e->name, name);
  if (!ui_read_line(UI_ROW_STATUS, "Save as: ", name, 16, 0) || !name[0]) { ui_status("cancelled", 0); return 0; }
  upper(name);

  type = ends_with_prg(name) ? CBMDOS_TYPE_PRG : CBMDOS_TYPE_SEQ;
  ui_line(ROW_PROMPT, "File type: S = SEQ (text, data)   P = PRG (a program)   RETURN keeps the default", 0);
  ui_status(type == CBMDOS_TYPE_PRG ? "Type [PRG]: " : "Type [SEQ]: ", 0);
  ui_flush_keys();
  k = ui_wait_key();
  if (k == KEY_STOP) { ui_status("cancelled", 0); return 0; }
  if (k == 'p' || k == 'P') type = CBMDOS_TYPE_PRG;
  if (k == 's' || k == 'S') type = CBMDOS_TYPE_SEQ;

  ui_status("opening the file on the disk...", 0);
  err = cbmdos_create_as(name, xfer_drive, type);
  if (err == CBMDOS_ERR_EXISTS) {
    ui_line(ROW_PROMPT, "That name is already on the disk.  O = overwrite it   anything else cancels", 0);
    ui_status("Overwrite? ", 0);
    ui_flush_keys();
    k = ui_wait_key();
    if (k != 'o' && k != 'O') { ui_status("cancelled", 0); return 0; }
    err = cbmdos_delete(name, xfer_drive);
    if (err == CBMDOS_OK) err = cbmdos_create_as(name, xfer_drive, type);
  }
  if (err != CBMDOS_OK) { ui_status("disk: ", cbmdos_text(err)); return 0; }

  if (!ftp_ctrl_alive()) { cbmdos_close(); cbmdos_delete(name, xfer_drive); ui_status("the connection was lost; press R to reconnect", 0); return 0; }
  ui_status("asking the server...", 0);
  if (!ftp_pasv() || !ftp_start_transfer("RETR", e->name)) {
    cbmdos_close(); cbmdos_delete(name, xfer_drive);
    ui_status("get: ", ftp_strerror());
    return 0;
  }

  progress(0, e->size);
  ftp_frames = 0;
  ui_flush_keys();
  for (;;) {
    n = ftp_data_read(buf, sizeof buf, &done);
    if (n) ftp_frames = 0;
    for (i = 0; i < n; i++) {
      err = cbmdos_put(buf[i]);
      if (err != CBMDOS_OK) {
        meganet_tcp_abort_s(FTP_DATA_SOCK);
        cbmdos_close(); cbmdos_delete(name, xfer_drive);
        ui_status("disk: ", cbmdos_text(err));
        return 0;
      }
    }
    got += n;
    if (got - shown >= 2048) { shown = got; progress(got, e->size); }
    ui_spin();
    if (ui_key() == KEY_STOP) {
      meganet_tcp_abort_s(FTP_DATA_SOCK);
      cbmdos_close(); cbmdos_delete(name, xfer_drive);
      ui_spin_clear();
      ftp_finish_transfer();                        /* the server's 426/226 for the aborted data */
      ui_status("cancelled; the partial file was removed", 0);
      return 0;
    }
    if (ftp_frames > FTP_TIMEOUT_FRAMES) {
      meganet_tcp_abort_s(FTP_DATA_SOCK);
      cbmdos_close(); cbmdos_delete(name, xfer_drive);
      ui_spin_clear();
      ui_status("get: the data stopped arriving", 0);
      return 0;
    }
    if (done) break;
  }
  ui_status("closing the file...", 0);
  err = cbmdos_close();
  k = ftp_finish_transfer();                        /* 226, or the server's 426/451 */
  ui_spin_clear();
  if (err != CBMDOS_OK) { ui_status("disk: ", cbmdos_text(err)); return 0; }
  /* A data connection that was reset looks like an end of file to the
   * reader; the server's completion reply and the size it listed tell
   * the truth. A short file is removed, not announced. */
  if (!k || (e->size && got != e->size)) {
    cbmdos_delete(name, xfer_drive);
    if (!k) ui_status("get: ", ftp_strerror());
    else ui_status("get: the file arrived short; the partial file was removed", 0);
    return 0;
  }
  {
    char num[12];
    strcpy(text, "saved ");
    strcat(text, name);
    strcat(text, xfer_drive ? " on unit 9: " : " on unit 8: ");
    ui_put_ulong(num, got); strcat(text, num); strcat(text, " bytes, ");
    ui_put_ulong(num, cbmdos_blocks()); strcat(text, num); strcat(text, " blocks");
    ui_status(text, 0);
  }
  return 1;
}

/* ---- uploads ----------------------------------------------------------- */

#include "mega65/fileio.h"

#define LP_MAX 64
#define LP_ROW_FIRST 2
#define LP_ROWS 20

struct local_entry {
  char name[17];
  unsigned int blocks;
  unsigned char type;
};
static struct local_entry lp[LP_MAX];
static unsigned char lp_count;
static char from_s[4] = "8";

static const char *type_text(unsigned char t)
{
  switch (t & 0x0f) {
  case 1: return "SEQ";
  case 2: return "PRG";
  case 3: return "USR";
  case 4: return "REL";
  default: return "???";
  }
}

/* Built with byte loops, not strcpy/strcat: with LTO and -Os the pair
 * strcpy(text, "      ") + strcat(text, lp[idx].name) was compiled into
 * two one-byte copies and every name showed as its first letter (the
 * identical pattern in the browser's draw_entry is fine; see
 * REQUIREMENTS.md 5.5). Plain loops give the compiler nothing to
 * simplify. */
static unsigned char put_str(unsigned char n, const char *s, unsigned char limit)
{
  while (*s && n < limit) text[n++] = *s++;
  return n;
}

static unsigned char put_pad(unsigned char n, unsigned char to)
{
  while (n < to) text[n++] = ' ';
  return n;
}

static void lp_draw_entry(unsigned char idx, unsigned char row, unsigned char selected)
{
  unsigned char n;
  n = put_pad(0, 6);
  n = put_str(n, lp[idx].name, 24);
  n = put_pad(n, 26);
  n = put_str(n, type_text(lp[idx].type), 30);
  n = put_pad(n, 34);
  text[n] = 0;
  ui_put_ulong(text + n, lp[idx].blocks);
  while (text[n]) n++;
  n = put_str(n, " blocks", 79);
  text[n] = 0;
  if (idx == selected) ui_line_rev(row, text, 0);
  else ui_line(row, text, 0);
}

static void lp_draw_page(unsigned char top, unsigned char selected)
{
  unsigned char r, idx;
  for (r = 0; r < LP_ROWS; r++) {
    idx = (unsigned char)(top + r);
    if (idx < lp_count) lp_draw_entry(idx, (unsigned char)(LP_ROW_FIRST + r), selected);
    else ui_line((unsigned char)(LP_ROW_FIRST + r), 0, 0);
  }
}

/* Lists the disk in `drive` and lets the cursor pick a file. */
static unsigned char pick_local(unsigned char drive, char *out)
{
  unsigned char selected = 0, top = 0, k, old;
  char num[8];

  ui_status("reading the directory...", 0);
  lp_count = 0;
  cbmdos_dir_first(drive);
  while (lp_count < LP_MAX && cbmdos_dir_next(lp[lp_count].name, &lp[lp_count].type, &lp[lp_count].blocks))
    lp_count++;
  cbmdos_dir_end();
  if (!lp_count) { ui_status("no files on that disk (or no disk in that unit)", 0); return 0; }

  ui_put_ulong(num, lp_count);
  strcpy(text, num);
  strcat(text, drive ? " files on unit 9" : " files on unit 8");
  ui_line(UI_ROW_TITLE, text, 0);
  ui_line(UI_ROW_KEYS, "cursor keys move   RETURN sends the file   RUN/STOP goes back", 0);
  ui_status(0, 0);
  ui_line(ROW_PROMPT, 0, 0);
  lp_draw_page(top, selected);
  for (;;) {
    k = ui_wait_key();
    old = selected;
    switch (k) {
    case KEY_DOWN: if (selected + 1 < lp_count) selected++; break;
    case KEY_UP: if (selected) selected--; break;
    case KEY_RIGHT: if (top + LP_ROWS < lp_count) selected = (unsigned char)(top + LP_ROWS); break;
    case KEY_LEFT: selected = (unsigned char)(top >= LP_ROWS ? top - LP_ROWS : 0); break;
    case KEY_HOME: selected = 0; break;
    case KEY_RETURN: strcpy(out, lp[selected].name); return 1;
    case KEY_STOP: return 0;
    default: continue;
    }
    if (selected < top || selected >= top + LP_ROWS) {
      top = (unsigned char)(selected - selected % LP_ROWS);
      lp_draw_page(top, selected);
    } else if (selected != old) {
      lp_draw_entry(old, (unsigned char)(LP_ROW_FIRST + old - top), selected);
      lp_draw_entry(selected, (unsigned char)(LP_ROW_FIRST + selected - top), selected);
    }
  }
}

/* Pushes `n` bytes into the data connection, polling until the stack has
 * taken them all. 0 on cancel, timeout or a dead connection. */
static unsigned char send_all(const unsigned char *p, unsigned int n)
{
  unsigned int k;
  unsigned char st, fl;
  ftp_frames = 0;
  while (n) {
    k = ftp_data_write(p, n);
    p += k; n -= k;
    if (k) ftp_frames = 0;
    ui_spin();
    st = meganet_tcp_state_s(FTP_DATA_SOCK, &fl, 0);
    if (st == MEGANET_TCP_CLOSED || (fl & MEGANET_TCP_F_RESET)) { ftp_error = FTP_E_RESET; return 0; }
    if (ui_key() == KEY_STOP) { ftp_error = FTP_E_NONE; return 0; }
    if (ftp_frames > FTP_TIMEOUT_FRAMES) { ftp_error = FTP_E_NOREPLY; return 0; }
  }
  return 1;
}

static void put_progress(unsigned long sent)
{
  char num[12];
  strcpy(text, "sending: ");
  ui_put_ulong(num, sent);
  strcat(text, num);
  strcat(text, " bytes   (RUN/STOP cancels)");
  ui_status(text, 0);
}

unsigned char xfer_put(void)
{
  static char local[32];
  static char remote[DL_NAME_MAX + 1];
  unsigned char from_sd, drive = 0, err, fd = 0xff, ok = 1;
  unsigned int n;
  unsigned long sent = 0, shown = 0;

  ui_line(ROW_PROMPT, "Send from: 8 or 9 (pick a file on that disk), or SD (a file on the SD card by name)", 0);
  if (!ui_read_line(UI_ROW_STATUS, "From: ", from_s, sizeof from_s - 1, 0) || !from_s[0]) { ui_status("cancelled", 0); return 0; }
  upper(from_s);
  from_sd = !strcmp(from_s, "SD");
  if (!from_sd && strcmp(from_s, "8") && strcmp(from_s, "9")) { ui_status("8, 9 or SD", 0); return 0; }

  if (from_sd) {
    ui_line(ROW_PROMPT, "The file's name on the SD card (root directory):", 0);
    local[0] = 0;
    if (!ui_read_line(UI_ROW_STATUS, "SD file: ", local, sizeof local - 1, 0) || !local[0]) { ui_status("cancelled", 0); return 0; }
    upper(local);
    fd = open(local);
    if (fd == 0xff) { ui_status("not found on the SD card: ", local); return 0; }
  } else {
    drive = (unsigned char)(from_s[0] == '9');
    if (!pick_local(drive, local)) { ui_status("cancelled", 0); return 0; }
    err = cbmdos_open_read(local, drive);
    if (err != CBMDOS_OK) { ui_status("disk: ", cbmdos_text(err)); return 0; }
  }

  strcpy(remote, local);
  ui_line(ROW_PROMPT, "The name to give it on the server:", 0);
  if (!ui_read_line(UI_ROW_STATUS, "Send as: ", remote, DL_NAME_MAX, 0) || !remote[0]) {
    if (from_sd) close(fd); else cbmdos_close_read();
    ui_status("cancelled", 0);
    return 0;
  }

  if (!ftp_ctrl_alive()) {
    if (from_sd) close(fd); else cbmdos_close_read();
    ui_status("the connection was lost; press R to reconnect", 0);
    return 0;
  }
  ui_status("asking the server...", 0);
  if (!ftp_pasv() || !ftp_start_transfer("STOR", remote)) {
    if (from_sd) close(fd); else cbmdos_close_read();
    ui_status("put: ", ftp_strerror());
    return 0;
  }

  put_progress(0);
  ui_flush_keys();
  for (;;) {
    if (from_sd) n = (unsigned int)read512(buf);
    else {
      n = cbmdos_read_next(buf, &err);
      if (err != CBMDOS_OK) { ui_status("disk: ", cbmdos_text(err)); ok = 0; break; }
    }
    if (!n) break;
    if (!send_all(buf, n)) {
      ui_status(ftp_error == FTP_E_NONE ? "cancelled" : "put: ", ftp_error == FTP_E_NONE ? 0 : ftp_strerror());
      ok = 0;
      break;
    }
    sent += n;
    if (sent - shown >= 2048) { shown = sent; put_progress(sent); }
  }
  if (from_sd) close(fd); else cbmdos_close_read();
  if (!ok) {
    meganet_tcp_abort_s(FTP_DATA_SOCK);
    ui_spin_clear();
    ftp_finish_transfer();                        /* the server's word on the aborted data */
    return 0;
  }
  ui_status("finishing...", 0);
  if (!ftp_finish_transfer()) { ui_spin_clear(); ui_status("put: ", ftp_strerror()); return 0; }
  ui_spin_clear();
  {
    char num[12];
    strcpy(text, "sent ");
    strcat(text, remote);
    strcat(text, ": ");
    ui_put_ulong(num, sent); strcat(text, num); strcat(text, " bytes");
    ui_status(text, 0);
  }
  return 1;
}
