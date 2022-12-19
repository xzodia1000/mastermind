/* ***************************************************************************** */
/* You can use this file to define the low-level hardware control fcts for       */
/* LED, button and LCD devices.                                                  */
/* Note that these need to be implemented in Assembler.                          */
/* You can use inline Assembler code, or use a stand-alone Assembler file.       */
/* Alternatively, you can implement all fcts directly in master-mind.c,          */
/* using inline Assembler code there.                                            */
/* The Makefile assumes you define the functions here.                           */
/* ***************************************************************************** */

#ifndef TRUE
#define TRUE (1 == 1)
#define FALSE (1 == 2)
#endif

#define PAGE_SIZE (4 * 1024)
#define BLOCK_SIZE (4 * 1024)

#define INPUT 0
#define OUTPUT 1

#define LOW 0
#define HIGH 1

// APP constants   ---------------------------------

// Tunables
// PINs (based on BCM numbering)
// GPIO pin for green LED
#define LED 13
// GPIO pin for red LED
#define LED2 5
// GPIO pin for button
#define BUTTON 19

// -----------------------------------------------------------------------------
// includes
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

// -----------------------------------------------------------------------------
// prototypes

int failure(int fatal, const char *message, ...);

// -----------------------------------------------------------------------------

/* this version needs gpio as argument, because it is in a separate file */
void digitalWrite(uint32_t *gpio, int pin, int value)
{
}

void pinMode(uint32_t *gpio, int pin, int mode)
{
  int fsel, shift, res;

  switch (pin)
  {
  case LED:
    fsel = 1;
    shift = 9;
    break;

  case LED2:
    fsel = 0;
    shift = 15;
    break;

  case BUTTON:
    fsel = 1;
    shift = 27;
    break;

  default:
    failure(TRUE, "pinMode: pin %d not supported\n", pin);
    break;
  }

  asm(
      "\tB _bonzo0\n"
      "_bonzo0:\n"
      "\tLDR R1, %[gpio]\n"
      "\tADD R0, R1, %[fsel]\n"
      "\tLDR R1, [R0, #0]\n"
      "\tMOV R2, #0b111\n"
      "\tLSL R2, %[shift]\n"
      "\tBIC R1, R1, R2\n"
      "\tMOV R2, %[mode]\n"
      "\tLSL R2, %[shift]\n"
      "\tORR R1, R2\n"
      "\tSTR R1, [R0, #0]\n"
      "\tMOV %[result], R1\n"
      : [result] "=r"(res)
      : [act] "r"(pin), [gpio] "m"(gpio), [fsel] "r"(fsel * 4),
        [shift] "r"(shift), [mode] "r"(mode)
      : "r0", "r1", "r2", "cc");
}

void writeLED(uint32_t *gpio, int led, int value)
{
  int off, res;

  switch (led)
  {
  case LED:
    off = (value == LOW) ? 10 : 7;
    break;

  case LED2:
    off = (value == LOW) ? 10 : 7;
    break;

  default:
    failure(TRUE, "writeLED: pin %d not supported\n", led);
  }

  asm volatile(
      "\tB   _bonzo1\n"
      "_bonzo1:\n"
      "\tLDR R1, %[gpio]\n"
      "\tADD R0, R1, %[off]\n"
      "\tMOV R2, #1\n"
      "\tMOV R1, %[led]\n"
      "\tAND R1, #31\n"
      "\tLSL R2, R1\n"
      "\tSTR R2, [R0, #0]\n"
      "\tMOV %[result], R2\n"
      : [result] "=r"(res)
      : [led] "r"(led), [gpio] "m"(gpio), [off] "r"(off * 4)
      : "r0", "r1", "r2", "cc");
}

int readButton(uint32_t *gpio, int button)
{

  int res;
  int off;

  switch (button)
  {
  case BUTTON:
    off = 13;
    break;

  default:
    failure(TRUE, "readButton: pin %d not supported\n", button);
    break;
  }

  asm(
      "\tB   _bonzo2\n"
      "_bonzo2:\n"
      "\tLDR R0, %[gpio]\n"
      "\tADD R1, R0, %[off]\n"
      "\tLDR R0, [R1]\n"
      "\tMOV R2, #1\n"
      "\tMOV R1, %[button]\n"
      "\tAND R1, #31\n"
      "\tLSL R2, R1\n"
      "\tAND R2, R0\n"
      "\tMOV %[result], R2\n"
      : [result] "=r"(res)
      : [button] "r"(button), [gpio] "m"(gpio), [off] "r"(off * 4)
      : "r0", "r1", "r2", "cc");

  return res;
}

void waitForButton(uint32_t *gpio, int button)
{
  while (readButton(gpio, button) == 0)
  {
  }
}

