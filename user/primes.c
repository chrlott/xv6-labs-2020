// Lab Xv6 and Unix utilities
// primes.c

#include "kernel/types.h"
#include "user/user.h"
#include "stddef.h"

void
mapping(int n, int pd[])
{
  close(n);
  dup(pd[n]);
  close(pd[0]);
  close(pd[1]);
}

void
primes()
{
  int a=0, buf=0;
  int fd[2];
  if (read(0, &a, sizeof(int)))
  {
    printf("prime %d\n", a);   //printf uses file descriptor 1 to print
    pipe(fd);
    if (fork() == 0)
    {
      mapping(1, fd);
      while (read(0, &buf, sizeof(int)))
      {
        if (buf % a != 0)
        {
          write(1, &buf, sizeof(int));
        }
      }
    }
    else
    {
      wait(NULL);
      mapping(0, fd);
      primes();
    }  
  }  
}

int 
main(int argc, char *argv[])
{
  int fd[2];
  pipe(fd);
  if (fork() == 0)
  {
    mapping(1, fd);
    for (int i = 2; i < 36; i++)
    {
      write(1, &i, sizeof(int));
    }
  }
  else
  {
    wait(0);
    mapping(0, fd);
    primes();
  }
  exit(0);
}
