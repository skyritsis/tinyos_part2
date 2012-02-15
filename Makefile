
CC=gcc 
CFLAGS= -Wall -g -O2

.PHONY: all

all: mtask ipcproc

mtask: mtask.o kernel.o
	$(CC) $(CFLAGS) -o $@ mtask.o kernel.o -lm

ipcproc: ipcproc.o kernel.o
	$(CC) $(CFLAGS) -o $@ ipcproc.o kernel.o -lm


mtask.c: syscalls.h ipccalls.h
kernel.c: syscalls.h ipccalls.h



