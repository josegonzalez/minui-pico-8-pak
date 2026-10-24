/*
 * Logs a crash of pico-8. See crash.h.
 *
 * The handler runs in the middle of whatever went wrong, so it sticks to calls
 * that are safe there: write, open and read, and glibc's backtrace, which is
 * called once on install so it has nothing left to load when it is needed.
 */

#define _GNU_SOURCE
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "crash.h"

#define FRAMES 32

static const int crash_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#define SIGNAL_COUNT ((int)(sizeof crash_signals / sizeof *crash_signals))

static struct sigaction previous[SIGNAL_COUNT];
/* a stack of its own, as a crash may be the stack running out */
static char alternate_stack[64 * 1024];

static void put(const char *text)
{
  size_t length = strlen(text);

  while (length > 0) {
    ssize_t written = write(STDERR_FILENO, text, length);
    if (written <= 0) {
      return;
    }
    text += written;
    length -= (size_t)written;
  }
}

static void put_hex(uintptr_t value)
{
  char digits[2 + sizeof value * 2 + 1];
  int i;

  digits[0] = '0';
  digits[1] = 'x';
  for (i = 0; i < (int)sizeof value * 2; i++) {
    digits[2 + i] = "0123456789abcdef"[(value >> ((sizeof value * 2 - 1 - i) * 4)) & 0xf];
  }
  digits[sizeof digits - 1] = '\0';
  put(digits);
}

static void put_number(long value)
{
  char digits[24];
  int i = (int)sizeof digits - 1;
  int negative = value < 0;
  unsigned long magnitude = negative ? 0ul - (unsigned long)value : (unsigned long)value;

  digits[i] = '\0';
  do {
    digits[--i] = (char)('0' + magnitude % 10);
    magnitude /= 10;
  } while (magnitude > 0 && i > 1);
  if (negative) {
    digits[--i] = '-';
  }
  put(digits + i);
}

/* the stamp gfx_log puts on every other line, the seconds since boot */
static void put_stamp(void)
{
  char uptime[32];
  ssize_t got;
  int fd = open("/proc/uptime", O_RDONLY);

  put("[");
  if (fd >= 0) {
    got = read(fd, uptime, sizeof uptime - 1);
    close(fd);
    if (got > 0) {
      uptime[got] = '\0';
      uptime[strcspn(uptime, " \n")] = '\0';
      put(uptime);
    }
  }
  put("] sdl-display: ");
}

static const char *signal_name(int signal_number)
{
  switch (signal_number) {
  case SIGSEGV:
    return "SIGSEGV";
  case SIGBUS:
    return "SIGBUS";
  case SIGILL:
    return "SIGILL";
  case SIGFPE:
    return "SIGFPE";
  default:
    return "SIGABRT";
  }
}

static uintptr_t parse_hex(const char *text, const char **end)
{
  uintptr_t value = 0;

  for (;; text++) {
    char c = *text;
    if (c >= '0' && c <= '9') {
      value = value * 16 + (uintptr_t)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value = value * 16 + (uintptr_t)(c - 'a' + 10);
    } else {
      break;
    }
  }
  *end = text;
  return value;
}

/*
 * Logs the line of /proc/self/maps holding address, which names the file it
 * belongs to, read a block at a time as no memory may be allocated here.
 */
static void put_mapping(const char *what, uintptr_t address)
{
  char block[4096], line[512];
  size_t length = 0;
  ssize_t got, i;
  int fd = open("/proc/self/maps", O_RDONLY);

  if (fd < 0) {
    return;
  }

  while ((got = read(fd, block, sizeof block)) > 0) {
    for (i = 0; i < got; i++) {
      const char *end;
      uintptr_t start, stop;

      if (block[i] != '\n') {
        if (length < sizeof line - 1) {
          line[length++] = block[i];
        }
        continue;
      }

      line[length] = '\0';
      length = 0;
      start = parse_hex(line, &end);
      if (*end != '-') {
        continue;
      }
      stop = parse_hex(end + 1, &end);
      if (address >= start && address < stop) {
        put_stamp();
        put(what);
        put(" is in ");
        put(line);
        put(", at ");
        put_hex(address - start);
        put(" into the mapping\n");
        close(fd);
        return;
      }
    }
  }

  close(fd);
  put_stamp();
  put(what);
  put(" is in no mapped file\n");
}

static uintptr_t instruction_address(void *context)
{
  ucontext_t *state = context;

  if (state == NULL) {
    return 0;
  }
#if defined(__aarch64__)
  return (uintptr_t)state->uc_mcontext.pc;
#elif defined(__arm__)
  return (uintptr_t)state->uc_mcontext.arm_pc;
#elif defined(__x86_64__)
  return (uintptr_t)state->uc_mcontext.gregs[REG_RIP];
#else
  return 0;
#endif
}

static void handle_crash(int signal_number, siginfo_t *info, void *context)
{
  void *frames[FRAMES];
  uintptr_t pc = instruction_address(context);
  int count, i;

  put_stamp();
  put("pico-8 crashed with ");
  put(signal_name(signal_number));
  put(" (code ");
  put_number(info != NULL ? info->si_code : 0);
  put(") at ");
  put_hex(info != NULL ? (uintptr_t)info->si_addr : 0);
  put(", running ");
  put_hex(pc);
  put("\n");

  if (info != NULL && info->si_addr != NULL) {
    put_mapping("the address", (uintptr_t)info->si_addr);
  }
  if (pc != 0) {
    put_mapping("the instruction", pc);
  }

  put_stamp();
  put("backtrace:\n");
  count = backtrace(frames, FRAMES);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);

  /*
   * Handed back to what handled it before, usually the default of ending the
   * process. The signal is held while this runs, so raising it again delivers
   * it on the way out, and a fault that returns would happen again anyway.
   */
  for (i = 0; i < SIGNAL_COUNT; i++) {
    if (crash_signals[i] == signal_number) {
      sigaction(signal_number, &previous[i], NULL);
    }
  }
  raise(signal_number);
}

void crash_report_install(void)
{
  static int installed;
  struct sigaction action;
  stack_t stack;
  void *warm[1];
  int i;

  if (installed) {
    return;
  }
  installed = 1;

  /* backtrace loads the unwinder the first time, which must not happen in the handler */
  backtrace(warm, 1);

  stack.ss_sp = alternate_stack;
  stack.ss_size = sizeof alternate_stack;
  stack.ss_flags = 0;
  sigaltstack(&stack, NULL);

  memset(&action, 0, sizeof action);
  action.sa_sigaction = handle_crash;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&action.sa_mask);
  for (i = 0; i < SIGNAL_COUNT; i++) {
    sigaction(crash_signals[i], &action, &previous[i]);
  }
}
