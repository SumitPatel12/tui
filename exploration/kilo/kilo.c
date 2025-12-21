#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios original_attributes;

void disableRawMode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_attributes);
}

void enableRawMode() {

  // Get the attributes, and populate them in the raw variable.
  tcgetattr(STDIN_FILENO, &original_attributes);

  // Before exiting we need to reset to the original attributes.
  atexit(disableRawMode);

  struct termios new_attributes = original_attributes;

  // Modify the attributes we want set.
  // c_lflag is for the local flags.
  //
  // ECHO causes each key press to be printed to the terminal, it's good for
  // when you're in canonical mode, but in raw mode you don't want the user's
  // input echoed since you're gonig to be handling that.
  // ECHO is a bitflag, so we not it's value and logical &, essentially unset
  //
  // ICANON is for canonical mode. In that mode, inupt is made visible line by
  // line.
  //
  // it.
  new_attributes.c_lflag &= ~(ECHO | ICANON);

  // Set the attributes \['_']/
  // TCSAFLUSH specifies when to apply the change. In our case it'll wait for
  // all pending output to be written to the terminla, and discards any input
  // that hasn't been read.
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_attributes);
}

int main() {
  enableRawMode();

  char c;
  // STDIN_FILENO is an int with value 0. Typically
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
    // Check for control characters. ASCII 0-31 are control characters and are
    // not printable.
    if (iscntrl(c)) {
      printf("%d\n", c);
    } else {
      // Print decimal number and a character.
      printf("%d ('%c')\n", c, c);
    }
  }

  return 0;
}
