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

typedef struct message_s {
	Pid_t sender;
	Pid_t receiver;
	long data;
}message;

typedef struct PCB_s {
	Pid_t parent_pid;
	Pid_t pid;
	int exitvalue;
	ucontext_t context;
	message* msg;
	enum ProcState state;
}PCB;

typedef struct mlist_s {
	message* m;
	struct mlist_s* next;
}mlist;

mlist* headm = NULL;
mlist* tailm = NULL;

int max_Msgs = 200;
int max_Mbox = 100;
int Mbox_cnt = 0;

typedef struct MesArray_s {
	Message* Msg;
	struct MesArray_s *next;
}MesArray;

typedef struct wait_s {
	pid_t proc;
	struct wait_s *next;
}wait;

wait *whead,*wtail;

typedef struct Mbox_s {
	Pid_t owner;
	const char* name;
	int Msg_cnt;
	//int Msg_ins;
	//int Msg_p;
	MesArray* Msgshead;
	MesArray* Msgstail;
}Mbox;

typedef struct Mboxlist_s {
	Mbox* mb;
	struct Mboxlist_s* next;
}Mboxlist;

Mboxlist* headmbox = NULL;
Mboxlist* tailmbox = NULL;

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
CondVar *waiting,*waitingM;

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
	Read *i,*temp;
	int j=0;
	enum ProcState mon = ProcessTable[3].state;
	//pause scheduling
	pause_scheduling();
	do{
	if(ProcessTable[curproc->proc].state==READY && tail->proc!=curproc->proc)
	{
		i = (Read*)malloc(sizeof(Read));
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
	}while(ProcessTable[curproc->proc].state!=READY);
//	temp = head;
//	while(temp!=NULL)
//	{
//		printf("\n%d in row is %d %d and curproc is %d\n",j++,temp->proc,ProcessTable[temp->proc].state,curproc->proc);
//		temp = temp->next;
//	}
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

	if(ProcessTable[pid].state == READY)
		return;
	ProcessTable[pid].state = READY;
	temp = (Read*)malloc(sizeof(Read));
	temp->proc = pid;
	temp->next = NULL;
	if(head==NULL)
	{
		head = tail;
	}
//	else
//	{
		tail->next = temp;
		tail=tail->next;
//	}
	//curproc = temp;
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
	head = head->next;
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
	Read* temp = head;
	int i=0;
	Mutex_Lock(&kernel_lock);
	ProcessTable[curproc->proc].exitvalue=exitval;//8etoyme to exit code ths diergasias iso me to exitval(pernaei san orisma)
	ProcessTable[curproc->proc].state=FINISHED;//kanoyme to state ths diergasias FINISHED
//	while(temp!=NULL)
//	{
//		printf("\n%d in row is %d and curproc is %d\n",i++,temp->proc,curproc->proc);
//		temp = temp->next;
//	}
	Mutex_Unlock(&kernel_lock);
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
	temp = (Read*)malloc(sizeof(Read));
	temp->proc = PCBcnt;
	temp->next = NULL;
	if(head==NULL)
	{
		head = temp;
		tail = temp;
	}
//	else if(tail==NULL)
//	{
//		tail = temp;
//		head->next = tail;
//	}
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
				if(ProcessTable[cpid].parent_pid == ProcessTable[curproc->proc].pid){
					f=1;
					if(ProcessTable[cpid].state == FINISHED)
					{
						ProcessTable[cpid].state = DEAD;
						ProcessTable[cpid].parent_pid = 0;
						status = &ProcessTable[cpid].exitvalue;
						return ProcessTable[cpid].pid;
					}
				}
			if(f==0){
				return NOPROC;
			}
			else
				yield();
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
						status = &ProcessTable[i].exitvalue;
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
	int i=0;

	PCBcnt = 0;
	curproc=NULL;
	head = NULL;
	tail = head;
	headm =NULL;
	tailm = NULL;
	headmbox = NULL;
	tailmbox = NULL;
	whead = NULL;
	wtail = NULL;
	K = ((int*)args)[0];
	waiting = (CondVar*) malloc(sizeof(CondVar)*(K+K+K));
	waitingM = (CondVar*) malloc(sizeof(CondVar)*(K+K+K));
	for(i=0;i<3*K;i++) {
	    Cond_Init(&(waiting[i]));
	    Cond_Init(&(waitingM[i]));
	  }
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

	Mutex_Lock(&kernel_lock);
	if(receiver>PCBcnt||curproc->proc==receiver||ProcessTable[receiver].state==FINISHED || ProcessTable[receiver].state==DEAD)
		return -1;
	mes = (message*)malloc(sizeof(message));
	mes->receiver = receiver;
	mes->sender = curproc->proc;
	mes->data = data;
	node = (mlist*)malloc(sizeof(mlist));
	node->m = mes;
	node->next = NULL;
	ProcessTable[mes->receiver].msg = mes;
//	if(headm==NULL)
//	{
//		headm = node;
//		tailm = node;
//	}
//	else
//	{
//		tailm->next = node;
//		tailm = tailm->next;
//	}
	//Cond_Signal(&(waiting[mes->receiver]));
	if(ProcessTable[receiver].state==FINISHED || ProcessTable[receiver].state==DEAD)
	{
		Mutex_Unlock(&kernel_lock);
		return -1;
	}
	if(ProcessTable[mes->receiver].state == SLEEPING)
	{
		Cond_Signal(&(waiting[mes->receiver]));
	}
	Cond_Wait(&kernel_lock,&(waiting[curproc->proc]));
	Mutex_Unlock(&kernel_lock);
	return 0;
}

Pid_t ReceivePort(long* data, int waitflag)
{
	mlist *search,*search_prev=NULL;
	Pid_t sender;

	Mutex_Lock(&kernel_lock);
	while(1)
	{
//		if(headm!=NULL)
//		{
//			search = headm;
//			do
//			{
//				if(search->m->receiver == curproc->proc)
//				{
//					*data = search->m->data;
//					if(search==headm)
//						headm = headm->next;
//					else
//						search_prev->next = search->next;
//					printf("\n%d waking up rec  %d\n",curproc->proc,search->m->sender);
//					Cond_Signal(&(waiting[search->m->sender]));
//					Mutex_Unlock(&kernel_lock);
//					return search->m->sender;
//				}
//				search_prev = search;
//				search = search->next;
//			}while(search!=NULL);
//		}
		//Mutex_Unlock(&kernel_lock);
		if(ProcessTable[curproc->proc].msg != NULL)
		{
			Cond_Signal(&(waiting[(ProcessTable[curproc->proc].msg)->sender]));
			*data = (ProcessTable[curproc->proc].msg)->data;
			sender = (ProcessTable[curproc->proc].msg)->sender;
			ProcessTable[curproc->proc].msg = NULL;
			Mutex_Unlock(&kernel_lock);
			return sender;;
		}
		if(waitflag==0){
			Mutex_Unlock(&kernel_lock);
			return NOPROC;
		}
		else{
			Cond_Wait(&kernel_lock,&(waiting[curproc->proc]));
		}
	}
}

int CreateMailBox(const char* mbox)
{
	Mboxlist *node,*search;

	Mutex_Lock(&kernel_lock);
	if(mbox==NULL)
		return -1;
	if(Mbox_cnt == max_Mbox)
		return -1;
	search = headmbox;
	while(search!=NULL)
	{
		if(search->mb->name == mbox)
			return -1;
		search = search->next;
	}
	node = (Mboxlist*)malloc(sizeof(Mboxlist));
	node->mb = (Mbox*)malloc(sizeof(Mbox));
	node->mb->Msgshead = NULL;
	node->mb->Msgstail = NULL;
	node->mb->Msg_cnt = 0;
	//node->mb->Msg_p = 0;
	//node->mb->Msg_ins = 0;
	node->mb->name = mbox;
	node->mb->owner = curproc->proc;
	//node->mb->Msgs = (Message*)malloc(max_Msgs*sizeof(Message));
	if(headmbox==NULL)
	{
		headmbox = node;
		tailmbox = node;
	}
	else
	{
		tailmbox->next = node;
		tailmbox = tailmbox->next;
	}
	Mbox_cnt++;
	Mutex_Unlock(&kernel_lock);
	return 0;
}


int DestroyMailBox(const char* mbox)
{
  return 0;
}

int SendMail(const char* mbox, Message* msg)
{
	Mboxlist* search;
	Message* newm;
	MesArray* node;
	wait* temp;

	Mutex_Lock(&kernel_lock);
	search = headmbox;
	while(search!=NULL)
	{
		if(search->mb->name == mbox)
			break;
		else
			search = search->next;
	}
	if(search == NULL)
			return-1;
	if(search->mb->owner == curproc->proc)
		return -1;
	if(search->mb->Msg_cnt==max_Msgs)
	{
		temp = (wait*)malloc(sizeof(wait));
		temp->proc = curproc->proc;
		temp->next = NULL;
		if(whead == NULL)
		{
			whead = temp;
			wtail = whead;
		}
		else
		{
			wtail->next = temp;
			wtail = wtail->next;
		}
		Cond_Wait(&kernel_lock,&(waitingM[curproc->proc]));
	}
	msg->sender = curproc->proc;
	newm = (Message*)malloc(sizeof(Message));
	newm->len = msg->len;
	newm->sender = curproc->proc;
	newm->type = msg->type;
	if(msg->len!=0){
		newm->data = (void*)malloc(msg->len);
		memcpy(newm->data,msg->data,msg->len);
	}
	node = (MesArray*)malloc(sizeof(MesArray));
	node->Msg = newm;
	node->next = NULL;
	if(search->mb->Msgshead==NULL){
		search->mb->Msgshead = node;
		search->mb->Msgstail = node;
	}
	else
	{
		search->mb->Msgstail->next = node;
		search->mb->Msgstail = search->mb->Msgstail->next;
	}
	search->mb->Msg_cnt = search->mb->Msg_cnt+1;
	if(ProcessTable[search->mb->owner].state == SLEEPING)
	{
		Cond_Signal(&(waitingM[search->mb->owner]));
	}
	Mutex_Unlock(&kernel_lock);
	return 0;
}

int GetMail(const char* mbox, Message* msg)
{
	Mboxlist* search;
	wait* temp;

	Mutex_Lock(&kernel_lock);
	search = headmbox;
	while(search!=NULL)
	{
		if(search->mb->name == mbox)
		{
			break;
		}
		search = search->next;
	}
	if(search->mb->Msg_cnt == 0){
		Cond_Wait(&kernel_lock,&(waitingM[curproc->proc]));
	}
	if(search == NULL)
		return-1;
	if(search->mb->owner != curproc->proc)
		return -1;
	//msg = malloc(sizeof(Message));
	msg->data = search->mb->Msgshead->Msg->data;
	msg->len = search->mb->Msgshead->Msg->len;
	//msg->data = (void*)malloc(msg->len);
	//memcpy(msg->data,search->mb->Msgs[search->mb->Msg_p].data,msg->len);
	msg->sender = search->mb->Msgshead->Msg->sender;
	msg->type = search->mb->Msgshead->Msg->type;
	//printf("\ninside Process %d: %s %d inside \n",msg->sender,msg->data,msg->len);//search->mb->Msgs[search->mb->Msg_cnt].data);
	search->mb->Msgshead = search->mb->Msgshead->next;
	if(search->mb->Msg_cnt != 0)
		search->mb->Msg_cnt = search->mb->Msg_cnt - 1;
	if(whead!=NULL){
		temp = whead;
		whead = whead->next;
		Cond_Signal(&(waitingM[temp->proc]));
	}
	Mutex_Unlock(&kernel_lock);
	return 0;
}

