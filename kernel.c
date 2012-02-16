#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "syscalls.h"
#include "ipccalls.h"


/*
 *
 * Kernel variables
 *
 */
enum ProcState { READY, SLEEPING, FINISHED, DEAD };

typedef struct PCB_s {
	Pid_t parent_pid;
	Pid_t pid;
	int exitvalue;
	ucontext_t context;

	enum ProcState state;
}PCB;

typedef struct message_s {
	Pid_t sender;
	Pid_t receiver;
	long data;
}message;

typedef struct mlist_s {
	message* m;
	struct mlist_s* next;
}mlist;

mlist* headm = NULL;
mlist* tailm = NULL;

typedef struct rlist {
	Pid_t proc;
	struct rlist* next;
}Read;

Read *head,*tail,*curproc;
PCB ProcessTable[MAX_PROC];
Pid_t PCBcnt;
Mutex kernel_lock = MUTEX_INIT;
ucontext_t kernel_context;
int K=0;
CondVar *waiting;

/*
 *
 * Concurrency control
 *
 */

#define QUANTUM (5000L)

struct itimerval quantum_itimer;

/*quantum_itimer.it_interval.tv_sec = 0L;
quantum_itimer.it_interval.tv_usec = QUANTUM;
quantum_itimer.it_value.tv_sec = 0L;
quantum_itimer..it_value.tv_usec = QUANTUM;
*/


void reset_timer()
{
	setitimer(ITIMER_VIRTUAL, &quantum_itimer, NULL);
}

sigset_t scheduler_sigmask;
/*sigemptyset(&scheduler_sigmask);
sigaddset(&scheduler_sigmask, SIGVTALRM);*/

void pause_scheduling() {sigprocmask(SIG_BLOCK,&scheduler_sigmask, NULL);}
void resume_scheduling() {sigprocmask(SIG_UNBLOCK, &scheduler_sigmask, NULL);}

void schedule(int sig){
	PCB *old, *new;
	Read* i;
	//pause scheduling
	pause_scheduling();
	if(ProcessTable[curproc->proc].state==READY)
	{
		i = malloc(sizeof(Read*));
		i->proc = curproc->proc;
		tail->next = i;
		tail = tail->next;
		tail->next = NULL;
	}
	old = &ProcessTable[curproc->proc];
	//if(old->state==FINISHED);
		//final_cleanup(old);

	reset_timer();
	//resume scheduling
	//curproc->proc = new->pid;
	if(head==NULL)
		head=tail;
	new = &ProcessTable[head->proc];
	curproc = head;
	head = head->next;
	resume_scheduling();
	if(old!=new)
	{
		//printf("\nold = %d, new = %d, cur = %d",old->pid,new->pid,curproc->proc);
		swapcontext(&old->context,&new->context);}
	else
		getcontext(&old->context);
}

void yield() {schedule(0);}

void wakeup(Pid_t pid){
	Read *temp;

	ProcessTable[pid].state = READY;
	temp = malloc(sizeof(Read*));
	temp->proc = pid;
	temp->next = NULL;
	if(head==NULL)
	{
		head=temp;
		head->next = tail;
	}
	else
	{
		tail->next = temp;
		tail=tail->next;
	}
	//yield();
	//curproc->next=NULL;
}

void release_and_sleep(Mutex* cv){
	ProcessTable[curproc->proc].state = SLEEPING;
	//printf("%d process is sleeping",curproc->proc);
	Mutex_Unlock(cv);
	yield();
}


void run_scheduler()
{
	struct sigaction sa;
	int err;
	sa.sa_handler = schedule;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&(sa.sa_mask));

	while((err = sigaction(SIGVTALRM, &sa, NULL)) && (errno==EINTR));
	assert(err==0);

	reset_timer();

	curproc = head;
	swapcontext(&kernel_context,&(ProcessTable[curproc->proc].context));
}

int Mutex_TryLock(Mutex *lock)
{
  char oldval;
  __asm__ __volatile__("xchgb %b0,%1"
		       :"=q" (oldval), "=m" (*lock)
		       :"0" (0) : "memory");
  return oldval > 0;
}

void Mutex_Unlock(Mutex* lock)
{
  *lock = 1;
}

void Mutex_Lock(Mutex* lock)
{
	while(!Mutex_TryLock(lock))
		yield();
}

void Cond_Init(CondVar* cv)
{
  cv->waitset = NULL;
  cv->wstail = NULL;
}

static Mutex condvar_mutex = MUTEX_INIT;

void Cond_Wait(Mutex* mutex, CondVar* cv)
{

	tinyos_cv_waiter waitnode;
	waitnode.pid = GetPid();
	waitnode.next = NULL;

	Mutex_Lock(&condvar_mutex);

	if(cv->waitset==NULL)
		cv->wstail = &(cv->waitset);
	*(cv->wstail) = &waitnode;
	cv->wstail = &(waitnode.next);

	Mutex_Unlock(mutex);
	release_and_sleep(&condvar_mutex);

	Mutex_Lock(mutex);
}

static void doSignal(CondVar* cv)
{
	if(cv->waitset != NULL){
		tinyos_cv_waiter *node = cv->waitset;
		cv->waitset = node->next;
		wakeup(node->pid);
	}
}

void Cond_Signal(CondVar* cv)
{
	Mutex_Lock(&condvar_mutex);
	doSignal(cv);
	Mutex_Unlock(&condvar_mutex);
}

void Cond_Broadcast(CondVar* cv)
{
	Mutex_Lock(&condvar_mutex);
	while(cv->waitset != NULL)
		doSignal(cv);
	Mutex_Unlock(&condvar_mutex);
}

void runFunc(Task func,int argl,void* args)
{
	int x;
	x=func(argl,args);//trexoyme thn synarthsh poy pernaei san orisma(func)me ta orismata ths(args)
	//yield();
	Exit(x);//molis teleiwsei h parapanw synarthsh, termatizoyme thn diergasia
}

void init_context(ucontext_t* uc, void* stack, size_t stack_size, Task call, int argl, void* args)
{
	void* arg;
	getcontext(uc);
	uc->uc_link = NULL;
	uc->uc_stack.ss_sp = stack;
	uc->uc_stack.ss_size = stack_size;
	uc->uc_stack.ss_flags = 0;
	arg = malloc(argl);
	memcpy(arg,args,argl);
	makecontext(uc, runFunc, 3, call, argl, arg);
}

#define PROCESS_STACK_SIZE 65536


/*
 *
 * System calls
 *
 */


void Exit(int exitval)
{
	ProcessTable[curproc->proc].exitvalue=exitval;//8etoyme to exit code ths diergasias iso me to exitval(pernaei san orisma)
	ProcessTable[curproc->proc].state=FINISHED;//kanoyme to state ths diergasias FINISHED
	if(curproc->proc == 1)
		swapcontext(&(ProcessTable[curproc->proc].context),&kernel_context);
	yield();//trexoyme thn epomenh diergasia(h diergasia poy molis teleiwse bgainei apo th lista toy scheduler
}

Pid_t Exec(Task call, int argl, void* args)
{
	//ucontext_t unew;
	Read *temp;
	void* stack = malloc(PROCESS_STACK_SIZE);
	Mutex_Lock(&kernel_lock);
	PCBcnt = PCBcnt+1;
	init_context(&ProcessTable[PCBcnt].context, stack, PROCESS_STACK_SIZE, call, argl, args);
	//ProcessTable[PCBcnt].context = unew;
	ProcessTable[PCBcnt].parent_pid = (curproc==NULL) ? 0 : curproc->proc;
	ProcessTable[PCBcnt].pid = PCBcnt;
	ProcessTable[PCBcnt].state = READY;
	temp = malloc(sizeof(Read*));
	temp->proc = PCBcnt;
	temp->next = NULL;
	if(head==NULL)
	{
		head = temp;
	}
	else if(tail==NULL)
	{
		tail = temp;
		head->next = tail;
	}
	else
	{
		tail->next = temp;
		tail = tail->next;
	}
	Mutex_Unlock(&kernel_lock);
	return ProcessTable[PCBcnt].pid;//curproc->pid;
}

Pid_t GetPid()
{
  return curproc->proc;
}


Pid_t WaitChild(Pid_t cpid, int* status)
{
	int i,f;
	while(1){
		f=0;
		if(cpid!=NOPROC)
		{
			for(i=1;i<=PCBcnt;i++)
			{
				if(ProcessTable[i].parent_pid == ProcessTable[curproc->proc].pid && ProcessTable[i].parent_pid ==cpid){
					f=1;
					if(ProcessTable[i].state == FINISHED)
					{
						ProcessTable[i].state = DEAD;
						ProcessTable[i].parent_pid = 0;
						return ProcessTable[i].pid;
					}
				}
			}
			if(f==0){
				return NOPROC;
			}
		}
		else
		{
			for(i=1;i<=PCBcnt;i++)
			{
				if(ProcessTable[i].parent_pid == ProcessTable[curproc->proc].pid){
					f=1;
					if(ProcessTable[i].state == FINISHED){

						ProcessTable[i].state = DEAD;
						ProcessTable[i].parent_pid = 0;
						return ProcessTable[i].pid;
					}
				}
			}
			if(f==0){
				return NOPROC;
			}
			yield();
		}
	}
}

/*
 *
 * Initialization
 *
 */

void boot(Task boot_task, int argl, void* args)
{
	PCBcnt = 0;
	curproc=NULL;
	head = NULL;
	tail = head;
	K = ((int*)args)[0];
	waiting = (CondVar*) malloc(sizeof(CondVar)*K);
	sigemptyset(&scheduler_sigmask);
	sigaddset(&scheduler_sigmask, SIGVTALRM);
	quantum_itimer.it_interval.tv_sec = 0L;
	quantum_itimer.it_interval.tv_usec = QUANTUM;
	quantum_itimer.it_value.tv_sec = 0L;
	quantum_itimer.it_value.tv_usec = QUANTUM;
	Exec(boot_task,argl,args);
	run_scheduler();
}

/*
 *
 * IPC
 *
 */

Pid_t GetPPid() 
{ 
  return ProcessTable[curproc->proc].parent_pid;
}

int SendPort(Pid_t receiver, long data)
{
	mlist* node;
	message* mes;
	if(receiver>PCBcnt||curproc->proc==receiver||ProcessTable[receiver].state==FINISHED || ProcessTable[receiver].state==DEAD)
		return -1;
	mes = malloc(sizeof(message*));
	mes->receiver = receiver;
	mes->sender = curproc->proc;
	mes->data = data;
	node = malloc(sizeof(mlist*));
	node->m = mes;
	node->next = NULL;
	if(headm==NULL)
	{
		headm = node;
		tailm = node;
	}
	else
	{
		tailm->next = node;
		tailm = tailm->next;
	}
	Cond_Wait(&kernel_lock,&(waiting[curproc->proc]));
	if(ProcessTable[receiver].state==FINISHED || ProcessTable[receiver].state==DEAD)
		return -1;
	return 0;
}

Pid_t ReceivePort(long* data, int waitflag)
{
	mlist *search,*search_prev=NULL;;
	while(1)
	{
		if(headm!=NULL)
		{
			search = headm;
			do
			{
				if(search->m->receiver == curproc->proc)
				{
					data = &search->m->data;
					Cond_Signal(&(waiting[search->m->sender]));
					if(search==headm)
						head = head->next;
					else
						search_prev->next = search->next;
					return search->m->sender;
				}
				search_prev = search;
				search = search->next;
			}while(search!=NULL);
		}
		if(waitflag==0)
			return NOPROC;
		else
			yield();
	}
}

int CreateMailBox(const char* mbox)
{
  return 0;
}


int DestroyMailBox(const char* mbox)
{
  return 0;
}

int SendMail(const char* mbox, Message* msg)
{
  return 0;
}

int GetMail(const char* mbox, Message* msg)
{
  return 0;
}

