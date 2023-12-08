#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  const char *path;
  void *handle = NULL;
  int fd, ret = EXIT_FAILURE;

  if (argc <= 1) {
    printf("Please specify a module to load!\n");
    return ret;
  }

  path = argv[1];

  // man7: EISDIR when pathname refers to a directory and the access requested
  // involved writing (that is, O_WRONLY or O_RDWR is set).
  fd = open(path, O_WRONLY);
  if (fd < 0 && errno == EISDIR) {
    printf("load: %s: is a directory\n", path);
    return ret;
  } else {
    close(fd);
  }

  handle = dlopen(path, RTLD_NOW);
  if (handle == NULL) {
    const char *err_str = dlerror();
    printf("load: module=%s\n%s\n", path, err_str ? err_str : "unknown");
  } else {
    printf("load: module=%s %s\n", path, "Success!");
    ret = EXIT_SUCCESS;
  }
  return ret;
}
