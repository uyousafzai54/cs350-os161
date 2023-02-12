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
#include <vfs.h>
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>

// void **args_alloc(size_t size) {
//   char ** arr = kmalloc(sizeof(char * )*(size+1));
//   arr[size] = NULL;
// }

// void args_free(char **args) {
//   size_t i = 0;
//   while(args[i]!=NULL) {
//     free(args[i]);
//     i++;
//   }
//   free(args);
// }

// int argcopy_in(char **args, size_t size) {
//   char **user_args = kmalloc(sizeof(vaddr_t) * (size+1));
//   for(size_t i =0; i<size; i++) {
//     copyinstr(args[i], user_args[i], strlen(args[i]+1), NULL);
//   }
// }

int sys_execv(char *progname, char **argv) {
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;


    char *prog = (char *) kmalloc(sizeof(char) * PATH_MAX); 
    if (prog == NULL) {
        return(ENOMEM); 
    } else {
        kprintf("arg: %s\n", progname);
        result = copyinstr((const_userptr_t) progname, prog, PATH_MAX, NULL); 
    }
    
    /* Open the file. */
    result = vfs_open(prog, O_RDONLY, 0, &v);
    if (result) {
    return result;
    }

    size_t size = 0;
    while(argv[size]!=NULL) {
      size += 1;
    }

    //copy args from old userspace to kernel space
    char **kern_args = (char **) kmalloc(sizeof(vaddr_t) * (size+1));
    kern_args[size] = NULL;
    for(size_t i =0; i<size; i++) {
      copyinstr((userptr_t) argv[i], kern_args[i], strlen(argv[i]+1), NULL);
    }

    as_destroy(curproc_getas());

    /* Create a new address space. */
    as = as_create();
    if (as ==NULL) {
      vfs_close(v);
      return ENOMEM;
    }

    
    /* Switch to it and activate it. */
    curproc_setas(as);
    as_activate();

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
      /* p_addrspace will go away when curproc is destroyed */
      vfs_close(v);
      return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    //kprintf("stackptr: 0x%08x", stackptr);
    if (result) {
      /* p_addrspace will go away when curproc is destroyed */
      return result;
    }

    // copyin((userptr_t) , kern_args, )
    //copy args from kernel to new userspace
    //TO-DO: Free used memory
    vaddr_t *userArgs = (vaddr_t *) kmalloc((size+1)*sizeof(vaddr_t));
    userArgs[size] = (vaddr_t) NULL;
    for(size_t i=0; i<size; i++) {
      size_t size = strlen(argv[i])+1;
      stackptr -= (unsigned int) (size); 
      stackptr = (unsigned int) (stackptr - (stackptr % 4));
      copyout(argv[i], (userptr_t) stackptr, size);
      userArgs[i] = (vaddr_t) stackptr;
      kprintf("arg: %s\n", argv[i]);
      kprintf("user args: %08x\n", userArgs[i]);
    }
    int argsSize = sizeof(vaddr_t) * (size+1);
    stackptr -= (unsigned int) (argsSize); 
    stackptr = (unsigned int) (stackptr - (stackptr % 4));
    copyout(userArgs, (userptr_t) stackptr, argsSize);
    /* Warp to user mode. */


    enter_new_process(size, (userptr_t) stackptr,
          stackptr, entrypoint);

    panic("enter_new_process returned\n");
	  return EINVAL;
}

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

