#include <curses.h>

extern _win_st *win;

void init();
void onF10();

int main() {
    win = initscr();
    cbreak();
    noecho();
    scrollok(win, true);
    init();
    onF10();
    endwin();
    return 0;
}
