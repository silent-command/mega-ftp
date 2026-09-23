#include <string.h>
#include "dirlist.h"

struct dl_entry dl_entries[DL_MAX];
unsigned char dl_count;
unsigned char dl_overflow;

void dl_clear(void)
{
  dl_count = 0;
  dl_overflow = 0;
}

static unsigned char is_digit(char c) { return c >= '0' && c <= '9'; }

/* Advances past the current field and the spaces after it. */
static const char *next_field(const char *p)
{
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  return p;
}

static unsigned long field_ulong(const char *p)
{
  unsigned long v = 0;
  while (is_digit(*p)) { v = v * 10 + (unsigned long)(*p - '0'); p++; }
  return v;
}

static void set_name(struct dl_entry *e, const char *p)
{
  unsigned char n = 0;
  const char *q;
  /* a symlink line ends " -> target"; the name is what comes before */
  if (e->kind == DL_LINK) {
    q = strstr(p, " -> ");
    if (q) {
      while (p < q && n < DL_NAME_MAX) e->name[n++] = *p++;
      e->name[n] = 0;
      return;
    }
  }
  while (*p && *p != '\r' && n < DL_NAME_MAX) e->name[n++] = *p++;
  e->name[n] = 0;
}

void dl_add_line(const char *s)
{
  struct dl_entry *e;
  const char *p = s;
  unsigned char i;

  while (*p == ' ') p++;
  if (!*p || !strncmp(p, "total ", 6)) return;
  if (dl_count >= DL_MAX) { dl_overflow = 1; return; }
  e = &dl_entries[dl_count];
  e->size = 0;

  if (is_digit(p[0]) && is_digit(p[1]) && p[2] == '-') {
    /* DOS style: date, time, "<DIR>" or size, then the name */
    p = next_field(p);
    p = next_field(p);
    if (!strncmp(p, "<DIR>", 5)) { e->kind = DL_DIR; }
    else { e->kind = DL_FILE; e->size = field_ulong(p); }
    p = next_field(p);
  } else {
    /* Unix style: perms links owner group size month day time/year name */
    switch (p[0]) {
    case 'd': e->kind = DL_DIR; break;
    case 'l': e->kind = DL_LINK; break;
    case '-': e->kind = DL_FILE; break;
    default: return;                                  /* not a listing line */
    }
    for (i = 0; i < 4; i++) p = next_field(p);
    e->size = field_ulong(p);
    for (i = 0; i < 4; i++) p = next_field(p);
  }
  if (!*p) return;
  set_name(e, p);
  if (!strcmp(e->name, ".") || !strcmp(e->name, "..")) return;
  dl_count++;
}

void dl_sort(void)
{
  static struct dl_entry tmp;
  unsigned char i, j;
  /* stable: move each directory up past the files before it */
  for (i = 1; i < dl_count; i++) {
    if (dl_entries[i].kind != DL_DIR) continue;
    j = i;
    while (j > 0 && dl_entries[j - 1].kind != DL_DIR) j--;
    if (j == i) continue;
    memcpy(&tmp, &dl_entries[i], sizeof tmp);
    memmove(&dl_entries[j + 1], &dl_entries[j], (size_t)(i - j) * sizeof tmp);
    memcpy(&dl_entries[j], &tmp, sizeof tmp);
  }
}
