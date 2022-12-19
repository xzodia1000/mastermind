/*
  A C program to test the matching function (for master-mind) as implemented in matches.s

$ as  -o mm-matches.o mm-matches.s
$ gcc -c -o testm.o testm.c
$ gcc -o testm testm.o matches.o
$ ./testm
*/

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

#define LENGTH 3
#define COLORS 3

#define NAN1 8
#define NAN2 9

const int seqlen = LENGTH;
const int seqmax = COLORS;

/* ********************************** */
/* take these fcts from master-mind.c */
/* ********************************** */

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

/* counts how many entries in seq2 match entries in seq1 */
/* returns exact and approximate matches, either both encoded in one value, */
/* or as a pointer to a pair of values */
int *countMatches_C(int *seq1, int *seq2)
{
  int *data = (int *)malloc(2 * sizeof(int));

  data[0] = 0;
  data[1] = 0;

  int approx = 0;
  for (int i = 0; i < seqlen; i++)
  {
    if (seq1[i] == seq2[i])
    {
      if (seq2[i] == approx && data[1] != 0)
      {
        data[1]--;
      }
      data[0]++;
    }

    else
    {
      for (int j = 0; j < seqlen; j++)
      {
        if (seq1[i] == seq2[j] && i != j && seq2[j] != approx)
        {
          approx = seq2[j];
          data[1]++;
        }
      }
    }
  }

  return data;
}

/* show the results from calling countMatches on seq1 and seq1 */
void showMatches(int *code, int *seq1, int *seq2, int lcd_format)
{
  code = countMatches_C(seq1, seq2);
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

// The ARM assembler version of the matching fct
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
}

// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

int main(int argc, char **argv)
{
  int *res, *res_c, t, t_c, m, n;
  int *seq1, *seq2, *cpy1, *cpy2;
  struct timeval t1, t2;
  char str_in[20], str[20] = "some text";
  int verbose = 0, debug = 0, help = 0, opt_s = 0, opt_n = 0;

  // see: man 3 getopt for docu and an example of command line parsing
  { // see the CW spec for the intended meaning of these options
    int opt;
    while ((opt = getopt(argc, argv, "hvs:n:")) != -1)
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
      case 's':
        opt_s = atoi(optarg);
        break;
      case 'n':
        opt_n = atoi(optarg);
        break;
      default: /* '?' */
        fprintf(stderr, "Usage: %s [-h] [-v] [-s <seed>] [-n <no. of iterations>]  \n", argv[0]);
        exit(EXIT_FAILURE);
      }
    }
  }

  seq1 = (int *)malloc(seqlen * sizeof(int));
  seq2 = (int *)malloc(seqlen * sizeof(int));
  cpy1 = (int *)malloc(seqlen * sizeof(int));
  cpy2 = (int *)malloc(seqlen * sizeof(int));

  if (argc > optind + 1)
  {
    strcpy(str_in, argv[optind]);
    m = atoi(str_in);
    strcpy(str_in, argv[optind + 1]);
    n = atoi(str_in);
    fprintf(stderr, "Testing matches function with sequences %d and %d\n", m, n);
  }
  else
  {
    int i, j, n = 10, *res, *res_c, oks = 0, tot = 0; // number of test cases
    fprintf(stderr, "Running tests of matches function with %d pairs of random input sequences ...\n", n);
    if (opt_n != 0)
      n = opt_n;
    if (opt_s != 0)
      srand(opt_s);
    else
      srand(1701);
    for (i = 0; i < n; i++)
    {
      for (j = 0; j < seqlen; j++)
      {
        seq1[j] = (rand() % seqlen + 1);
        seq2[j] = (rand() % seqlen + 1);
      }
      memcpy(cpy1, seq1, seqlen * sizeof(int));
      memcpy(cpy2, seq2, seqlen * sizeof(int));
      if (verbose)
      {
        fprintf(stderr, "Random sequences are:\n");
        showSeq(seq1);
        showSeq(seq2);
      }
      res = countMatches(seq1, seq2); // extern; code in matches.s
      memcpy(seq1, cpy1, seqlen * sizeof(int));
      memcpy(seq2, cpy2, seqlen * sizeof(int));
      res_c = countMatches_C(seq1, seq2); // local C function
      if (debug)
      {
        fprintf(stdout, "DBG: sequences after matching:\n");
        showSeq(seq1);
        showSeq(seq2);
      }
      fprintf(stdout, "Matches (encoded) (in C):   %d %d\n", res_c[0], res_c[1]);
      fprintf(stdout, "Matches (encoded) (in Asm): %d %d\n", res[0], res[1]);
      memcpy(seq1, cpy1, seqlen * sizeof(int));
      memcpy(seq2, cpy2, seqlen * sizeof(int));
      showMatches(res_c, seq1, seq2, 0);
      showMatches(res, seq1, seq2, 0);
      tot++;
      if (res[0] == res_c[0] && res[1] == res_c[1])
      {
        fprintf(stdout, "__ result OK\n");
        oks++;
      }
      else
      {
        fprintf(stdout, "** result WRONG\n");
      }
    }
    fprintf(stderr, "%d out of %d tests OK\n", oks, tot);
    exit(oks == tot ? 0 : 1);
  }

  readSeq(seq1, m);
  readSeq(seq2, n);

  memcpy(cpy1, seq1, seqlen * sizeof(int));
  memcpy(cpy2, seq2, seqlen * sizeof(int));
  memcpy(seq1, cpy1, seqlen * sizeof(int));
  memcpy(seq2, cpy2, seqlen * sizeof(int));

  gettimeofday(&t1, NULL);
  res_c = countMatches_C(seq1, seq2); // local C function
  gettimeofday(&t2, NULL);
  // d = difftime(t1,t2);
  if (t2.tv_usec < t1.tv_usec) // Counter wrapped
    t_c = (1000000 + t2.tv_usec) - t1.tv_usec;
  else
    t_c = t2.tv_usec - t1.tv_usec;

  if (debug)
  {
    fprintf(stdout, "DBG: sequences after matching:\n");
    showSeq(seq1);
    showSeq(seq2);
  }
  memcpy(seq1, cpy1, seqlen * sizeof(int));
  memcpy(seq2, cpy2, seqlen * sizeof(int));

  gettimeofday(&t1, NULL);
  res = countMatches(seq1, seq2); // extern; code in hamming4.s
  gettimeofday(&t2, NULL);
  // d = difftime(t1,t2);
  if (t2.tv_usec < t1.tv_usec) // Counter wrapped
    t = (1000000 + t2.tv_usec) - t1.tv_usec;
  else
    t = t2.tv_usec - t1.tv_usec;

  if (debug)
  {
    fprintf(stdout, "DBG: sequences after matching:\n");
    showSeq(seq1);
    showSeq(seq2);
  }

  memcpy(seq1, cpy1, seqlen * sizeof(int));
  memcpy(seq2, cpy2, seqlen * sizeof(int));
  showMatches(res_c, seq1, seq2, 0);
  showMatches(res, seq1, seq2, 0);

  if (res[0] == res_c[0] && res[1] == res[1])
  {
    fprintf(stdout, "__ result OK\n");
  }
  else
  {
    fprintf(stdout, "** result WRONG\n");
  }
  fprintf(stderr, "C   version:\t\tresult=%d %d (elapsed time: %dms)\n", res_c[0], res_c[1], t_c);
  fprintf(stderr, "Asm version:\t\tresult=%d %d (elapsed time: %dms)\n", res[0], res[1], t);

  return 0;
}

