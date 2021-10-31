# Assignment 4 - Enhancing xv6 riscv OS 
#### Sneha Raghava Raju 
#### Roll No: 2020101125

## Overview

Implemented the strace function call., added FCFS and PBS scheduler, and modified the procdump function.

## Run the shell
* Run the following command 
``make qemu``

* Add the SCHEDULER flag to choose between RR, FCFS, and PBS:
``make qemu SCHEDULER=RR``

* **NOTE**:
run 'make clean' when the scheduler is to be changed:
i.e if you first run:
``make qemu SCHEDULER=RR``
then want to change the scheduler to FCFS, run:
``make clean``
and then:
``make qemu SCHEDULER=FCFS``

# Spec 1
## strace systemcall
The following files have been modified:
* proc.c
* proc.h
* syscall.h
* syscall.c
* sysproc.c
* added new file user/strace.c
* user.h
* usys.pl


In `proc.c` added:

`int tracemask; `
to store the integer mask passed by the user.


In `syscall.c` added a array of system calls and modified the syscall function to print the arguments:

```
void
syscall(void)
{
  int num, arg;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  arg = p->trapframe->a0;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) 
  {
    p->trapframe->a0 = syscalls[num]();

    //** modifying syscall to print the strace **//
    if ((p->tracemask >> num) & 1) 
    {
      printf("%d: syscall %s (%d", p->pid, syscall_names[num], arg);

      for(int i = 1; i < syscall_arg_count[num]; i++)
      {
        int n;
        argint(i, &n);
        printf(" %d", n);
      }
	    printf(") -> %d\n", p->trapframe->a0);
    }
  } 
  else 
  {
    printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

Adding the system call to `sysproc.c`:

```
uint64
sys_strace(void)
{
  int n;

  if(argint(0, &n) < 0)       //setting mask arg given by user to n
  {
    return -1;
  }
  struct proc *p = myproc();
  p->tracemask = n;    //setting tracemask of the process to n 
  
  return 0;
}
```
And wrote a user program `strace.c` to call the trace.

### Explaination

So the system call 'strace' was defined and written. It was included in the user files so that it could be called. 

`strace <mask> <command>` , prints all the systems calls and their arguments, specified by mask.



# Spec 2 
## Part 1: FCFS scheduler

FCFS scheduler executes processes in order of their arrival. Process with lower creation times are executed first.

Stored ctime (creation time) of a process, by adding this to allocproc() in `proc.c`:
`p->ctime = ticks; `

And in the scheduler() function we check which process has the lowest p->ctime and switch to that process.

Also disabled 'yield()' in `trap.c`, to make the scheduler non-preemptive.

## Part 2: PBS scheduler

PBS scheduler executes processes based on their priority. Process can have priority from 0-100 (0 being high priority and 100 being low).

Stored `int priority` , `int stime`, `int dynamic_priority`, `int niceness` , `int num_of_runs` in struct proc in `proc.h`. (priority is static priority, stime is sleeping time, num_of_runs in the number of times the process was picked by the scheduler).

p->priority is initially set to 60 in allocproc().

p->stime is calculated by adding the following to the update_time() function :

```
void
update_time()
{
  struct proc* p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNING) {
      p->rtime++;
    }
    if(p->state == SLEEPING)
    {
      p->stime++;
    }
    release(&p->lock); 
  }
}
```

p->niceness is initially set to 5 in allocproc().

Then in the scheduler() in `proc.c` niceness, dynamic_priority and num_of_runs are calculated:
if p->RUNNABLE then the following are calculated:

```
//niceness = Int( ticks in sleeping state/ticks in running+sleeping state)*10
        int niceness = ((p->stime)/((p->rtime) + (p->stime)))*10;

        p->niceness = niceness;

        if((p->rtime + p->stime)==0 || p->stime == 0)
        {
          p->niceness = 0;
        }

        // if((p->rtime + p->stime) == p->stime)
        // {
        //   p->niceness = 10;
        // }
        
        // DP = max(0, min(SP − niceness + 5, 100))
        int dp = p->priority - p->niceness + 5;

        if (dp > 100)
        {
          dp = 100;
        }

        if (dp < 0)
        {
          dp = 0;
        }

        p->dynamic_priority = dp;
```

Then the process with the lowest value of dynamic_priority is found and if it is found we update num_of_runs:

`low_priority->num_of_runs += 1`

and we switch to that process.

Also disabled 'yield()' in `trap.c`, to make the scheduler non-preemptive.

## Part 3: MLFQ scheduler

**Q:** If a process voluntarily relinquishes control of the CPU(eg. For doing I/O), it leaves the queuing network, and when the process becomes ready again after the I/O, it is  inserted at the tail of the same queue, from which it is relinquished earlier. Explain how could this be exploited by a process?

**Ans:** A process could take advantage of this policy by giving up CPU just before time slice is over, so that it is not demoted to a lower queue, and gets a fresh time slice.

# Spec 3
## modify procdump 

The procdump() function in `proc.c` was modified in order to print diffrent values:
```
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  #ifdef PBS
  printf("PID\tPriority\tState\trtime\twtime\tnrun\n");
  #endif
  for(p = proc; p < &proc[NPROC]; p++)
  {
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    //printf("%d\t%s\t%s\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d", p->pid, state, p->name, p->ctime, p->rtime, p->stime, p->priority, p->niceness, p->dynamic_priority, p->num_of_runs);    
    
    #ifdef PBS
      printf("%d\t%d\t\t%s\t%d\t%d\t%d", p->pid, p->dynamic_priority, state, p->rtime, (ticks - p->rtime), p->num_of_runs);    
    #else
    #ifndef PBS
     printf("%d %s %s", p->pid, state, p->name);
    #endif
    #endif

    printf("\n");
  }
}
```

# Performance Comparison
**Round Robin:** 
Run 1 : Average rtime 134,  wtime 8
Run 2 : Average rtime 140,  wtime 8

**FCFS**: 
Run 1 : Average rtime 103,  wtime 8
Run 2 : Average rtime 147,  wtime 21

**PBS**: 
Run 1 : Average rtime 120,  wtime 9
Run 2 : Average rtime 122,  wtime 20

So we see that round robin seems to be better in this case (since the schedulertest program has I/O bound processes), it has more runnning time and less waiting time.
