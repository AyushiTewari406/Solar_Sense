#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <string>

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define IRAM_ATTR
#define INPUT_PULLUP 2
#define FALLING 2

unsigned long millis();
inline void delay(unsigned long) {}
inline void pinMode(uint8_t, uint8_t) {}
inline uint8_t digitalPinToInterrupt(uint8_t p) { return p; }
inline void attachInterrupt(uint8_t, void (*)(), int) {}
inline void noInterrupts() {}
inline void interrupts() {}

#ifdef __MINGW32__
static inline struct tm* shim_localtime_r(const time_t* t, struct tm* out) {
  struct tm* r = localtime(t);
  if (r) *out = *r;
  return out;
}
#define localtime_r shim_localtime_r
#endif

typedef std::string String;

class Print {
 public:
  size_t print(const char* s) { return fputs(s, stdout) < 0 ? 0 : strlen(s); }
  size_t print(char c) { fputc(c, stdout); return 1; }
  size_t println(const char* s) { ::printf("%s\n", s); return strlen(s) + 1; }
  size_t println() { ::printf("\n"); return 1; }
  size_t printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n < 0 ? 0 : (size_t)n;
  }
};

class Stream : public Print {
 public:
  int available() { return 0; }
  int read() { return -1; }
};

class HardwareSerial : public Stream {
 public:
  void begin(unsigned long) {}
};
extern HardwareSerial Serial;

struct EspClass {
  uint32_t getFreeHeap() { return 240000; }
};
extern EspClass ESP;
