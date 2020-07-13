#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/input.h"


static void syscall_handler (struct intr_frame *);
int exception_exit (int);
bool validate (uint32_t* , int);
bool validate_string (void *);
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  
  uint32_t* args = ((uint32_t*) f->esp);
  if(!validate(args,0)){//||!validate_string((void *)args[1])){
    //f->eax = -1;
    exception_exit(-1);
  }

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */
  if (args[0] == SYS_PRACTICE) //lib/user/syscall-nr.h
  {
    f->eax = ++args[1];
  }

  if (args[0] == SYS_HALT){
    shutdown_power_off();
  }

  if (args[0] == SYS_EXEC){
    if(!validate(args,1)||!validate_string((void *)args[1])){
      //f->eax = -1;
      exception_exit(-1);
    }
    char *file = (char *)args[1];
    tid_t tid = process_execute(file);
    f->eax = tid;
  }

  if (args[0] == SYS_WAIT){
    if(!validate(args,1)){//||!validate_string((void *)args[1])){
      //f->eax = -1;
      exception_exit(-1);
    }
    tid_t tid = args[1];
    f->eax = process_wait ((int)tid);
  }

  if (args[0] == SYS_WRITE){
    int fd = args[1];
    char *buffer = args[2];
    int size = args[3];
    if (fd == 1) {
      putbuf(buffer,size);
      f->eax = size;
    }
  }

  if (args[0] == SYS_EXIT)
    {
      if(!validate(args,1))//||!validate_string((void *)args[1]))
      {
        f->eax = -1;
        exception_exit(-1);
      }
      //f->eax = args[1];
      //printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
      //thread_exit ();
      int exit_code = args[1];
      f->eax = exit_code;
      exception_exit(exit_code);
    }
}

int exception_exit (int exit_code){
      thread_current ()-> self_wait_status_t -> exit_code = exit_code;
      printf ("%s: exit(%d)\n", &thread_current ()->name, exit_code);
      thread_exit ();
}

bool validate (uint32_t* args, int num){
  struct thread *t = thread_current ();

  /* argv and all argv[i]. Check pointer address and value
     (should also be a user space ptr) */
  

  int i;
  for (i = 0; i < num + 2; i++)
    {
      if (&args[i] == NULL || 
      !(is_user_vaddr (&args[i]))||
      pagedir_get_page (t->pagedir, &args[i]) == NULL )
      return false;
    }
 
  return true;
}

bool validate_string (void *arg){
 if ((arg == NULL) || !(is_user_vaddr (arg)) ||
      pagedir_get_page (thread_current () ->pagedir, arg) == NULL)
    return false;
  if ((arg+1 == NULL) || !(is_user_vaddr (arg+1)) ||
      pagedir_get_page (thread_current () ->pagedir, arg+1) == NULL)
    return false;
  return true;
}
