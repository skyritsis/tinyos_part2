
#ifndef __IPCCALLS_H__
#define __IPCCALLS_H__

#include "syscalls.h"

/* 
   This function returns the parent pid of the current process.
*/
Pid_t GetPPid(void);


/*********************************************
   Synchronous process communication: ports 
**********************************************/

/*
  This function transfers the long value of the second argument to the
  process whose pid is given in the first argument. The calling
  process will sleep until the receiving process has received the
  data by calling ReceivePort.

  The return value is 0 on sucess and -1 on error. Possible errors
  are:
  - the receiver is not a valid process (returns immediately)
  - the receiver is the calling process (returns immediately)
  - the receiver exited without calling ReceivePort
*/
int SendPort(Pid_t receiver, long data);

/*
  This function receives a value sent to the current process' port and
  stores it in the variable pointed to by the first argument. The
  return value of the function is the PID of the sender of the data.

  If there is no value sent to this process' port at the time the
  function is called, the behavior depends on the value of the second
  argument, waitflag. 
  -- If waitflag is false, the call returns immediately the value
  NOPROC without modifying the location pointed to by the first
  argument. 
  -- If waitflag is true, the process is suspended until a value is
  sent to the process port by another process calling SendPort.
 */
Pid_t ReceivePort(long* data, int waitflag);


/*************************************************
   Asynchronous process communication: mailboxes 
**************************************************/

/*
  The structure below defines the messages sent to mailboxes.
  The sender must fill in the attributes: type, data, and len.
  If len is greater than 0, data must point to a memory area of length
  len. If len is zero, the value of data is ignored.

  The receiver receives a structure with the folllowing attributes:
  - attribute sender contains the PID of the sending process.
  - attribute type contains a copy of the value sent by the sender.
  - attribute len contains a copy of the value sent by the sender.
  - if len>0, attribute data points to a memory area of length len,
    containing ***** A COPY OF THE DATA SENT BY THE SENDER ******
    if len==0, attribute data is NULL.
 */
typedef struct mailmessage_s {
  Pid_t sender;
  int type;
  void* data;
  size_t len;
} Message;

/*
  This function creates a mailbox with the name given by the first
  parameter, which must be a non-NULL string. The owner of
  the new mailbox is the calling process. Only the owner can read mail
  from this mailbox, and destroy it.

  The function returns 0 on success, and -1 on failure. Reasons of
  failure are:
  - The given value of mbox is the name used by another mailbox.
  - The total number of mailboxes in the system is exceeded.
 */
int CreateMailBox(const char* mbox);

/*
  This function destroys a mailbox  with the name given by the first
  parameter, which must be a non-NULL string. The owner of
  the mailbox must be the calling process. Any messages contained in
  the mailbox are discarded.

  The function returns 0 on success and -1 on failure. Reasons of
  failure are:
  - The given value of mbox is not the name of a mailbox.
  - The caller is not the owner of the mailbox.

  When a process exits, all its mailboxes are destroyed aytomatically.
 */
int DestroyMailBox(const char* mbox);

/*
  This function sends the message described by the second argument to
  the mailbox specified by the first argument. If the mailbox is full,
  the function blocks until space is available in the mailbox, or the
  mailbox is destroyed.

  The function returns 0 on success and -1 on failure. Reasons of
  failure are:
  - The given value of mbox is not the name of a mailbox.
  - The calling process is the owner of the mailbox (i.e. mail to yourself
  is not allowed).
 */
int SendMail(const char* mbox, Message* msg);

/*
  This function receives a message in the structure given in second
  argument, from the mailbox specified by the first argument. If the
  mailbox is empty, the function blocks until a message arrives.

  The function returns 0 on success and -1 on failure. Reasons of
  failure are:
  - The given value of mbox is not the name of a mailbox.
  - The calling process is not the owner of the mailbox (i.e. cannot
  read others' mail).
 */
int GetMail(const char* mbox, Message* msg);

#endif
