#include "stdio.h"
#include "signal.h"
#include "stdlib.h"
#include "unistd.h"
#include "string.h"
#include "time.h"

FILE *log_fd;

#define die(_fmt, ...) { \
    fprintf(stderr, "die: "_fmt"\n", ##__VA_ARGS__ ); \
    exit(1); \
}

#define log(_fmt, ...) {\
    time_t t = time(NULL); \
    struct tm *lt = localtime(&t); \
    fprintf(log_fd, \
        "[%02d:%02d:%02d %02d/%02d/%4d] [%04d] [%s] "_fmt"\n",\
        lt->tm_hour, lt->tm_min, lt->tm_sec, \
        lt->tm_mon+1, lt->tm_mday, lt->tm_year+1900, \
        __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    fflush(log_fd); \
}

int
log_open(const char* a) {
    int ret = 0;
    char *fn =
        malloc(strlen(getenv("HOME") + strlen(a) + 3));

    strcpy(fn, getenv("HOME"));
    strcat(fn, "/.");
    strcat(fn, a);
    if ( (log_fd = fopen(fn, "w")) == NULL ) {
        ret = -1;
    }
    free(fn);
    return ret;
}

void
log_close(void) {
    fclose(log_fd);
}
