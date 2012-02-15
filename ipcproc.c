
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>

#include "ipccalls.h"


/*****************************************************/

/*
 *
 *  Tasks.
 *
 */


#define FBASE  35
#define FGAP   10

int FMIN, FMAX;

unsigned int fiborand() { return lrand48() % (FMAX-FMIN+1) + FMIN; }

unsigned int fibo(unsigned int n)
{
  static long x=0;
  x++;
  if(n<2) 
    return n;
  else
    return fibo(n-1)+fibo(n-2);
}


/* 
 *  This is a print server
 */

#define PRINTER_PRINT 1
#define PRINTER_QUIT 2


/*
  PPROGRAM: printer_task
  
  This task creates a mailbox and receives strings, which then prints to the terminal.
  Conceptually, this is a "service", started by boot_task and ending when boot_task
  asks it to quit.
 */
int printer_task(int argl, void* args)
{
  int ok;
  Message m;

  ok = CreateMailBox("printer");
  assert(ok==0);
  
  // synchronize with my parent, he waited for me to create my mailbox
  SendPort(GetPPid(), 0L);
  
  // print loop
  while(1) {
    GetMail("printer", &m);
    if(m.type == PRINTER_PRINT)
      printf("Process %d: %s",m.sender,(char*)(m.data));
    else if(m.type == PRINTER_QUIT) {
      /* only if parent says so! */
      if(m.sender==GetPPid()) break;
    }
    /* else ignore */
  }
  return 0;
}

void print_string(char* s)
{
  Message m;
  int ok;

  m.type = PRINTER_PRINT;
  m.data = s;
  m.len = strlen(s)+1;
  ok = SendMail("printer", &m);
  assert(ok==0);
}


/*
  PROGRAM: fibo_task

  This program will repeatedly receive synchronously a positive value k and compute 
  the k-th fibonacci number. Each computed fibonacci number is sent to the "printer"
  message queue. It exits if it receives k==-1
*/
int fibo_task(int argl, void* args)
{
  unsigned int k,f;
  long msg;
  char *prbuf = malloc(1024);
  Pid_t pid;

  sprintf(prbuf,"Hello from fibo_task process %d.\n",GetPid());
  print_string(prbuf);

  while(1) {
    // Wait for a request to compute a Fibonacci number.
    pid = ReceivePort(&msg, 1);
    assert(pid == GetPPid());
    if(msg<0) {
      sprintf(prbuf, "fibo_task exiting (pid = %d). Bye!\n", GetPid());
      print_string(prbuf);
      free(prbuf);
      Exit(0);			/* Test Exit! */
    }

    k = msg;
    sprintf(prbuf,"I will compute the %d-th Fibonacci number\n",k);
    print_string(prbuf);
    f = fibo(k);
    sprintf(prbuf,"The %d-th Fibonacci number is %d.\n",k,f);
    print_string(prbuf);
  }
  return 0;
}


/*
  PROGRAM: fibodriver_task

  This program creates a fibo_task child and then sends to it a number of values to
  process.
*/
int fibodriver_task(int argl, void* args)
{
  int work;
  Pid_t child;

  assert(argl == sizeof(int));
  work = *((int*)args) ;

  child = Exec(fibo_task, 0, NULL);
  while(work>0) {
    SendPort(child, fiborand());
    work --;
  }
  SendPort(child, -1);
  
  WaitChild(child,NULL);
  return 0;
}


/*
 * This is the initial task, which starts all the other tasks (except for the idle task). 
 */
int boot_task(int argl, void* args)
{
  int i,ntasks,ncalls;
  int status;
  long msg;
  Message m;
  Pid_t pid;
  Pid_t printer;
  Pid_t* pids;
  
  ntasks = ((int*)args)[0];
  ncalls = ((int*)args)[1];

  pids = malloc(sizeof(Pid_t)*ntasks);
  
  /* Launch child processes */
  printer = Exec(printer_task,0,NULL);
  ReceivePort(&msg,1);		/* Wait until it is ready to print */

  for(i=0; i<ntasks; i++) {
    pids[i] = pid = Exec(fibodriver_task,sizeof(ncalls),&ncalls);
    printf("boot_task: executed fibodriver %d   pid=%d\n",i+1,pid);
  }

  /* Wait for child processes */
  for(i=0;i<ntasks;i++) {
    Pid_t pid = WaitChild(NOPROC, &status);
    printf("boot_task: Process %d exited with exit value %d.\n",pid,status);
  }
 
  /* Tell printer to exit */
  m.type = PRINTER_QUIT;
  m.data = NULL;
  m.len = 0;
  SendMail("printer",&m);
  
  /* Loop until all children have exited  */
  while(WaitChild(NOPROC,NULL) != NOPROC)
    printf("boot_task: child exited.\n");

  printf("boot_task: exiting!\n");

  return 0;
}


/****************************************************/
#define MAXTASKS  ((MAX_PROC-4)/2)
#define MAXCALLS  50

void usage(const char* pname)
{
  printf("usage:\n  %s <number of tasks> <calls per task>\n\n"
	 "where:\n"
	 "     <number of tasks> is from 1 to %d\n"
	 "     <calls per task> is from 1 to %d\n",
	 pname, MAXTASKS, MAXCALLS);
  exit(1);
}

int main(int argc, const char** argv) 
{
  int ntasks;
  int ncalls;
  int args[2];

  if(argc!=3) usage(argv[0]); 

  ntasks = atoi(argv[1]);
  ncalls = atoi(argv[2]);
  if( (ntasks < 1) || (ntasks > MAXTASKS) ||
      (ncalls < 1) || (ncalls > MAXCALLS) ) usage(argv[0]); 
  
  // adjust for many processes
  FMIN = FBASE - (int)( log((double)(ntasks*ncalls))/log(.5+.5*sqrt(5.)));
  if(FMIN < 1) FMIN = 1;
  FMAX = FMIN+FGAP;
  
  /* boot */
  printf("*** Booting TinyOS\n");
  args[0] = ntasks; args[1] = ncalls;
  boot(boot_task, sizeof(args), args);
  printf("*** TinyOS halted. Bye!\n");
  
  return 0;
}
