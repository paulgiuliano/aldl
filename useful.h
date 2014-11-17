#ifndef _USEFUL_H
#define _USEFUL_H

/************ SCOPE *********************************
  Useful but generic functions.
****************************************************/

/* this uses clock_gettime instead of gettimeofday, which rounds nanosecond
   clock instead of microsecond, and isnt affected by time jumps.  i think that
   it might introduce more overhead, though ... */
#define USEFUL_BETTERCLOCK

/* define to check malloc return values */
#undef MALLOC_ERRCHECK

/* clock source for USEFUL_BETTERCLOCK mode. */
#define _CLOCKSOURCE CLOCK_MONOTONIC

#ifdef USEFUL_BETTERCLOCK
#include <time.h>
typedef struct timespec timespec_t;
#else
#include <sys/time.h>
typedef struct timeval timespec_t;
#endif

#ifdef MALLOC_ERRCHECK
  /* a safe malloc that checks the return value and bails */
  void *smalloc(size_t size);
#else
  /* just macro malloc */
  #define smalloc(SIZE) malloc(SIZE)
#endif

/* get current time */
timespec_t get_time();

/* get the difference between the current time and the timestamp */
unsigned long get_elapsed_ms(timespec_t timestamp);

/* convert a 0xFF format string to a 'byte'... */
#define hextobyte(STR) (int)strtol(STR,NULL,16)

/* get a single bit from a byte.  xor with flip. */
#define getbit(P,BPOS,FLIP) (int)FLIP^(P>>BPOS&0x01)

/* generate a checksum byte */
byte checksum_generate(byte *buf, int len);

/* test checksum byte of buf, 1 if ok */
int checksum_test(byte *buf, int len);

/* compare a byte string n(eedle) in h(aystack), nonzero if found */
int cmp_bytestring(byte *h, int hsize, byte *n, int nsize);

/* print a string of bytes in hex format */
void printhexstring(byte *str, int length);

/* sleep for ms milliseconds */
#define msleep(...) usleep(__VA_ARGS__ * 1000)

#endif
