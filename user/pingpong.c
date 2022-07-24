#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int p0[2],p1[2]; //build two pipes
    pipe(p0);
    pipe(p1);
    char buf[5];
    if(fork()==0)
    {
        read(p0[0],buf,4);
        int pid=getpid();
        printf("%d: received %s\n",pid,buf);  //print the content received from p0
        close(p0[0]);
        close(p0[1]);   //close pipe0
        write(p1[1],"pong",4);
        close(p1[0]);
        close(p1[1]);   //close pipe1
    }
    else{
        write(p0[1],"ping",4);   //write the content to p0
        close(p0[0]);
        close(p0[1]);
        int pid=getpid();
        read(p1[0],buf,4);
        printf("%d: received %s\n",pid,buf);
        close(p1[0]);
        close(p1[1]);
    }
    exit(0);
}
