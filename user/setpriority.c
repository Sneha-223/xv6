#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int pid, priority;

  if(argc < 3)
  {
    fprintf(2, "Usage: setpriority <priority> <pid>\n");
    exit(1);
  }

  priority = atoi(argv[1]);
  pid = atoi(argv[2]);

  if (priority < 0 || priority > 100) 
  {
    fprintf(2, "Invalid priority set a value from 0-100\n");
    exit(1);
  }
  
  //for debugging
  printf("pid = %d, priority = %d\n", pid, priority);

  setpriority(priority, pid);

  exit(0);
}