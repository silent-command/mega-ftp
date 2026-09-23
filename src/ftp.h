/* The FTP protocol over mega-net: a control connection on socket 0 and
 * passive data connections on socket 1. Every call returns promptly or
 * within a bounded wait paced on the frame counter; nothing here draws
 * on the screen. RFC 959, the parts a client needs. */
#ifndef FTP_H
#define FTP_H

#define FTP_CTRL_SOCK 0
#define FTP_DATA_SOCK 1
#define FTP_REPLY_MAX 120           /* the last line of the last reply, for the screen */
#define FTP_TIMEOUT_FRAMES 1500     /* 30 s: a reply, a connection, a data segment */

extern unsigned int ftp_code;       /* the last reply's three-digit code, 0 if none */
extern char ftp_reply[FTP_REPLY_MAX];
extern unsigned char ftp_error;     /* FTP_E_* of the last failure */
#define FTP_E_NONE 0
#define FTP_E_REFUSED 1             /* connection refused */
#define FTP_E_NOREPLY 2             /* no answer, or timed out */
#define FTP_E_RESET 3               /* the peer reset or closed the control connection */
#define FTP_E_REPLY 4               /* the server said no: see ftp_code and ftp_reply */
#define FTP_E_PASV 5                /* PASV reply could not be parsed */

/* Connects to ip:port and waits for the 220 greeting. */
unsigned char ftp_connect(const unsigned char *ip4, unsigned int port);
/* USER/PASS; anonymous if user is "". */
unsigned char ftp_login(const char *user, const char *pass);
/* Sends a command line (without CRLF) and waits for the complete reply.
 * Returns 1 if the reply class is 2 or 3 (positive), else 0 with
 * ftp_error set; ftp_code and ftp_reply are set either way. */
unsigned char ftp_command(const char *cmd);
unsigned char ftp_command2(const char *verb, const char *arg);
/* Opens a passive data connection (PASV, then connect on socket 1). */
unsigned char ftp_pasv(void);
/* Starts a transfer on the open data connection: sends the command and
 * waits for the 150/125 preliminary reply. */
unsigned char ftp_start_transfer(const char *verb, const char *arg);
/* Reads what the data connection has. Returns bytes copied (0 if none
 * yet); *done becomes 1 when the peer has finished and it is all read. */
unsigned int ftp_data_read(unsigned char *buf, unsigned int cap, unsigned char *done);
/* Queues bytes on the data connection; returns how many it took. */
unsigned int ftp_data_write(const unsigned char *buf, unsigned int len);
/* Closes the data connection and waits for the 226 completion reply. */
unsigned char ftp_finish_transfer(void);
void ftp_quit(void);

/* 1 while the control connection is established and the peer has not
 * closed or reset it. Servers drop idle sessions; the browser checks
 * before each command and reconnects. */
unsigned char ftp_ctrl_alive(void);

/* Words for ftp_error; for FTP_E_REPLY, the server's own line. */
const char *ftp_strerror(void);
/* The pump: polls the stack. Call it from every loop of your own. */
void ftp_poll(void);
/* Frames elapsed since the last ftp_poll() reset; for callers' timeouts. */
extern unsigned int ftp_frames;

#endif
