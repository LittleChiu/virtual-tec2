#include <curses.h>

#include "../tec2.h"

extern _win_st *win;
extern Tec2 tec2;

void init();
void onF10();

int main() {
    win = initscr();
    cbreak();
    noecho();
    scrollok(win, true);
    init();
    tec2.MEM[0xffff] = 0xBEEF;
    tec2.MEM[0x0000] = 0xCAFE;
    tec2.MEM[0x0001] = 0x1234;
    onF10();
    endwin();
    return 0;
}
