#ifndef __SYSCALLS_H__
#define __SYSCALLS_H__

/*
 *
 * Public kernel declarations
 *
 */

/*******************************************
 * Processes types and constants.
 *******************************************/

typedef int Pid_t;		/* The PID type  */

/* The invalid PID */
#define NOPROC (-1)

/* The maximum number of processes */
#define MAX_PROC 65536

/*******************************************
 *      Concurrency control
 *******************************************/

/* Mutexes */
typedef volatile unsigned int Mutex;

/* This macro is used to initialize mutexes as follows:

   Mutex mu_mytex = MUTEX_INIT;
 */
#define MUTEX_INIT 1

/* Try to lock a mutex, return 1 on success, 0 on failure (when the
   mutex is taken).  This function does not block.  */
int Mutex_TryLock(Mutex*);

/* Lock a mutex by waiting if necessary. */
void Mutex_Lock(Mutex*);

/* Unlock a mutex that you locked. */
void Mutex_Unlock(Mutex*);

/* Helper struct for CondVar. Defines a linked-list node. */
typedef struct tinyos_cv_waiter_s {
  Pid_t pid;
  struct tinyos_cv_waiter_s *next;
} tinyos_cv_waiter;

/* Condition variables */
typedef struct {
  tinyos_cv_waiter *waitset;   /* Waitset, head of linked list */
  tinyos_cv_waiter **wstail;   /* Waitset, pointer to tail. */
} CondVar;

/* This macro is used to initialize a condition variable as follows:

   CondVar my_cv = CONDVAR_INIT;
 */
#define CONDVAR_INIT { NULL, NULL }

/* Initialize a CondVar when CONDVAR_INIT cannot 
   be used (e.g. an array of CondVars) */
void Cond_Init(CondVar*);

/* Wait on a condition variable. This must be called only while we
  have locked the mutex that is associated with this call.  It will
  put our process to sleep, unlocking the mutex. These operations
  happen atomically.  When our process is woken up later (by another
  process that called Cond_Signal or Cond_Broadcast), this function
  first re-locks the mutex and then returns.  */
void Cond_Wait(Mutex*, CondVar*);

/* Signal wakes up exactly one process sleeping on this condition
   variable (if any). */
void Cond_Signal(CondVar*);

/* Broadcast wakes up all processes sleeping on this condition variable
   (if any). */
void Cond_Broadcast(CondVar*); 

/*******************************************
 * Process creation
 *******************************************/

/* New processes are created by calling a starting function, whose
   signature is Task. */
typedef int (*Task)(int, void*);

/* Create a new process by calling the function with signature Task
   and pass it parameters argl and args, where argl is the length of
   byte array args (args can be NULL). */
Pid_t Exec(Task, int argl, void* args);

/* When this function is called by a process, the process terminates
   and sets its exit code to val. */
void Exit(int val);

/* This function will return the exit status of a child process,
   waiting if necessary for a child process to end. When parameter
   pid holds the value of a specific child process of this process,
   WaitChild will wait for this specific process to finish. If
   parameter pid is equal to NOPROC, then WaitChild will wait for
   *any* child process to exit. 

   If parameter exitval is a not null, the exit code of the child
   process will be stored in the variable pointed to by status.

   On success, WaitChild returns the pid of an exited child.
   On error, WaitChild returns NOPROC. Possible errors are:
   - the specified pid is not a valid pid.
   - the specified process is not a child of this process.
   - the process has no child processes to wait on (when pid=NOPROC).
*/
Pid_t WaitChild(Pid_t pid, int* exitval);

/* This function returns the pid of the current process */
Pid_t GetPid(void);

/* This function is called with the same parameters as exec, to boot
   the system. The system must initialize the scheduler and then call
   the function boot_task with parameters argl and args. The boot task
   can then create more processes.

   When the boot task finishes, and no other processes are running,
   this call halts the scheduler, cleans up TinyOS structures and then
   returns. */
void boot(Task boot_task, int argl, void* args);

#endif
