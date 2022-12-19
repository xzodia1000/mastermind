/*
 * MasterMind implementation: template; see comments below on which parts need to be completed
 * CW spec: https://www.macs.hw.ac.uk/~hwloidl/Courses/F28HS/F28HS_CW2_2022.pdf
 * This repo: https://gitlab-student.macs.hw.ac.uk/f28hs-2021-22/f28hs-2021-22-staff/f28hs-2021-22-cwk2-sys

 * Compile:
 gcc -c -o lcdBinary.o lcdBinary.c
 gcc -c -o master-mind.o master-mind.c
 gcc -o master-mind master-mind.o lcdBinary.o
 * Run:
 sudo ./master-mind

 OR use the Makefile to build
 > make all
 and run
 > make run
 and test
 > make test

 ***********************************************************************
 * The Low-level interface to LED, button, and LCD is based on:
 * wiringPi libraries by
 * Copyright (c) 2012-2013 Gordon Henderson.
 ***********************************************************************
 * See:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with wiringPi.  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
*/

/* ======================================================= */
/* SECTION: includes                                       */
/* ------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <unistd.h>
#include <string.h>
#include <time.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

/* --------------------------------------------------------------------------- */
/* Config settings */
/* you can use CPP flags to e.g. print extra debugging messages */
/* or switch between different versions of the code e.g. digitalWrite() in Assembler */
#define DEBUG
#undef ASM_CODE

// =======================================================
// Tunables
// PINs (based on BCM numbering)
// For wiring see CW spec: https://www.macs.hw.ac.uk/~hwloidl/Courses/F28HS/F28HS_CW2_2022.pdf
// GPIO pin for green LED
#define LED 13
// GPIO pin for red LED
#define LED2 5
// GPIO pin for button
#define BUTTON 19
// =======================================================
// delay for loop iterations (mainly), in ms
// in mili-seconds: 0.2s
#define DELAY 200
// in micro-seconds: 3s
#define TIMEOUT 3000000
// =======================================================
// APP constants   ---------------------------------
// number of colours and length of the sequence
#define COLS 3
#define SEQL 3
// =======================================================

// generic constants

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

/* ======================================================= */
/* SECTION: constants and prototypes                       */
/* ------------------------------------------------------- */

/* Constants */

static const int colors = COLS;
static const int seqlen = SEQL;

static char *color_names[] = {"red", "green", "blue"};

static int *theSeq = NULL;

static int *seq1, *seq2;

/* --------------------------------------------------------------------------- */

// Mask for the bottom 64 pins which belong to the Raspberry Pi
//	The others are available for the other devices

#define PI_GPIO_MASK (0xFFFFFFC0)

static unsigned int gpiobase;
static uint32_t *gpio;

static int timed_out = 0;

/* ------------------------------------------------------- */
// misc prototypes

int failure(int fatal, const char *message, ...);
void waitForEnter(void);
void waitForButton(uint32_t *gpio, int button);

/* ======================================================= */
/* SECTION: hardware interface (LED, button)  */
/* ------------------------------------------------------- */
/* low-level interface to the hardware */

/* set the @mode@ of a GPIO @pin@ to INPUT or OUTPUT; @gpio@ is the mmaped GPIO base address */
void pinMode(uint32_t *gpio, int pin, int mode);

/* send a @value@ (LOW or HIGH) on pin number @pin@; @gpio@ is the mmaped GPIO base address */
void writeLED(uint32_t *gpio, int led, int value);

/* read a @value@ (LOW or HIGH) from pin number @pin@ (a button device); @gpio@ is the mmaped GPIO base address */
int readButton(uint32_t *gpio, int button);

/* wait for a button input on pin number @button@; @gpio@ is the mmaped GPIO base address */
void waitForButton(uint32_t *gpio, int button);

/* ======================================================= */
/* SECTION: game logic                                     */
/* ------------------------------------------------------- */
/* AUX fcts of the game logic */

/* initialise the secret sequence; by default it should be a random sequence */
void initSeq()
{
  theSeq = (int *)malloc(seqlen * sizeof(int));

  srand(time(0));

  for (int i = 0; i < seqlen; i++)
  {
    theSeq[i] = (rand() % 3) + 1;
  }
}

/* display the sequence on the terminal window, using the format from the sample run in the spec */
void showSeq(int *seq)
{
  fprintf(stdout, "Secret: ");
  for (int i = 0; i < seqlen; i++)
  {
    fprintf(stdout, "%d ", seq[i]);
  }

  fprintf(stdout, "\n");
}

#define NAN1 8
#define NAN2 9

/* counts how many entries in seq2 match entries in seq1 */
/* returns exact and approximate matches */
/* as a pointer to a pair of values */
int *countMatches(int *seq1, int *seq2)
{
  int *data = (int *)malloc(2 * sizeof(int)); // variable to store the matches

  int res_exact = 0;
  int res_approx = 0;

  asm(
      "start:\n"
      "\tMOV R0, #0\n" // exact
      "\tMOV R3, #0\n" // approx
      "\tMOV R1, %[seq1]\n"
      "\tMOV R2, %[seq2]\n"
      "\tMOV R4, #0\n" // approx indicator
      "\tMOV R5, #0\n" // length 1
      "\tMOV R7, #0\n" // index 1
      "\tMOV R6, #0\n" // length 2
      "\tMOV R8, #0\n" // index 2
      "\tB main_loop\n"

      "main_loop:\n"
      "\tCMP R5, #3\n"
      "\tBEQ exit_routine\n"

      "\tLDR R9, [R1, R7]\n"
      "\tLDR R10, [R2, R7]\n"

      "\tCMP R9, R10\n"
      "\tBEQ add_exact\n"

      "\tMOV R6, #0\n"
      "\tMOV R8, #0\n"
      "\rB approx_loop\n"
      "\tB loop_increment1\n"

      "loop_increment1:\n"
      "\tADD R5, R5, #1\n"
      "\tADD R7, R7, #4\n"
      "\tB main_loop\n"

      "add_exact:\n"
      "\tADD R0, R0, #1\n"
      "\tCMP R10, R4\n"
      "\tBEQ check_approx_size\n"
      "\tB loop_increment1\n"

      "check_approx_size:\n"
      "\tCMP R3, #0\n"
      "\tBNE decrement_approx\n"
      "\tB loop_increment1\n"

      "decrement_approx:\n"
      "\tSUB R3, R3, #1\n"
      "\tB loop_increment1\n"

      "approx_loop:\n"
      "\tCMP R6, #3\n"
      "\tBEQ loop_increment1\n"

      "\tLDR R10, [R2, R8]\n"

      "\tCMP R9, R10\n"
      "\tBEQ index_check\n"
      "\tB loop_increment2\n"

      "loop_increment2:\n"
      "\tADD R6, R6, #1\n"
      "\tADD R8, R8, #4\n"
      "\tB approx_loop\n"

      "index_check:\n"
      "\tCMP R5, R6\n"
      "\tBNE approx_check\n"
      "\tB loop_increment2\n"

      "approx_check:\n"
      "\tCMP R10, R4\n"
      "\tBNE add_approx\n"
      "\tB loop_increment2\n"

      "add_approx:\n"
      "\tMOV R4, R10\n"
      "\tADD R3, R3, #1\n"
      "\tMOV R6, #2\n"
      "\tB loop_increment2\n"

      "exit_routine:\n"
      "\tMOV %[result_exact], R0\n"
      "\tMOV %[result_approx], R3\n"

      : [result_exact] "=r"(res_exact), [result_approx] "=r"(res_approx)
      : [seq1] "r"(seq1), [seq2] "r"(seq2), [seqlen] "r"(seqlen)
      : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "cc");

  data[0] = res_exact;
  data[1] = res_approx;

  return data;

  // int *data = (int *)malloc(2 * sizeof(int));

  // data[0] = 0;
  // data[1] = 0;

  // int approx = 0;
  // for (int i = 0; i < seqlen; i++)
  // {
  //   if (seq1[i] == seq2[i])
  //   {
  //     if (seq2[i] == approx && data[1] != 0)
  //     {
  //       data[1]--;
  //     }
  //     data[0]++;
  //   }

  //   else
  //   {
  //     for (int j = 0; j < seqlen; j++)
  //     {
  //       if (seq1[i] == seq2[j] && i != j && seq2[j] != approx)
  //       {
  //         approx = seq2[j];
  //         data[1]++;
  //       }
  //     }
  //   }
  // }

  // return data;
}

/* show the results from calling countMatches on seq1 and seq1 */
void showMatches(int *code, int *seq1, int *seq2, int lcd_format)
{
  code = countMatches(seq1, seq2); // calculating the matches
  fprintf(stdout, "%d exact\n", code[0]);
  fprintf(stdout, "%d approximate\n", code[1]);
}

/* parse an integer value as a list of digits, and put them into @seq@ */
/* needed for processing command-line with options -s or -u            */
void readSeq(int *seq, int val)
{
  int length = seqlen;

  while (length != 0)
  {
    /* Accessing the integer digit by digit */
    seq[length - 1] = val % 10;
    val /= 10;
    length--;
  }
}

/* read a guess sequence fron stdin and store the values in arr */
/* only needed for testing the game logic, without button input */
int readNum(int max)
{
  int seq[max];
  int tmp;

  for (int i = 0; i < max; i++)
  {
    /* Gets input from the user and stores into an array */
    scanf("Enter number: %d", &tmp);
    seq[i] = tmp;
  }

  return tmp;
}

/* ======================================================= */
/* SECTION: TIMER code                                     */
/* ------------------------------------------------------- */

/* timestamps needed to implement a time-out mechanism */
static uint64_t startT, stopT;

/* you may need this function in timer_handler() below  */
/* use the libc fct gettimeofday() to implement it      */
uint64_t timeInMicroseconds()
{
  struct timeval tv;
  uint64_t now;
  gettimeofday(&tv, NULL);
  now = (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)tv.tv_usec;
  return (uint64_t)now;
}

/* this should be the callback, triggered via an interval timer, */
/* that is set-up through a call to sigaction() in the main fct. */
void timer_handler(int signum)
{
  static int count = 0;
  stopT = timeInMicroseconds();
  count++;
  fprintf(stderr, "Timer expired %d times. Time took: %f\n", count, (stopT - startT) / 1000000.0);
  timed_out = 1;
}

/* initialise time-stamps, setup an interval timer, and install the timer_handler callback */
void initITimer(uint64_t timeout)
{
  struct sigaction sa;
  struct itimerval timer;

  /* setting the signale handler for when the timer expires */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &timer_handler;

  sigaction(SIGALRM, &sa, NULL);

  /* specifications for a non recurring timer */
  timer.it_value.tv_sec = timeout;
  timer.it_value.tv_usec = 0;

  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 0;

  setitimer(ITIMER_REAL, &timer, NULL);

  startT = timeInMicroseconds();
}

/* ======================================================= */
/* SECTION: Aux function                                   */
/* ------------------------------------------------------- */

int failure(int fatal, const char *message, ...)
{
  va_list argp;
  char buffer[1024];

  if (!fatal) //  && wiringPiReturnCodes)
    return -1;

  va_start(argp, message);
  vsnprintf(buffer, 1023, message, argp);
  va_end(argp);

  fprintf(stderr, "%s", buffer);
  exit(EXIT_FAILURE);

  return 0;
}

/*
 * waitForEnter:
 *********************************************************************************
 */

void waitForEnter(void)
{
  printf("Press ENTER to continue: ");
  (void)fgetc(stdin);
}

/*
 * delay:
 *	Wait for some number of milliseconds
 *********************************************************************************
 */

void delay(unsigned int howLong)
{
  struct timespec sleeper, dummy;

  sleeper.tv_sec = (time_t)(howLong / 1000);
  sleeper.tv_nsec = (long)(howLong % 1000) * 1000000;

  nanosleep(&sleeper, &dummy);
}

/* From wiringPi code; comment by Gordon Henderson
 * delayMicroseconds:
 *	This is somewhat intersting. It seems that on the Pi, a single call
 *	to nanosleep takes some 80 to 130 microseconds anyway, so while
 *	obeying the standards (may take longer), it's not always what we
 *	want!
 *
 *	So what I'll do now is if the delay is less than 100uS we'll do it
 *	in a hard loop, watching a built-in counter on the ARM chip. This is
 *	somewhat sub-optimal in that it uses 100% CPU, something not an issue
 *	in a microcontroller, but under a multi-tasking, multi-user OS, it's
 *	wastefull, however we've no real choice )-:
 *
 *      Plan B: It seems all might not be well with that plan, so changing it
 *      to use gettimeofday () and poll on that instead...
 *********************************************************************************
 */

void delayMicroseconds(unsigned int howLong)
{
  struct timespec sleeper;
  unsigned int uSecs = howLong % 1000000;
  unsigned int wSecs = howLong / 1000000;

  /**/ if (howLong == 0)
    return;
#if 0
  else if (howLong  < 100)
    delayMicrosecondsHard (howLong) ;
#endif
  else
  {
    sleeper.tv_sec = wSecs;
    sleeper.tv_nsec = (long)(uSecs * 1000L);
    nanosleep(&sleeper, NULL);
  }
}

/* ======================================================= */
/* SECTION: aux functions for game logic                   */
/* ------------------------------------------------------- */

/* interface on top of the low-level pin I/O code */

/* blink the led on pin @led@, @c@ times */
void blinkN(uint32_t *gpio, int led, int c)
{
  /* ***  COMPLETE the code here  ***  */
  for (int i = 0; i < c; i++)
  {
    /* turns the led on and off with  certain delay */
    writeLED(gpio, led, HIGH);
    delay(700);
    writeLED(gpio, led, LOW);
    delay(700);
  }

  delay(500);
}

/* ======================================================= */
/* SECTION: main fct                                       */
/* ------------------------------------------------------- */

int main(int argc, char *argv[])
{

  int found = 0, attempts = 0, *result;
  int *attSeq;

  int pinLED = LED, pin2LED2 = LED2, pinButton = BUTTON;
  int fd;

  // variables for command-line processing
  char str_in[20], str[20] = "some text";
  int verbose = 0, debug = 0, help = 0, opt_m = 0, opt_n = 0, opt_s = 0, unit_test = 0;

  // -------------------------------------------------------
  // process command-line arguments

  // see: man 3 getopt for docu and an example of command line parsing
  { // see the CW spec for the intended meaning of these options
    int opt;
    while ((opt = getopt(argc, argv, "hvdus:")) != -1)
    {
      switch (opt)
      {
      case 'v':
        verbose = 1;
        break;
      case 'h':
        help = 1;
        break;
      case 'd':
        debug = 1;
        break;
      case 'u':
        unit_test = 1;
        break;
      case 's':
        opt_s = atoi(optarg);
        break;
      default: /* '?' */
        fprintf(stderr, "Usage: %s [-h] [-v] [-d] [-u <seq1> <seq2>] [-s <secret seq>]  \n", argv[0]);
        exit(EXIT_FAILURE);
      }
    }
  }

  if (help)
  {
    fprintf(stderr, "MasterMind program, running on a Raspberry Pi, with connected LED, button and LCD display\n");
    fprintf(stderr, "Use the button for input of numbers. The LCD display will show the matches with the secret sequence.\n");
    fprintf(stderr, "For full specification of the program see: https://www.macs.hw.ac.uk/~hwloidl/Courses/F28HS/F28HS_CW2_2022.pdf\n");
    fprintf(stderr, "Usage: %s [-h] [-v] [-d] [-u <seq1> <seq2>] [-s <secret seq>]  \n", argv[0]);
    exit(EXIT_SUCCESS);
  }

  if (unit_test && optind >= argc - 1)
  {
    fprintf(stderr, "Expected 2 arguments after option -u\n");
    exit(EXIT_FAILURE);
  }

  if (verbose && unit_test)
  {
    printf("1st argument = %s\n", argv[optind]);
    printf("2nd argument = %s\n", argv[optind + 1]);
  }

  if (verbose)
  {
    fprintf(stdout, "Settings for running the program\n");
    fprintf(stdout, "Verbose is %s\n", (verbose ? "ON" : "OFF"));
    fprintf(stdout, "Debug is %s\n", (debug ? "ON" : "OFF"));
    fprintf(stdout, "Unittest is %s\n", (unit_test ? "ON" : "OFF"));
    if (opt_s)
      fprintf(stdout, "Secret sequence set to %d\n", opt_s);
  }

  seq1 = (int *)malloc(seqlen * sizeof(int));
  seq2 = (int *)malloc(seqlen * sizeof(int));

  // check for -u option, and if so run a unit test on the matching function
  if (unit_test && argc > optind + 1)
  { // more arguments to process; only needed with -u
    strcpy(str_in, argv[optind]);
    opt_m = atoi(str_in);
    strcpy(str_in, argv[optind + 1]);
    opt_n = atoi(str_in);
    // CALL a test-matches function; see testm.c for an example implementation
    readSeq(seq1, opt_m); // turn the integer number into a sequence of numbers
    readSeq(seq2, opt_n); // turn the integer number into a sequence of numbers
    if (verbose)
      fprintf(stdout, "Testing matches function with sequences %d and %d\n", opt_m, opt_n);
    int *res_matches = countMatches(seq1, seq2);
    showMatches(res_matches, seq1, seq2, 1);
    exit(EXIT_SUCCESS);
  }
  else
  {
    /* nothing to do here; just continue with the rest of the main fct */
  }

  if (opt_s)
  { // if -s option is given, use the sequence as secret sequence
    if (theSeq == NULL)
      theSeq = (int *)malloc(seqlen * sizeof(int));
    readSeq(theSeq, opt_s);
    if (verbose)
    {
      fprintf(stderr, "Running program with secret sequence:\n");
      showSeq(theSeq);
    }
  }

  if (geteuid() != 0)
    fprintf(stderr, "setup: Must be root. (Did you forget sudo?)\n");

  // -----------------------------------------------------------------------------
  // constants for RPi2
  gpiobase = 0x3F200000;

  // -----------------------------------------------------------------------------
  // memory mapping
  // Open the master /dev/memory device

  if ((fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC)) < 0)
    return failure(FALSE, "setup: Unable to open /dev/mem: %s\n", strerror(errno));

  // GPIO:
  gpio = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gpiobase);
  if ((int32_t)gpio == -1)
    return failure(FALSE, "setup: mmap (GPIO) failed: %s\n", strerror(errno));

  // -------------------------------------------------------
  // Configuration of LED and BUTTON

  pinMode(gpio, pinLED, OUTPUT);
  pinMode(gpio, pin2LED2, OUTPUT);
  pinMode(gpio, pinButton, INPUT);

  // init of guess sequence, and copies (for use in countMatches)
  attSeq = (int *)malloc(seqlen * sizeof(int));

  // -----------------------------------------------------------------------------
  // Start of game
  fprintf(stderr, "Game Start\n");

  /* initialise the secret sequence */
  if (!opt_s)
    initSeq();
  if (debug)
    showSeq(theSeq);

  // -----------------------------------------------------------------------------
  // +++++ main loop

  fprintf(stdout, "\n");

  while (!found)
  {
    attempts++;

    blinkN(gpio, pin2LED2, 3);
    fprintf(stdout, "Round %d\n", attempts);
    printf("\n");

    /* defining the guess sequence numbers to calculate the input */
    attSeq[0] = 0;
    attSeq[1] = 0;
    attSeq[2] = 0;

    for (int i = 0; i < seqlen; i++)
    {
      /* Gets 3 numbers from the user to from the guess sequence */
      waitForButton(gpio, pinButton);

      timed_out = 0; // variable to indicate the timer
      initITimer(5); // initilializing the timer

      while (!timed_out)
      {
        /* gets input from the user until the timer expires */
        if (readButton(gpio, pinButton) != 0)
        {
          attSeq[i]++;
          fprintf(stderr, "Button Pressed\n");
        }

        delay(DELAY);
      }

      if (attSeq[i] > 3)
      {
        /* sets the number to 3 if the user pressed the button more than 3 times */
        attSeq[i] = 3;
      }

      fprintf(stdout, "Input: %d\n", attSeq[i]); // prints the inputted number to the stdout

      blinkN(gpio, pin2LED2, 1);
      blinkN(gpio, pinLED, attSeq[i]); // blinks the green led based on the input
      fprintf(stdout, "\n");
    }

    blinkN(gpio, pin2LED2, 2);

    result = countMatches(theSeq, attSeq); // calculates the exact and approximate matches

    if (result[0] == 3)
    {
      found = 1;
    }

    else if (attempts == 5)
    {
      /* exists the game after 5 rounds */
      break;
    }

    else
    {
      showMatches(result, theSeq, attSeq, 1); // prints the exact and approximate matches to the stdout
      fprintf(stdout, "\n");

      blinkN(gpio, pinLED, result[0]); // blinks the green led based on exact matches
      blinkN(gpio, pin2LED2, 1);       // red led as separator
      blinkN(gpio, pinLED, result[1]); // blinks the green led based on approximate matches
    }
  }
  if (found)
  {

    /* when the sequence is guessed correctly */
    fprintf(stdout, "Game completed in %d rounds\n", attempts);

    writeLED(gpio, pin2LED2, HIGH);
    blinkN(gpio, pinLED, 3);
    writeLED(gpio, pin2LED2, LOW);

    fprintf(stdout, "SUCCESS\n");
  }
  else
  {
    fprintf(stdout, "Sequence not found\n");
  }
  return 0;
}

