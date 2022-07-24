#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void
find(char *path,char *file)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){      //fstat: use fd to get stat(file info)
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
    fprintf(2, "find: directory too long\n");
    close(fd);
    return;
  }
  
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';         //add '/' to the end, and p++
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;

      if(!strcmp(".",de.name)||!strcmp("..",de.name))
        continue;

      memmove(p, de.name, DIRSIZ);      //add filename(de.name) to buf's end
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){        //stat: use filename to get stat
        fprintf(2,"ls: cannot stat %s\n", buf);
        continue;
      }
       switch(st.type){
         case T_FILE:
           if(!strcmp(file,de.name))
           {
             printf("%s\n",buf);
           }
           break;
         case T_DIR:
           find(buf,file);
       }
    }    
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: find <dir> <filename>...\n");
    exit(0);
  }
  find(argv[1],argv[2]);
  exit(0);
}
