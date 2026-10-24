/*
 * Describes a crash of pico-8 in the pak's log. A bus error or segmentation
 * fault otherwise leaves only "Bus error" from the shell, with nothing to say
 * where it happened.
 */

#ifndef CRASH_H
#define CRASH_H

/*
 * Catches SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT. Each is logged with the
 * address it happened at, the file mapped there, the instruction that raised
 * it and a backtrace, and then handed back to whatever handled it before, so
 * pico-8 still ends with the same signal and launch.sh sees the same status.
 */
void crash_report_install(void);

#endif
