#include <cstdlib>
#include <cstring>
#include <curses.h>

#include "../tec2.h"

extern _win_st *win;
extern Tec2 tec2;

void init();
void onF10();

static int parseHex(const char *text) {
    char *end = nullptr;
    long value = std::strtol(text, &end, 16);
    if (end == text || *end != '\0') {
        return -1;
    }
    return static_cast<int>(value) & 0xffff;
}

int main(int argc, char **argv) {
    win = initscr();
    cbreak();
    noecho();
    scrollok(win, true);
    init();

    for (int i = 1; i + 1 < argc; i += 2) {
        int address = parseHex(argv[i]);
        int value = parseHex(argv[i + 1]);
        if (address < 0 || value < 0) {
            endwin();
            return 2;
        }
        tec2.MEM[address] = value;
    }

    onF10();
    endwin();
    return 0;
}
