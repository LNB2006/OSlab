#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char *argv[])
{
    int p[2];//子进程读，父进程写
    pipe(p);
    int q[2];//父进程读，子进程写
    pipe(q);
    int parent_pid = getpid();
    int pid;
    pid = fork();
    int child_pid;
    if(pid == 0)
    {
        // child process
        child_pid = getpid();
        close(p[1]);
        close(q[0]);
        int x;
        read(p[0], &x, sizeof(x));
        printf("%d: received ping from pid %d\n", child_pid, parent_pid);
        write(q[1], &x, sizeof(x));
    }
    else
    {
        // parent process
        close(p[0]);
        close(q[1]);
        int x = 1;
        write(p[1], &x, sizeof(x));
        read(q[0], &x, sizeof(x));
        printf("%d: received pong from pid %d\n", parent_pid, pid);
    }
   exit(0);
}
