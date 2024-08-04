#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

/*
a tool to move files in the format wkN_lab{0,1}_title?.ext to appropriate
directories
*/

char* PATTERN = "wk([0-9]{1,2})_(lec[0-9]{1,2}|lab|ws)_([a-z]{4}[0-9]{4})_([a-"
                "zA-Z0-9]+)\\.([a-zA-Z0-9]+)";
const char* DL_DIR = "/home/dell/dls";

enum grps {
    WEEK,
    TYPE,
    CODE,
    TITLE,
    EXT,
};

char* base_dir = "/home/dell/uwa/sem2_2024";

void handle_events(int fd, int wd);
void parse_reg(const char* name);
void process_file(const char* name, char** strmatches, int nmatch);
int strtonum(char* match);
void copy_file(char* src, char* dst);

int main()
{
    int fd = inotify_init1(IN_NONBLOCK);
    int wd = inotify_add_watch(fd, DL_DIR, IN_CREATE);
    struct pollfd fds[1];

    if (fd == -1) {
        perror("inotify_init1");
        exit(EXIT_FAILURE);
    }

    fds[0].fd = fd;
    fds[0].events = POLLIN;

    while (1) {
        int poll_num = poll(fds, 1, -1);
        if (poll_num == -1) {
            if (errno == EINTR)
                continue;
            perror("poll");
            exit(EXIT_FAILURE);
        }

        if (poll_num > 0) {
            if (fds[0].revents & POLLIN)
                handle_events(fd, wd);
        }
    }

    printf("stopping now");
    close(fd);
    exit(EXIT_SUCCESS);
}

void handle_events(int fd, int wd)
{
    /*
      Some systems cannot read integer variables if they are not
      properly aligned. On other systems, incorrect alignment may
      decrease performance. Hence, the buffer used for reading from
      the inotify file descriptor should have the same alignment as
      struct inotify_event.
    */
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event* event;
    ssize_t len;

    for (;;) {
        len = read(fd, buf, sizeof(buf));

        if ((int)len == -1 && errno != EAGAIN) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        // no more events to read
        if (len <= 0)
            break;

        // loop over all events
        for (char* ptr = buf; ptr < buf + len;
             ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event*)ptr;

            if ((event->mask & IN_CREATE) && !(event->mask & IN_ISDIR)
                && event->len) {
                parse_reg(event->name);
            }
        }
    }
}

void parse_reg(const char* name)
{
    regex_t rebuf;

    int result_comp = regcomp(&rebuf, PATTERN, REG_EXTENDED);
    if (!result_comp) {
        printf("compiled regex successfully\n");
        printf("found %lu groups\n", rebuf.re_nsub);
    } else {
        printf("failed to compile with error code %d\n", result_comp);
    }

    int nmatch = rebuf.re_nsub;
    regmatch_t matches[nmatch + 1];
    if (!regexec(&rebuf, name, nmatch + 1, matches, 0)) {
        char* strmatches[nmatch];

        for (int i = 1; i < nmatch + 1; i++) {
            strmatches[i - 1]
                = (char*)malloc(matches[i].rm_eo - matches[i].rm_so + 1);
            strmatches[i - 1] = strndup(
                name + matches[i].rm_so, matches[i].rm_eo - matches[i].rm_so);
            printf("match %d: %s\n", i,
                strndup(name + matches[i].rm_so,
                    matches[i].rm_eo - matches[i].rm_so));
        }

        process_file(name, strmatches, nmatch);
    } else {
        printf("no match found\n");
    }
}

void process_file(const char* name, char** matches, int nmatch)
{
    char new_name[1000];
    char new_dir[4096];

    strcat(strcat(strcat(strcpy(new_dir, base_dir), "/"), matches[CODE]), "/");

    // different lab dir for systems and ds
    if (!strncmp(matches[TYPE], "lab", 3)
        && (!strcmp(matches[CODE], "cits2002")
            || !strcmp(matches[CODE], "cits2211"))) {
        strcat(strcat(strcat(new_dir, "src/wk"), matches[WEEK]), "/");
    } else {
        strcat(strcat(strcat(new_dir, "week "), matches[WEEK]), "/");
        if (!strncmp(matches[TYPE], "lab", 3)) {
            mkdir(new_dir, 0755);
            strcat(new_dir, "lab/");
        }
    }

    // plain name for systems and stat labs unless the ext is rmd
    // or html
    if ((!strcmp(matches[CODE], "cits2002")
            || !strcmp(matches[CODE], "stat2402"))
        && !strncmp(matches[TYPE], "lab", 3) && strcmp(matches[EXT], "Rmd")
        && strcmp(matches[EXT], "html")) {
        strcat(strcat(strcpy(new_name, matches[TITLE]), "."), matches[EXT]);
    } else {
        strcat(strcat(strcat(strcat(strcat(strcat(strcat(strcpy(new_name, "wk"),
                                                      matches[WEEK]),
                                               "_"),
                                        matches[TYPE]),
                                 "_"),
                          matches[TITLE]),
                   "."),
            matches[EXT]);
    }

    mkdir(new_dir, 0755);

    char new_path[strlen(new_dir) + strlen(new_name) + 1];
    char old_path[strlen(DL_DIR) + strlen(name) + 1];

    strcat(strcpy(new_path, new_dir), new_name);
    strcat(strcpy(old_path, DL_DIR), name);

    printf("moving %s -> %s\n", old_path, new_path);
    copy_file(old_path, new_path);
}

int strtonum(char* match)
{
    char* end;
    long lnum = strtol(match, &end, 10);
    if (lnum > INT_MAX || lnum < INT_MIN) {
        fprintf(stderr, "should have used rust\n");
        exit(EXIT_FAILURE);
    }

    return (int)lnum;
}

void copy_file(char* src, char* dst)
{
    FILE* src_cp = fopen(src, "r");
    FILE* dst_cp = fopen(dst, "w");

    char buf_cp[50];

    while (fgets(buf_cp, 50, src_cp) != NULL) {
        fputs(buf_cp, dst_cp);
    };

    fclose(src_cp);
    fclose(dst_cp);
}
