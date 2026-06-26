#include <cstring>
#include <cstdlib>
#include <curses.h>

extern _win_st *win;
extern char buffer[1000];

void init();
void onF10();
void procStr(char *str);
void cmdD();

int main(int argc, char **argv) {
    win = initscr();
    cbreak();
    noecho();
    scrollok(win, true);
    init();
    onF10();
    int dumpAddress = 0x0800;
    if (argc > 1) {
        dumpAddress = static_cast<int>(std::strtol(argv[1], nullptr, 16)) & 0xffff;
    }
    std::snprintf(buffer, sizeof(buffer), "D%04X", dumpAddress);
    procStr(buffer);
    cmdD();
    endwin();
    return 0;
}
