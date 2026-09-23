#include "mega65/memory.h"
#include "meganet.h"
#include "m65_boot.h"
#include "ftp.h"
#include "ui.h"
#include "netutil.h"

/* Three attempts of eight seconds; mega-net retries DISCOVER itself every
 * four, so this restarts a machine that gave up behind a link still
 * coming up at power-on. */
#define DHCP_ATTEMPTS 3
#define DHCP_WAIT_FRAMES 400
#define DNS_WAIT_FRAMES 400

unsigned char net_up(const char **err)
{
  unsigned char attempt, st;

  if (!m65_boot_load(err)) return 0;
  meganet_call(MEGANET_INIT, 0, 0, 0, 0);
  for (attempt = 0; attempt < DHCP_ATTEMPTS; attempt++) {
    meganet_dhcp_start();
    ftp_frames = 0;
    while (ftp_frames < DHCP_WAIT_FRAMES) {
      ftp_poll();
      st = meganet_dhcp_state();
      if (st == MEGANET_DHCP_BOUND) { *err = 0; return 1; }
      if (st == MEGANET_DHCP_FAILED) break;
    }
  }
  *err = "no DHCP lease (cable? router?)";
  return 0;
}

static unsigned char parse_dotted_quad(const char *s, unsigned char *out)
{
  unsigned int octet;
  unsigned char part, digits;
  for (part = 0; part < 4; part++) {
    octet = 0; digits = 0;
    while (*s >= '0' && *s <= '9') {
      octet = octet * 10 + (unsigned char)(*s - '0');
      if (octet > 255) return 0;
      digits++; s++;
    }
    if (!digits) return 0;
    out[part] = (unsigned char)octet;
    if (part < 3) { if (*s != '.') return 0; s++; }
  }
  return *s == 0;
}

unsigned char net_resolve(const char *host, unsigned char *ip, const char **err)
{
  unsigned char state;
  if (parse_dotted_quad(host, ip)) { *err = 0; return 1; }
  meganet_dns_start(host);
  ftp_frames = 0;
  state = MEGANET_DNS_WAITING;
  while (ftp_frames < DNS_WAIT_FRAMES) {
    ftp_poll();
    state = meganet_dns_state();
    if (state != MEGANET_DNS_WAITING) break;
  }
  if (state != MEGANET_DNS_DONE) {
    *err = (state == MEGANET_DNS_FAILED) ? "host not found" : "no reply from the name server";
    return 0;
  }
  meganet_dns_result(ip);
  *err = 0;
  return 1;
}
