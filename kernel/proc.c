#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->ctime = ticks;       // setting create time of the process (Q2)
  p->rtime = 0;           // initializing run time of the process to 0 (Q2)
  p->etime = 0;           // initializing end time of the process to 0 (Q2)

  p->priority = 60;       // set the default priority  (Q2 - PBS)
  p->niceness = 5;

  p->stime = 0;
  p->num_of_runs = 0;

  // #ifdef MLFQ
  // p->num_of_runs = 0;
  // p->sched_time = 0;
  // p->queue = 0;

  // for (int i = 0; i < 5; i++)
  //   p->ticks[i] = 0;
  // #endif
  
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->tracemask = 0;               // Trace Mask to store the mask passed by the user
  p->ctime = 0;                   // Create time of the process 
  p->rtime = 0;                   // Run time of the process 
  p->etime = 0;                   // End time of the process 
  p->priority = 0;                // Process priority for PBS
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  //** copying tracemask **//
  np->tracemask = p->tracemask;

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  p->etime = ticks;         //setting end time of the process (Q2)

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
waitx(uint64 addr, uint* rtime, uint* wtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;

          *rtime = np->pid;                               // running time of the process (Q2)
          *wtime = np->etime - np->ctime - np->rtime;     //wait time of the process (Q2)

          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

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

int setpriority(int new_priority, int pid)
{
  int old_priority = -1;

  // //acquire process table lock
  //acquire(&ptable.lock);

  //scan through process table
  struct proc* p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    //int old_priority;
    acquire(&p->lock);
    if (p->pid == pid)
    {
      //store old priority and change the priority
      old_priority = p->priority;
      p->priority = new_priority;
      p->niceness = 5;
      //printf("p: %d\n", p->niceness);
    }
    release(&p->lock);
  }
  // //release process table lock
  //release(&ptable.lock);

  //output old priority
  return old_priority;
  //return pid;
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  struct cpu *c = mycpu();
  //struct proc *minP;
  c->proc = 0;

  #ifdef FCFS
  int sched_condition = 0;
  #endif

  #ifdef PBS
  int priority_condition = 0;
  #endif

  // #ifdef MLFQ
  // p = 0;
  // #endif

  for(;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    #ifdef RR

    //printf("RR\n");

    for(p = proc; p < &proc[NPROC]; p++) 
    {
      acquire(&p->lock);
      if(p->state == RUNNABLE) 
      {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        //printf("pname %s, pid %d, rtime %d ctime %d\n", p->name, p->pid, p->rtime, p->ctime);
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
    #else
    #ifdef FCFS

    //printf("FCFS\n");

    struct proc *scheduled_process = 0;

    for(p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);

      if(p->state == RUNNABLE)
      {
        if(scheduled_process == 0)
        {
          scheduled_process = p;
        }
        else
        {
          sched_condition = p->ctime < scheduled_process->ctime;

          if(sched_condition)
          {
            release(&scheduled_process->lock);
            scheduled_process = p;
          }
          else
          {
            release(&p->lock);
          }
        }
      }
      else
      {
        release(&p->lock);
      }
    }
      
    if(scheduled_process != 0)
    {
      scheduled_process->state = RUNNING;
      c->proc = scheduled_process;
      swtch(&c->context, &scheduled_process->context);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      release(&scheduled_process->lock);
  
    }

    #else
    #ifdef PBS

    struct proc *low_priority = 0;
    
    // Loop over process table looking for process to run.
    //acquire(&ptable.lock);
    for(p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);

      if(p->state == RUNNABLE)
      {
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

        if(low_priority == 0)   //for the first process
        {
          low_priority = p;
        }
        else                    // for every process after the first
        {
          priority_condition = p->dynamic_priority < low_priority->dynamic_priority;

          if(priority_condition)
          {
            release(&low_priority->lock);
            low_priority = p;
          }
          else
          {
            release(&p->lock);
          }
        }
      }
      else
      {
        release(&p->lock);
      }
    }
      
    if(low_priority != 0)
    {
      low_priority->num_of_runs += 1;
      low_priority->state = RUNNING;
      c->proc = low_priority;
      
      //printf("pname %s, pid %d, rtime %d ctime %d\n", scheduled_process->name, scheduled_process->pid, scheduled_process->rtime, scheduled_process->ctime);
      swtch(&c->context, &low_priority->context);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      release(&low_priority->lock);
  
    }


    // #else
    // #ifdef MLFQ

    // for (p = proc; p < &proc[NPROC]; p++)
    // {
    //   acquire(&p->lock);
    //   if (p->state == RUNNABLE && p == 0)    
    //   {
    //     pushback(&mlfq[0], p);    //puting process into highest priority queue 
    //     p->queue = 0;
    //     p->sched_time = ticks;    // setting scheduled time
    //   }
    // }
    // struct proc *toRun = 0;
    
    // for (int i = 0; i < 5; i++)
    // {
    //   if (size(&mlfq[i]) > 0)
    //   {
    //     toRun = mlfq[i].que[mlfq[i].start];

    //     if (toRun == 0)
    //       continue;

    //    if (toRun->state == RUNNABLE)
    //       break;
    //     else          
    //       deletefront(&mlfq[i]);      // process 
    //   }
    // }
    // if (toRun != 0 && toRun->state == RUNNABLE)   //if a process to run was found
    // {
    //     toRun->num_of_runs += 1;
    //     toRun->sched_time = ticks;

    //     c->proc = toRun;
    //     toRun->state = RUNNING;

    //     swtch(&c->context, &toRun->context);

    //     if (toRun->state == SLEEPING)
    //     {
    //       struct proc * del = mlfq[p->queue].que[mlfq[p->queue].start];
    //       deletefront(&mlfq[p->queue]);
    //       pushback(&mlfq[p->queue], del);
    //     }

    //     c->proc = 0;
    // }

    // release(&p->lock);

    //#endif
    #endif

    #endif
    #endif

  }
}


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
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
