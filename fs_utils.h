#ifndef FS_UTILS_H
#define FS_UTILS_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool fs_create(const char *path, const char *type) {
  if (!path || !type)
    return false;
  if (strcmp(type, "dir") == 0) {
    return mkdir(path, 0755) == 0;
  }
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0)
    return false;
  close(fd);
  return true;
}

static int fs_unlink_cb(const char *fpath, const struct stat *sb, int typeflag,
                        struct FTW *ftwbuf) {
  (void)sb;
  (void)ftwbuf;
  int ret = 0;
  switch (typeflag) {
  case FTW_F:
  case FTW_SL:
  case FTW_SLN:
    ret = unlink(fpath);
    break;
  case FTW_DP:
    ret = rmdir(fpath);
    break;
  default:
    break;
  }
  return ret;
}

static inline bool fs_delete(const char *path) {
  struct stat st;
  if (lstat(path, &st) != 0)
    return false;
  if (S_ISDIR(st.st_mode)) {
    return nftw(path, fs_unlink_cb, 16, FTW_DEPTH | FTW_PHYS) == 0;
  }
  return unlink(path) == 0;
}

static inline bool copy_file(const char *src, const char *dest, mode_t mode) {
  int in = open(src, O_RDONLY);
  if (in < 0)
    return false;
  int out = open(dest, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (out < 0) {
    close(in);
    return false;
  }
  char buf[8192];
  ssize_t n;
  bool ok = true;
  while ((n = read(in, buf, sizeof(buf))) > 0) {
    ssize_t w = write(out, buf, (size_t)n);
    if (w != n) {
      ok = false;
      break;
    }
  }
  if (n < 0)
    ok = false;
  close(in);
  close(out);
  return ok;
}

static inline bool copy_dir(const char *src, const char *dest) {
  struct stat st;
  if (stat(src, &st) != 0)
    return false;
  if (mkdir(dest, st.st_mode) != 0 && errno != EEXIST)
    return false;

  DIR *d = opendir(src);
  if (!d)
    return false;
  struct dirent *e;
  bool ok = true;
  char s_path[PATH_MAX];
  char d_path[PATH_MAX];
  while ((e = readdir(d)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
      continue;
    snprintf(s_path, sizeof(s_path), "%s/%s", src, e->d_name);
    snprintf(d_path, sizeof(d_path), "%s/%s", dest, e->d_name);
    if (lstat(s_path, &st) != 0) {
      ok = false;
      break;
    }
    if (S_ISDIR(st.st_mode)) {
      if (!copy_dir(s_path, d_path)) {
        ok = false;
        break;
      }
    } else if (!copy_file(s_path, d_path, st.st_mode)) {
      ok = false;
      break;
    }
  }
  closedir(d);
  return ok;
}

static inline bool fs_copy(const char *src, const char *dest) {
  struct stat st;
  if (lstat(src, &st) != 0)
    return false;
  if (S_ISDIR(st.st_mode))
    return copy_dir(src, dest);
  else
    return copy_file(src, dest, st.st_mode);
}

static inline bool fs_move(const char *src, const char *dest) {
  if (rename(src, dest) == 0)
    return true;
  if (errno == EXDEV) {
    if (fs_copy(src, dest))
      return fs_delete(src);
  }
  return false;
}

static inline bool fs_write(const char *path, const char *content,
                            const char *mode) {
  FILE *f = fopen(path, mode);
  if (!f)
    return false;
  size_t len = strlen(content);
  bool ok = fwrite(content, 1, len, f) == len;
  fclose(f);
  return ok;
}

static inline char *fs_read(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return NULL;
  }
  buf[sz] = '\0';
  fclose(f);
  return buf;
}

static inline bool fs_unpack(const char *tar_path, const char *dest) {
  if (!tar_path || !dest)
    return false;
  char cmd[PATH_MAX * 2 + 32];
  snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s'", tar_path, dest);
  return system(cmd) == 0;
}

static inline bool fs_chmod(const char *path, mode_t mode) {
  return chmod(path, mode) == 0;
}

static inline bool fs_chown(const char *path, const char *spec) {
  if (!path || !spec)
    return false;
  char *tmp = strdup(spec);
  if (!tmp)
    return false;
  char *grp = strchr(tmp, ':');
  if (grp)
    *grp++ = '\0';
  struct passwd *pw = getpwnam(tmp);
  uid_t uid = pw ? pw->pw_uid : (uid_t)-1;
  gid_t gid = (gid_t)-1;
  if (grp) {
    struct group *g = getgrnam(grp);
    gid = g ? g->gr_gid : (gid_t)-1;
  }
  free(tmp);
  if (uid == (uid_t)-1 && gid == (gid_t)-1)
    return false;
  return chown(path, uid == (uid_t)-1 ? -1 : uid,
               gid == (gid_t)-1 ? -1 : gid) == 0;
}

static inline const char *rand_choice(const char **options, size_t count) {
  if (!options || count == 0)
    return NULL;
  size_t idx = (size_t)rand() % count;
  return options[idx];
}

static inline void seed_apply(unsigned int seed) { srand(seed); }

#ifdef __cplusplus
}
#endif

#endif /* FS_UTILS_H */
