/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>

#ifdef HAVE_SELINUX
#include <selinux/label.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <private/android_filesystem_config.h>

#include "log.h"
#include "util.h"

/*
 * gettime() - returns the time in seconds of the system's monotonic clock or
 * zero on error.
 */
time_t gettime(void)
{
    struct timespec ts;
    int ret;

    ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret < 0) {
        ERROR("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return 0;
    }

    return ts.tv_sec;
}

/*
 * android_name_to_id - returns the integer uid/gid associated with the given
 * name, or -1U on error.
 */
static unsigned int android_name_to_id(const char *name)
{
    struct android_id_info const *info = android_ids;
    unsigned int n;

    for (n = 0; n < android_id_count; n++) {
        if (!strcmp(info[n].name, name))
            return info[n].aid;
    }

    return -1U;
}

/*
 * decode_uid - decodes and returns the given string, which can be either the
 * numeric or name representation, into the integer uid or gid. Returns -1U on
 * error.
 */
unsigned int decode_uid(const char *s)
{
    unsigned int v;

    if (!s || *s == '\0')
        return -1U;
    if (isalpha(s[0]))
        return android_name_to_id(s);

    errno = 0;
    v = (unsigned int) strtoul(s, 0, 0);
    if (errno)
        return -1U;
    return v;
}

int mkdir_recursive(const char *pathname, mode_t mode)
{
    char buf[128];
    const char *slash;
    const char *p = pathname;
    int width;
    int ret;
    struct stat info;

    while ((slash = strchr(p, '/')) != NULL) {
        width = slash - pathname;
        p = slash + 1;
        if (width < 0)
            break;
        if (width == 0)
            continue;
        if ((unsigned int)width > sizeof(buf) - 1) {
            ERROR("path too long for mkdir_recursive\n");
            return -1;
        }
        memcpy(buf, pathname, width);
        buf[width] = 0;
        if (stat(buf, &info) != 0) {
            ret = mkdir(buf, mode);
            if (ret && errno != EEXIST)
                return ret;
        }
    }
    ret = mkdir(pathname, mode);
    if (ret && errno != EEXIST)
        return ret;
    return 0;
}

/*
* replaces any unacceptable characters with '_', the
* length of the resulting string is equal to the input string
*/
void sanitize(char *s)
{
    const char* accept =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789"
            "_-.";

    if (!s)
        return;

    for (; *s; s++) {
        s += strspn(s, accept);
        if (*s) *s = '_';
    }
}

int make_link(const char *oldpath, const char *newpath)
{
    int ret;
    char buf[256];
    char *slash;
    int width;

    slash = strrchr(newpath, '/');
    if (!slash)
        return -1;

    width = slash - newpath;
    if (width <= 0 || width > (int)sizeof(buf) - 1)
        return -1;

    memcpy(buf, newpath, width);
    buf[width] = 0;
    ret = mkdir_recursive(buf, 0755);
    if (ret)
    {
        ERROR("Failed to create directory %s: %s (%d)\n", buf, strerror(errno), errno);
        return -1;
    }

    ret = symlink(oldpath, newpath);
    if (ret && errno != EEXIST)
    {
        ERROR("Failed to symlink %s to %s: %s (%d)\n", oldpath, newpath, strerror(errno), errno);
        return -1;
    }
    return 0;
}

void remove_link(const char *oldpath, const char *newpath)
{
    char path[256];
    ssize_t ret;
    ret = readlink(newpath, path, sizeof(path) - 1);
    if (ret <= 0)
        return;
    path[ret] = 0;
    if (!strcmp(path, oldpath))
        unlink(newpath);
}

int wait_for_file(const char *filename, int timeout)
{
    struct stat info;
    time_t timeout_time = gettime() + timeout;
    int ret = -1;

    while (gettime() < timeout_time && ((ret = stat(filename, &info)) < 0))
        usleep(10000);

    return ret;
}

int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "r");
    if(!in)
        return -1;

    FILE *out = fopen(to, "w");
    if(!out)
    {
        fclose(in);
        return -1;
    }

    fseek(in, 0, SEEK_END);
    int size = ftell(in);
    rewind(in);

    char *buff = malloc(size);
    fread(buff, 1, size, in);
    fwrite(buff, 1, size, out);

    fclose(in);
    fclose(out);
    free(buff);
    return 0;
}

int mkdir_with_perms(const char *path, mode_t mode, const char *owner, const char *group)
{
    int ret;

    ret = mkdir(path, mode);
    /* chmod in case the directory already exists */
    if (ret == -1 && errno == EEXIST) {
        ret = chmod(path, mode);
    }
    if (ret == -1) {
        return -errno;
    }

    if(owner)
    {
        uid_t uid = decode_uid(owner);
        gid_t gid = -1;

        if(group)
            gid = decode_uid(group);

        if(chown(path, uid, gid) < 0)
            return -errno;
    }
    return 0;
}

int write_file(const char *path, const char *value)
{
    int fd, ret, len;

    fd = open(path, O_WRONLY|O_CREAT, 0622);

    if (fd < 0)
    {
        ERROR("Failed to open file %s (%d: %s)\n", path, errno, strerror(errno));
        return -errno;
    }

    len = strlen(value);

    do {
        ret = write(fd, value, len);
    } while (ret < 0 && errno == EINTR);

    close(fd);
    if (ret < 0) {
        return -errno;
    } else {
        return 0;
    }
}

int remove_dir(const char *dir)
{
    struct DIR *d = opendir(dir);
    if(!d)
        return -1;

    struct dirent *dt;
    int res = 0;

    int dir_len = strlen(dir) + 1;
    char *n = malloc(dir_len + 1);
    strcpy(n, dir);
    strcat(n, "/");

    while(res == 0 && (dt = readdir(d)))
    {
        if(dt->d_name[0] == '.' && (dt->d_name[1] == '.' || dt->d_name[1] == 0))
            continue;

        n = realloc(n, dir_len + strlen(dt->d_name) + 1);
        n[dir_len] = 0;
        strcat(n, dt->d_name);

        if(dt->d_type == DT_DIR)
        {
            if(remove_dir(n) < 0)
                res = -1;
        }
        else
        {
            if(remove(n) < 0)
                res = -1;
        }
    }

    free(n);
    closedir(d);

    if(res == 0 && remove(dir) < 0)
        res = -1;
    return res;
}

void stdio_to_null(void)
{
    int fd = open("/dev/null", O_RDWR);
    if(fd >= 0)
    {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }
}

int run_cmd(char **cmd)
{
    pid_t pID = vfork();
    if(pID == 0)
    {
        stdio_to_null();
        execve(cmd[0], cmd, NULL);
        _exit(127);
    }
    else
    {
        int status = 0;
        while(waitpid(pID, &status, WNOHANG) == 0)
            usleep(50000);
        return status;
    }
}

char *run_get_stdout(char **cmd)
{
   int fd[2];
   if(pipe(fd) < 0)
        return NULL;

    pid_t pid = vfork();
    if (pid < 0)
    {
        close(fd[0]);
        close(fd[1]);
        return NULL;
    }

    if(pid == 0) // child
    {
        close(fd[0]);
        dup2(fd[1], 1);  // send stdout to the pipe
        dup2(fd[1], 2);  // send stderr to the pipe
        close(fd[1]);

        execv(cmd[0], cmd);
        _exit(127);
    }
    else
    {
        close(fd[1]);

        char *res = malloc(512);
        char buffer[512];
        int size = 512, written = 0, len;
        while ((len = read(fd[0], buffer, sizeof(buffer))) > 0)
        {
            if(written + len + 1 > size)
            {
                size = written + len + 256;
                res = realloc(res, size);
            }
            memcpy(res+written, buffer, len);
            written += len;
            res[written] = 0;
        }

        close(fd[0]);

        if(written == 0)
        {
            free(res);
            return NULL;
        }
        return res;
    }
    return NULL;
}

uint32_t timespec_diff(struct timespec *f, struct timespec *s)
{
    uint32_t res = 0;
    if(s->tv_nsec-f->tv_nsec < 0)
    {
        res = (s->tv_sec-f->tv_sec-1)*1000;
        res += 1000 + ((s->tv_nsec-f->tv_nsec)/1000000);
    }
    else
    {
        res = (s->tv_sec-f->tv_sec)*1000;
        res += (s->tv_nsec-f->tv_nsec)/1000000;
    }
    return res;
}

char *readlink_recursive(const char *link)
{
    struct stat info;
    if(lstat(link, &info) < 0)
        return NULL;

    if(!S_ISLNK(info.st_mode))
        return strdup(link);

    char path[256];
    char buff[256];
    char *p = (char*)link;

    while(S_ISLNK(info.st_mode))
    {
        if(info.st_size >= sizeof(path)-1)
        {
            ERROR("readlink_recursive(): Couldn't resolve, too long path.\n");
            return NULL;
        }

        if(readlink(p, buff, info.st_size) != info.st_size)
        {
            ERROR("readlink_recursive: readlink() failed on %s!\n", p);
            return NULL;
        }

        buff[info.st_size] = 0;
        strcpy(path, buff);
        p = path;

        if(lstat(buff, &info) < 0)
        {
            ERROR("readlink_recursive: couldn't do lstat on %s!\n", buff);
            return NULL;
        }
    }

    return strdup(buff);
}

int imin(int a, int b)
{
    return (a < b) ? a : b;
}

int imax(int a, int b)
{
    return (a > b) ? a : b;
}

int in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    if(x < rx || y < ry)
        return 0;

    if(x > rx+rw || y > ry+rh)
        return 0;
    return 1;
}

char *parse_string(char *src)
{
    char *start = strchr(src, '"');
    char *end = strrchr(src, '"');

    if(!start || start == end || start+1 == end)
        return NULL;
    ++start;
    return strndup(start, end-start);
}

// alloc and fill with 0s
void *mzalloc(size_t size)
{
    void *res = malloc(size);
    memset(res, 0, size);
    return res;
}
