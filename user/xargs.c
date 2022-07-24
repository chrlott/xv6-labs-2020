#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char *argv[])
{
    if(argc < 2) {
        fprintf(2, "usage: xargs command\n");
        exit(1);
    }
    char * a[MAXARG];
    int index=0;
    for(int i=1;i<argc;i++)
    {
        a[index++]=argv[i];
    }
    char buf[512]={0};
    char t[512]={0};
    a[index]=t;
    int tail=0;
    while(read(0,buf, sizeof buf)>0){
        for(int i=0;i<strlen(buf);i++)
        {
            if(buf[i]=='\n'){
                t[tail]=0;
                tail=0;
                if(fork()==0)
                {
                    exec(argv[1],a);
                }
                else wait(0);
            }
            else
              t[tail++]=buf[i];
        }
    }
    exit(0);
}