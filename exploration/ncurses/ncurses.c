#define _XOPEN_SOURCE_EXTENDED 1

#include <locale.h>
#include <ncurses.h>
#include <curses.h>

int main(int argc, char *argv[]) {
   if (!setlocale(LC_ALL, "") || !initscr() || curs_set(0) == ERR)
      return 1;

   // Replacing a full-width character with a half-width one breaks ncurses
   for (int i = 0; i < 5; i++) {
      mvaddwstr(    i,  0, L"======");
      mvaddwstr(    i, 10, L"======");
      mvaddwstr(    i, 10 + i, L"\\");

      mvaddwstr(6 + i,  0, L"ーー==");
      mvaddwstr(6 + i, 10, L"ーー==");
      // However this fixes it: wnoutrefresh (stdscr);
      mvaddwstr(6 + i, 10 + i, L"\\");
   }

   // The first vertical line ends up misaligned, the second one is fine;
   // interestingly, just calling wnoutrefresh() wouldn't be enough
   for (int i = 0; i < 11; i++) mvaddwstr(i, 20, L"|");
   refresh();
   for (int i = 0; i < 11; i++) mvaddwstr(i, 25, L"X");
   refresh(); getch(); endwin(); return 0;
}
