#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DLOPENER_PRINTF_EARLY(fmt, ...) printf("%s: " fmt "\n", argv[0], ##__VA_ARGS__)
#define DLOPENER_PRINTF(fmt, ...) printf("%s: load %s: " fmt "\n", argv[0], path, ##__VA_ARGS__)
#define DLOPENER_PERROR(operation) printf("%s: load %s: %s: %s\n", argv[0], path, operation, strerror(errno))

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE, free_pathbuf = 0;
  char *path = NULL;
  void *handle = NULL;
  struct stat buf;

  if (argc <= 1) {
    DLOPENER_PRINTF_EARLY("Please specify a module to load!");
    return ret;
  }

  path = argv[1];

  ret = lstat(path, &buf);
  if (ret < 0) {
    DLOPENER_PERROR("stat");
    DLOPENER_PRINTF("Will use system library path");
  } else if (S_ISLNK(buf.st_mode)) {
    DLOPENER_PRINTF("Following symlink");
    path = realpath(path, NULL);
    if (!path) {
      DLOPENER_PERROR("realpath");
      return ret;
    }
    free_pathbuf = 1;
  } else if (!S_ISREG(buf.st_mode)) {
    DLOPENER_PRINTF("load %s: Not a regular file", path);
    return ret;
  }

  handle = dlopen(path, RTLD_NOW);
  if (handle == NULL) {
    const char *err_str = dlerror();
    DLOPENER_PRINTF("Failed:\n-> %s", err_str ? err_str : "unknown");
  } else {
    DLOPENER_PRINTF("Succeeded");
    ret = EXIT_SUCCESS;
  }
  if (free_pathbuf)
    free(path);
  return ret;
}
