#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** Data ***/
struct termios original_attributes;

/*** Terminal Attributes and Configuration ***/
/**
 * die: Prints out an error and exits the process.
 * @s: String error to be outputted, with the perror.
 */
void die(const char *s) {
  perror(s);
  exit(1);
}

/**
 * disableRawMode: Restores terminal to original attributes.
 *
 * Resets the terminal attributes back to what they were before enableRawMode
 * was called. This is necessary because we modify many flags, and without
 * resetting, the terminal would remain in the state we set instead of what
 * the user had configured.
 */
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_attributes) == -1) {
    die("tcsetattr");
  }
}

/**
 * enableRawMode: Configures the terminal for raw mode operation.
 *
 * Saves the current terminal attributes and then modifies them to enable raw
 * mode by disabling echo, canonical mode, signals, and various input/output
 * processing features. Registers disableRawMode to be called at exit to
 * restore original terminal settings.
 */
void enableRawMode() {
  // Get the attributes, and populate them in the raw variable.
  if (tcgetattr(STDIN_FILENO, &original_attributes) == -1) {
    die("tcgetattr");
  }

  // Before exiting we need to reset to the original attributes.
  atexit(disableRawMode);

  struct termios new_attributes = original_attributes;

  // IXON: Disables Ctrl-S and Ctrl-Q. Ctrl-S stops data from being transmitted
  // to the terminal until you press Ctrl-Q
  //
  // ICRNL: Disables Ctrl-M. Ctrl-M should return 13, but it returns 10. The
  // terminal translates ny carriage returns (13, '\r') inputted by the user
  // into newlines (10, '\n').
  //
  // IBRKINT: Break conditons cause SIGINT
  //
  // INPCK: Enables parity checking, mostly doesn't apply to modern computers.
  //
  // ISTRIP: causes the 8th bit of each input byte to be stripped (setting it to
  // 0). It's probably turned off for modern terminals.
  new_attributes.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

  // Turns off all output processing. Output processing changes all "\n" to
  // "\r\n".
  // "\n" moves the cursor to the next line, and \r moves it back to the start
  // of that line.
  // For example: If the cursor was at position represented by (row, col): (5,
  // 10).
  // \n will make the new position (6, 10).
  // \r will make the new position (6, 0).
  new_attributes.c_oflag &= ~(OPOST);

  // Tell the terminal that character szie (CS) is 8 bits per byte.
  new_attributes.c_cflag |= (CS8);

  // Modify the attributes we want set.
  // c_lflag is for the local flags.
  //
  // ECHO: causes each key press to be printed to the terminal, it's good for
  // when you're in canonical mode, but in raw mode you don't want the user's
  // input echoed since you're gonig to be handling that.
  //
  // ICANON is for canonical mode. In that mode, inupt is made visible line by
  // line.
  //
  // ISIG: When any of the characters INTR, QUIT, SUSP, or DSUSP are received,
  // generate the corresponding signal.
  // Unsetting ISIG stops the signals Ctrl-C and Ctrl-Z
  //
  // IEXTEN: Turns off Ctrl-V, and Ctrl-O
  new_attributes.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

  // VMIN: Minimum number of characters for noncanonical read (VMIN).
  // 0 means we return as soon as any input to be read.
  new_attributes.c_cc[VMIN] = 0;

  // VTIME: Timeout in deciseconds for noncanonical read (TIME).
  // Returns 0 after 0.1 x n seconds n begin the value provided.
  new_attributes.c_cc[VTIME] = 1;

  // Set the attributes \['_']/
  // TCSAFLUSH specifies when to apply the change. In our case it'll wait for
  // all pending output to be written to the terminla, and discards any input
  // that hasn't been read.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_attributes) == -1) {
    die("tcsetattr");
  }
}

/*** Init ***/
int main() {
  enableRawMode();

  char c;
  // STDIN_FILENO is an int with value 0. Typically

  while (1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1)) {
      die("read");
    }

    // Check for control characters. ASCII 0-31 are control characters and are
    // not printable.
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      // Print decimal number and a character.
      printf("%d ('%c')\r\n", c, c);
    }

    if (c == 'q')
      break;
  };

  return 0;
}
