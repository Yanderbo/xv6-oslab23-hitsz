#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

void find(char *path, char *file_name) {
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    printf("find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    printf("find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if (st.type == T_DIR) {
    char buf[512];
    strcpy(buf, path);
    char *p = buf + strlen(buf);
    *p++ = '/';
    
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      if (stat(buf, &st) < 0) {
        printf("find: cannot stat %s\n", buf);
        continue;
      }

      if (st.type == T_FILE && strcmp(de.name, file_name) == 0) {
        printf("%s/%s\n", path, file_name);
      }

      if (st.type == T_DIR) {
        find(buf, file_name);
      }
    }
  }

  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf("Usage: find path file_name\n");
    exit(1);
  }

  find(argv[1], argv[2]);

  exit(0);
}
