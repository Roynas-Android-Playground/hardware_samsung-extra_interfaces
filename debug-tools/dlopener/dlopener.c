#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  const char *path;
  struct stat buf;
  void *handle = NULL;
  int ret = EXIT_FAILURE;

  if (argc <= 1) {
    printf("Please specify a module to load!\n");
    return ret;
  }

  path = argv[1];

  ret = lstat(path, &buf);
  if (ret < 0) {
    printf("load %s: stat: %s\n", path, strerror(errno));
    return ret;
  } else if (S_ISLNK(buf.st_mode)) {
    char link[PATH_MAX];
    printf("load %s: Following symlink\n", path);
    ret = readlink(path, link, sizeof(link) - 1);
    if (ret < 0) {
      printf("load %s: readlink: %s\n", path, strerror(errno));
      return ret;
    }
    link[PATH_MAX - 1] = '\0';
    path = link;
  } else if (!S_ISREG(buf.st_mode)) {
    printf("load %s: Not a regular file\n", path);
    return ret;
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
