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
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/kernel/list.h"
#include "stdlib.h"

/* Declare helper functions */
static void syscall_handler (struct intr_frame *);
int exception_exit (int);
bool validate (uint32_t* , int);
bool validate_string (void *);
bool validate_ptr (uint32_t *args, int num);
static int add_file_descriptor(struct file *curr_file);

/* optimize with array of function pointers */
void (*syscalls[SYSCALL_NUM])(struct intr_frame * f);
static void syscall_practice(struct intr_frame * f);

static void syscall_halt(struct intr_frame * f UNUSED);
static void syscall_exec(struct intr_frame * f);
static void syscall_wait(struct intr_frame * f);
static void syscall_exit(struct intr_frame * f);

//Task 3 syscalls
static void syscall_create(struct intr_frame * f);
static void syscall_remove(struct intr_frame * f);
static void syscall_close(struct intr_frame * f);
static void syscall_open(struct intr_frame * f);
static void syscall_filesize(struct intr_frame * f);
static void syscall_read(struct intr_frame *f);

static void syscall_write(struct intr_frame * f);


void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  syscalls[SYS_PRACTICE] = syscall_practice;
  syscalls[SYS_HALT]     = syscall_halt;
  syscalls[SYS_EXEC]     = syscall_exec;
  syscalls[SYS_WAIT]     = syscall_wait;
  syscalls[SYS_EXIT]     = syscall_exit;
  syscalls[SYS_WRITE]    = syscall_write;
  // Task 3 syscalls
  syscalls[SYS_CREATE]   = syscall_create;
  syscalls[SYS_REMOVE]   = syscall_remove;
  syscalls[SYS_CLOSE]    = syscall_close;
  syscalls[SYS_OPEN]     = syscall_open;
  syscalls[SYS_FILESIZE] = syscall_filesize;
  syscalls[SYS_READ]     = syscall_read;


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
  // printf("System call number: %d\n", args[0]);
  (*syscalls[(int)args[0]])(f);
}


static void syscall_practice(struct intr_frame * f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  f->eax = ++args[1];
}

static void syscall_halt(struct intr_frame * f)
{
  shutdown_power_off();
}

static void syscall_exec(struct intr_frame * f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  if(!validate(args,1)||!validate_string((void *)args[1])){
    exception_exit(-1);
  }
  char *file = (char *)args[1];
  tid_t tid = process_execute(file);
  f->eax = tid;
}

static void syscall_wait(struct intr_frame * f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  if(!validate(args,1)){//||!validate_string((void *)args[1])){
    exception_exit(-1);
  }
  tid_t tid = args[1];
  f->eax = process_wait ((int)tid);
}

static void syscall_exit(struct intr_frame * f)
{
  uint32_t* args = ((uint32_t*) f->esp);
  if(!validate(args,1))//||!validate_string((void *)args[1]))
  {
    f->eax = -1;
    exception_exit(-1);
  }
  int exit_code = args[1];
  f->eax = exit_code;
  exception_exit(exit_code);
}

static void syscall_create(struct intr_frame *f) {
  uint32_t *args = ((uint32_t *)f->esp);
  if (!validate(args,1)||!validate_string((void *)args[1])) {
    exception_exit(-1);
  } else {
    char *name = (char *)args[1];
    off_t initial_size = args[2];
    f->eax = filesys_create(name, initial_size);
  }
}

static void syscall_remove(struct intr_frame *f) {
  uint32_t *args = ((uint32_t *)f->esp);
  char *name = (char *)args[1];
  f->eax = filesys_remove(name);
}

static void syscall_open(struct intr_frame * f){
  uint32_t *args = ((uint32_t *)f->esp);
  char *name = (char *)args[1];
  if (!validate(args,1)||!validate_string((void *)args[1])) {
    exception_exit(-1);
  } else if (name[0] == '\0') {
    f->eax = -1;
  } else {
    struct file *curr_file = filesys_open(name);
    if (!curr_file) {
      f->eax = -1;
    } else {
      int fd = add_file_descriptor(curr_file);
      f->eax = fd;
    }
  }
}

static void syscall_filesize(struct intr_frame * f){
  uint32_t *args = ((uint32_t*)f->esp);
  int fd = args[1];

  struct thread *t = thread_current();
  struct file_descriptor *temp = NULL;
  struct list_elem *e;
  for (e = list_begin(&(t->file_descriptors)); e != list_end(&(t->file_descriptors)); e = list_next(e)) {
    temp = list_entry(e, struct file_descriptor, elem);
    if (temp->fd == fd) {
      struct file *curr_file = temp->curr_file;
      f->eax = file_length(curr_file);
      break;
    }
  }
}

static void syscall_read(struct intr_frame *f) {
  uint32_t *args = ((uint32_t*)f->esp);
  if (!validate(args,3)) {
    exception_exit(-1);
  } else {
    int fd = args[1];
    char *buffer = (char*)args[2];
    int size = args[3];

    /* Read input from standard input. */
    if (fd == 0) {
      int bytes_read = 0;
      uint8_t *bytes_buffer = (uint8_t *)args[2];
      uint8_t temp;
      while (bytes_read < size && (temp = input_getc()) != 0) {
        *bytes_buffer = temp;
        bytes_buffer += 1;
        bytes_read += 1;
      }
      f->eax = bytes_read;
    } else { // Otherwise
      struct thread *t = thread_current();
      struct file_descriptor *temp = NULL;
      struct file_descriptor *found = NULL;
      struct list_elem *e;
      for (e = list_begin(&(t->file_descriptors)); e != list_end(&(t->file_descriptors)); e = list_next(e)) {
        temp = list_entry(e, struct file_descriptor, elem);
        if (temp->fd == fd) {
          found = temp;
          break;
        }
      }
      if (!found) {
        exception_exit(-1);
      } else {
        struct file *curr_file = found->curr_file;
        f->eax = file_read(curr_file, buffer, size);
      }
    }
  }
}

static void syscall_write(struct intr_frame * f)
{
  uint32_t *args = ((uint32_t*)f->esp);
  if (!validate(args,3)) {
    exception_exit(-1);
  } else {
    int fd = args[1];
    char *buffer = (char *)args[2];
    int size = args[3];

    /* Write output to standard output */
    if (fd == 1) {
      putbuf(buffer, size);
      f->eax = size;
    } else { // Otherwise
      struct thread *t = thread_current();
      struct file_descriptor *temp = NULL;
      struct file_descriptor *found = NULL;
      struct list_elem *e;
      for (e = list_begin(&(t->file_descriptors)); e != list_end(&(t->file_descriptors)); e = list_next(e)) {
        temp = list_entry(e, struct file_descriptor, elem);
        if (temp->fd == fd) {
          found = temp;
          break;
        }
      }
      if (!found) {
        exception_exit(-1);
      } else {
        struct file *curr_file = found->curr_file;
        f->eax = file_write(curr_file, buffer, size);
      }
    }
  }
}

static void syscall_close(struct intr_frame * f){
  uint32_t* args = ((uint32_t*) f->esp);
  struct file *file = NULL;
  int fd = args[1];
  if (fd == 1){
    file = (struct file *) args[1];
  }
  file_close(file);
}

/* Exit when there is an exception. */
int exception_exit (int exit_code){
      thread_current ()-> self_wait_status_t -> exit_code = exit_code;
      printf ("%s: exit(%d)\n", &thread_current ()->name, exit_code);
      thread_exit ();
}

/* argv and all argv[i]. Check pointer address and value
     (should also be a user space ptr) */
bool validate (uint32_t* args, int num){
  struct thread *t = thread_current ();
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

/* Check whether the address is valid. In the case that the address is at 
   the edge of the boundary, we also check whether the next address is valid
   or not. */
bool validate_string (void *arg){
  if ((arg == NULL) || !(is_user_vaddr (arg)) ||
      pagedir_get_page (thread_current () ->pagedir, arg) == NULL)
    return false;
  if ((arg+1 == NULL) || !(is_user_vaddr (arg+1)) ||
      pagedir_get_page (thread_current () ->pagedir, arg+1) == NULL)
    return false;
  return true;
}

/* Given a file object, this helper functions returns the corresponding file descriptor. */
static int add_file_descriptor(struct file *curr_file) {
  struct thread *t = thread_current();
  struct file_descriptor *curr_fd = (struct file_descriptor*)malloc(sizeof(struct file_descriptor));
  curr_fd->fd = t->fd_count;
  curr_fd->curr_file = curr_file;
  list_push_back(&(t->file_descriptors), &(curr_fd->elem));
  t->fd_count += 1;
  return curr_fd->fd;
}
