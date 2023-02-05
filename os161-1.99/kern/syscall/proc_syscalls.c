#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include "opt-A1.h"
#include <clock.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);
  #if OPT_A1
    while(array_num(curproc->p_children)!=0) {
      struct proc *temp_child = array_get(curproc->p_children, 0);
      array_remove(curproc->p_children, 0);
      spinlock_acquire(&temp_child->p_lock);
      if(temp_child->p_exitstatus==1) {
        spinlock_release(&temp_child->p_lock);
        proc_destroy(temp_child);
      } else {
        temp_child->p_parent = NULL;
        spinlock_release(&temp_child->p_lock);
      }
    }
  #endif



  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);
  #if OPT_A1
    spinlock_acquire(&p->p_lock);
    if(p->p_parent!=NULL) {
      //1 means this process has exited
      p->p_exitstatus = 1;
      p->p_exitcode = exitcode;
      spinlock_release(&p->p_lock);
    } else {
      spinlock_release(&p->p_lock);
      proc_destroy(p);
    }
  #else
    proc_destroy(p);
  #endif

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  #if OPT_A1
    *retval = curproc->p_pid;
  #else
    *retval = 1;
  #endif
  return(0);
}

#if OPT_A1
int sys_fork(pid_t *retval, struct trapframe *tf) {
  struct proc *newProc = proc_create_runprogram("child");
  newProc->p_parent = curproc;
  array_add(curproc->p_children, newProc, NULL);
  as_copy(curproc_getas(), &newProc->p_addrspace);
  struct trapframe *newTF = kmalloc(sizeof(struct trapframe));
  memcpy(newTF, tf, sizeof(struct trapframe));
  const char *name = "child_thread";
  thread_fork(name, newProc, (void*)enter_forked_process, newTF, 0);
  *retval = newProc->p_pid;
  clocksleep(1);
  return 0;
}
#endif

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  struct proc *temp_child = NULL;
  for(unsigned int i = 0; i<array_num(curproc->p_children); i++) {
    temp_child = array_get(curproc->p_children, i);
    if(temp_child->p_pid == pid) {
      array_remove(curproc->p_children, i);
      break;
    }
    temp_child = NULL;
  }
  if(temp_child==NULL) {
    return(ESRCH);
  }

  spinlock_acquire (& temp_child -> p_lock );
    while (temp_child ->p_exitstatus!=1) {
      spinlock_release (&temp_child ->p_lock);
      clocksleep (1);
      spinlock_acquire (&temp_child->p_lock);
  }
  spinlock_release (&temp_child->p_lock );
  exitstatus = _MKWAIT_EXIT(temp_child->p_exitcode); 
  proc_destroy(temp_child);

  /* for now, just pretend the exitstatus is 0 */
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

