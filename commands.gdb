set print null-stop on
tbreak exec
c
p cpus[$tp].proc->name
tbreak kernel/exec.c:100
c
p cpus[$tp].proc->name
