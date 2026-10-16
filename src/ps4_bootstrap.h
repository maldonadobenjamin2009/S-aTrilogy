#ifndef SAT_PS4_BOOTSTRAP_H
#define SAT_PS4_BOOTSTRAP_H

/* PS4-only bootstrap, early diagnostics and startup watchdog. */
void ps4_bootstrap_init(void);
void ps4_bootstrap_log(const char *fmt, ...);
void ps4_bootstrap_note_frame(void);

#endif
