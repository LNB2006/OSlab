#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define PATH_SIZE 512

// 判断路径最后一段是否与目标名称相同。
static int
match_name(char *path, char *target)
{
    int end = strlen(path);

    // 忽略路径末尾的斜杠。
    while(end > 0 && path[end - 1] == '/')
        end--;

    int start = end;
    while(start > 0 && path[start - 1] != '/')
        start--;

    int len = end - start;

    return len == strlen(target) &&
           memcmp(path + start, target, len) == 0;
}

static int
find(char *path, char *target)
{
    int fd = open(path, O_RDONLY);
    if(fd < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return 1;
    }

    struct stat st;
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return 1;
    }

    // 文件和目录都可以匹配，每个路径只打印一次。
    if((st.type == T_FILE || st.type == T_DIR) &&
       match_name(path, target)){
        printf("%s\n", path);
    }

    // 普通文件不需要继续遍历。
    if(st.type != T_DIR){
        close(fd);
        return 0;
    }

    struct dirent de;
    char name[DIRSIZ + 1];
    char child[PATH_SIZE];

    int path_len = strlen(path);
    int need_slash = path_len > 0 && path[path_len - 1] != '/';
    int status = 0;
    int n;

    // 逐个读取目录项。
    while((n = read(fd, &de, sizeof(de))) == sizeof(de)){
        if(de.inum == 0)
            continue;

        // 目录项名称不保证以 '\0' 结尾，需要手动补上。
        memmove(name, de.name, DIRSIZ);
        name[DIRSIZ] = '\0';

        // 跳过当前目录和父目录，避免无限递归。
        if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        int name_len = strlen(name);

        if(path_len + need_slash + name_len + 1 > PATH_SIZE){
            fprintf(2, "find: path too long under %s\n", path);
            status = 1;
            continue;
        }

        // 拼接子路径，例如 "." + "/" + "a" 得到 "./a"。
        memmove(child, path, path_len);

        int pos = path_len;
        if(need_slash)
            child[pos++] = '/';

        memmove(child + pos, name, name_len + 1);

        // 子路径是文件时检查名字，是目录时继续遍历。
        if(find(child, target) != 0)
            status = 1;
    }

    if(n != 0){
        fprintf(2, "find: cannot read directory %s\n", path);
        status = 1;
    }

    close(fd);
    return status;
}

int
main(int argc, char *argv[])
{
    if(argc != 3){
        fprintf(2, "Usage: find <path> <name>\n");
        exit(1);
    }

    exit(find(argv[1], argv[2]));
}