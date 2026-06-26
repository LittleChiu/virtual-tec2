#ifndef TEC2_HEADLESS_CURSES_H
#define TEC2_HEADLESS_CURSES_H

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct _win_st {};

#ifndef KEY_F
#define KEY_F(n) (0x1000 + (n))
#endif

static inline _win_st *initscr() {
    static _win_st win;
    return &win;
}

static inline int cbreak() { return 0; }
static inline int nocbreak() { return 0; }
static inline int noecho() { return 0; }
static inline int echo() { return 0; }
static inline int scrollok(_win_st *, bool) { return 0; }
static inline int endwin() { return 0; }
static inline int delch() { return 0; }

static inline int addch(int ch) {
    return std::putchar(ch);
}

static inline int printw(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = std::vprintf(fmt, args);
    va_end(args);
    std::fflush(stdout);
    return ret;
}

static inline int getch() {
    int ch = std::getchar();
    if (ch == EOF) {
        // The original monitor is interactive and does not handle EOF. Feeding
        // newline lets scripted comparisons terminate the current input field.
        return '\n';
    }
    return ch;
}

static inline int getstr(char *str) {
    if (std::fgets(str, 1000, stdin) == nullptr) {
        str[0] = '\0';
        return EOF;
    }
    size_t len = std::strlen(str);
    if (len > 0 && str[len - 1] == '\n') {
        str[--len] = '\0';
    }
    if (len > 0 && str[len - 1] == '\r') {
        str[--len] = '\0';
    }
    std::printf("%s\n", str);
    std::fflush(stdout);
    return 0;
}

#endif
