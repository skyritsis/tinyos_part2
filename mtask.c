#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>

#include "syscalls.h"


/* We use the fibo function to "burn" CPU cycle, computing Fibonacci numbers 
   in exponential time. The constant FBASE below can be used to adjusted the
   work performed by this program. 
*/
#define FBASE 35
#define FGAP  10
int FMIN, FMAX;
unsigned int fiborand() { return lrand48() % (FMAX-FMIN+1) + FMIN; }

unsigned int fibo(unsigned int n) /* Very slow routine */
{
  /* 
     Updated 2006: gcc-4.1 can optimize the recursion, making fibo run 
     in linear time, instead of exponential time.
     To prevent the compiler from optimizing the tail recursion,
     add a dummy side-effect, in the form of incrementing a static counter "x".
  */
  static long x = 0;
  x++;
  if(n<2) return n;
  else return fibo(n-1)+fibo(n-2);
}

/* Dining Philosophers */
int N=0;			/* Number of philosophers */
/* utilities */
int LEFT(int i) { return (i+1) % N; }
int RIGHT(int i) { return (i+N-1) % N; }

typedef enum { NOTHERE=0, THINKING, HUNGRY, EATING } PHIL;
PHIL* state;			/* state[N]: Philosopher state */

Mutex mx = MUTEX_INIT;		/* Mutual exclusion among philosophers (on
				 array 'state') */
CondVar *hungry;		/* hungry[N]: put hungry philosopher to sleep */

/* Prints the current state given a change (described by fmt) for
 philosopher ph */
void print_state(const char* fmt, int ph)
{
  int i;
  if(N<100) {
    for(i=0;i<N;i++) {
      char c= (".THE")[state[i]];
      if(i==ph) printf("[%c]", c); else printf(" %c ", c);
    }
  }
  printf(fmt, ph);
}

/* Functions think and eat (just burn CPU cycles). */
void think(int i) { fibo(fiborand()); }
void eat(int i)  { fibo(fiborand()); }

/* Attempt to make a (hungry) philosopher i to start eating */
void trytoeat(int i)
{
  if(state[i]==HUNGRY && state[LEFT(i)]!=EATING && state[RIGHT(i)]!=EATING) {
    state[i] = EATING;
    print_state("     %d is eating\n",i);
    Cond_Signal(&(hungry[i]));
  }
}

/* 
   PROGRAM:  Philosopher
   A program that simulates a philosopher participating in a dining philosophers' 
   symposium. Each philosopher will join the symposium, alternating between 
   thinking, going hungry and eating a bite, for a number of bites. After he 
   has eaten the last bite, he leaves.
*/
int Philosopher(int argl, void* args)
{
  int i,j;
  int bites;			/* Number of bites (mpoykies) */

  assert(argl == sizeof(int[2]));
  i = ((int*)args)[0];
  bites = ((int*)args)[1];

  Mutex_Lock(&mx);		/* Philosopher arrives in thinking state */
  state[i] = THINKING;
  print_state("     %d has arrived\n",i);
  Mutex_Unlock(&mx);

  for(j=0; j<bites; j++) {

    think(i);			/* THINK */

    Mutex_Lock(&mx);		/* GO HUNGRY */
    state[i] = HUNGRY;
    trytoeat(i);		/* This may not succeed */
    while(state[i]==HUNGRY) {
      print_state("     %d waits hungry\n",i);
      Cond_Wait(&mx, &(hungry[i])); /* If hungry we sleep. trytoeat(i) will wake us. */
    }
    assert(state[i]==EATING);
    Mutex_Unlock(&mx);    

    eat(i);			/* EAT */

    Mutex_Lock(&mx);
    state[i] = THINKING;	/* We are done eating, think again */
    print_state("     %d is thinking\n",i);
    trytoeat(LEFT(i));		/* Check if our left and right can eat NOW. */
    trytoeat(RIGHT(i));
    Mutex_Unlock(&mx);
  }
  Mutex_Lock(&mx);
  state[i] = NOTHERE;		/* We are done (eaten all the bites) */
  print_state("     %d is leaving\n",i);
  Mutex_Unlock(&mx);

  return i;
}

/*
  PROGRAM: Symposium
  This program executes a "symposium" for a number of philosophers, by spawning 
  a process per philosopher.
 */
int Symposium(int argl, void* args)
{
  int bites;			/* Bites (mpoykies) per philosopher */
  Pid_t pid;
  int i;

  assert(argl == sizeof(int[2]));
  N = ((int*)args)[0];		/* get the arguments */
  bites = ((int*)args)[1];

  /* Initialize structures */
  state = (PHIL*) malloc(sizeof(PHIL)*N);
  hungry = (CondVar*) malloc(sizeof(CondVar)*N);
  for(i=0;i<N;i++) {
    state[i] = NOTHERE;
    Cond_Init(&(hungry[i]));
  }
  
  /* Execute philosophers */
  for(i=0;i<N;i++) {
    int Arg[2];
    Arg[0] = i;
    Arg[1] = bites;
    pid = Exec(Philosopher, sizeof(Arg), Arg);
  }  

  /* Wait for philosophers to exit */  
  for(i=0;i<N;i++) {
    pid = WaitChild(NOPROC, NULL);
  }

  free(state);
  free(hungry);

  return 0;
}

/*
  PROGRAM: boot_task
  This is the initial task (which becomes the "init" process" with pid==1), 
  which starts all other tasks (except for the idle task). 
  It just executes the Symposium program, with the given parameters (number
  of philosophers and bites per philosopher).
 */

int boot_task(int argl, void* args)
{
  /* Just start task Symposium */
  Exec(Symposium, argl, args);

  while( WaitChild(NOPROC, NULL)!=NOPROC ); /* Wait for all children */

  return 0;
}

/**************************
  Main program
 **************************/

#define MAX_PHIL (MAX_PROC-3)

void usage(const char* pname)
{
  printf("usage:\n  %s <philosophers> <bites>\n\n  where <philosiphers> is from 1 to %d\n",
	 pname, MAX_PHIL);
  exit(1);
}


int main(int argc, const char** argv) 
{
  int nphil;			/* Number of philosophers */
  int bites;			/* Numer of bites per philosopher */
  int args[2];

  if(argc!=3) usage(argv[0]); 
  nphil = atoi(argv[1]);
  bites = atoi(argv[2]);

  /* check arguments */
  if( (nphil <= 0) || (nphil > MAX_PHIL) ) usage(argv[0]); 
  if( (bites <= 0) ) usage(argv[0]); 

  /* ajdust work per fibo call (to adapt to many philosophers/bites) */
  if(1) {
    FMIN = FBASE - (int)( log((double)(2*nphil*bites))/log(.5+.5*sqrt(5.)));
    if(FMIN<1) FMIN=1;
    FMAX = FMIN + FGAP;
    printf("FMIN = %d    FMAX = %d\n",FMIN,FMAX);
  }

  /* boot TinyOS */
  printf("*** Booting TinyOS\n");
  args[0] = nphil; args[1] = bites;
  boot(boot_task, sizeof(args), args);
  printf("*** TinyOS halted. Bye!\n");

  return 0;
}
