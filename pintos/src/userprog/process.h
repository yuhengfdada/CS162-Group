#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct load_info
{
    char* file_name;
    struct semaphore sema;
    bool success;
    struct dir *parent_working_dir;
};

#endif /* userprog/process.h */
