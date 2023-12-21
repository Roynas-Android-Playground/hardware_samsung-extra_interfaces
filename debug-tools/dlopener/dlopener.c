#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DLOPENER_PRINTF_EARLY(fmt, ...)                                        \
  printf("%s: " fmt "\n", argv[0], ##__VA_ARGS__)
#define DLOPENER_PRINTF(fmt, ...)                                              \
  printf("%s: load %s: " fmt "\n", argv[0], path, ##__VA_ARGS__)
#define DLOPENER_PERROR(operation)                                             \
  printf("%s: load %s: %s: %s\n", argv[0], path, operation, strerror(errno))

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;
  bool free_pathbuf = false;
  char *path = NULL;
  void *handle = NULL;
  struct stat buf;

  if (argc <= 1) {
    DLOPENER_PRINTF_EARLY("Please specify a module to load!");
    return ret;
  }

  path = argv[1];

  ret = lstat(path, &buf);
  if (ret == 0) {
    if (S_ISLNK(buf.st_mode)) {
      DLOPENER_PRINTF("Following symlink");
      path = realpath(path, NULL);
      if (!path) {
        DLOPENER_PERROR("realpath");
        goto out;
      }
      free_pathbuf = true;
      ret = lstat(path, &buf);
      if (ret) {
        DLOPENER_PERROR("lstat");
        goto out_free;
      }
    }
    if (!S_ISREG(buf.st_mode)) {
      DLOPENER_PRINTF("Not a regular file");
      goto out_free;
    }
  }

  handle = dlopen(path, RTLD_NOW);
  if (handle == NULL) {
    const char *err_str = dlerror() ?: "unknown";
    DLOPENER_PRINTF("Failed:\n-> %s", err_str);
  } else {
    dlclose(handle);
    DLOPENER_PRINTF("Succeeded");
    ret = EXIT_SUCCESS;
  }

out_free:
  if (free_pathbuf)
    free(path);
out:
  return ret;
}
