#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

/**
 * Ctrl key strips bits 5 and 6 from whatever key you press in combination with
 * it. We are duplicating that behaviour here by setting the top 3 bits to 0.
 */
#define CTRL_KEY(k) ((k) & 0x1F)

#define KILO_VERSION "0.0.1"

typedef struct erow {
  int size;
  char *contents;
} erow;

/*** Append Buffer ***/
struct abuf {
  char *buf;
  int len;
};

// We're going vim mode boiz. Maybe not the FULL THING, but at least some
// semblance of it.
typedef enum { NORMAL, INSERT, VISUAL } mode;

// We're blaspheming as well. WERE ALIASING hjkl as arrow KEYS
// :evil_laugh_if_thats_even_an_emote:
enum editor_key {
  ARROW_LEFT = 'h',
  ARROW_RIGHT = 'l',
  ARROW_UP = 'k',
  ARROW_DOWN = 'j',
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->buf, ab->len + len);

  if (new == NULL) {
    return;
  }
  memcpy(&new[ab->len], s, len);
  ab->buf = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->buf); }

/*** Data ***/
struct editorConfig {
  int cur_row;
  int cur_col;
  int row_offset;
  int screen_rows;
  int screen_cols;
  struct termios original_termios;
  mode mode;
  int num_rows;
  int row_capacity;
  erow *rows;
};

struct editorConfig E;

/*** utility ***/
void hideCursor(struct abuf *ab) { abAppend(ab, "\x1b[?25l", 6); }

void showCursor(struct abuf *ab) { abAppend(ab, "\x1b[?25h", 6); }

void resetCrusorPosition(struct abuf *ab) {
  // Reposition curosr to (1,1) (row, col).
  // You can write it as \x1b[row;colH, to set it to (row, col) but the defaults
  // are 1 so we don't need to specify them.
  abAppend(ab, "\x1b[H", 3);
}

void moveCursor(int rows, int cols) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", rows, cols);
  write(STDOUT_FILENO, buf, strlen(buf));
}

void moveCursorToCurrentPos() { moveCursor((E.cur_row - E.row_offset) + 1, E.cur_col + 1); }

/**
 * clearScreen - clears the screen
 * \x1b[wJ: Is an escape sequence.
 *    \x1b is the escape character (27), it's followed by '['.
 *    2J tells the terminal to clear the whole screen.
 *    1J would mean clear from top till the cursor.
 *    0J would mean clear from cursor to bottom.
 *    0, 1, and 2 are arguments.
 */
void clearScreen(struct abuf *ab) {
  // Clear whole screen
  abAppend(ab, "\x1b[2J", 4);
  resetCrusorPosition(ab);
}

/*** output ***/
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screen_rows; ++y) {
    int file_row = y + E.row_offset;
    if (file_row < E.num_rows) {
      int len = E.rows[file_row].size;
      if (len > E.screen_cols) {
        len = E.screen_cols;
      }
      abAppend(ab, E.rows[file_row].contents, len);
    } else {
      abAppend(ab, "~", 1);
    }
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screen_rows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

int editorScroll() {
  int scrolled = 0;
  if (E.cur_row < E.row_offset) {
    E.row_offset = E.cur_row;
    scrolled = 1;
  }
  if (E.cur_row >= E.row_offset + E.screen_rows) {
    E.row_offset = E.cur_row - E.screen_rows + 1;
    scrolled = 1;
  }
  return scrolled;
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  hideCursor(&ab);
  resetCrusorPosition(&ab);
  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cur_row - E.row_offset) + 1, E.cur_col + 1);
  abAppend(&ab, buf, strlen(buf));

  showCursor(&ab);

  write(STDOUT_FILENO, ab.buf, ab.len);
  abFree(&ab);
}

/*** Terminal Attributes and Configuration ***/
void editorFree() {
  if (E.rows != NULL) {
    for (int i = 0; i < E.num_rows; i++) {
      free(E.rows[i].contents);
    }
    free(E.rows);
  }
}

/**
 * die: Prints out an error and exits the process.
 * @s: String error to be outputted, with the perror.
 */
void die(const char *s) {
  struct abuf ab = ABUF_INIT;
  clearScreen(&ab);
  abFree(&ab);

  editorFree();
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
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1) {
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
  if (tcgetattr(STDIN_FILENO, &E.original_termios) == -1) {
    die("tcgetattr");
  }
  // Before exiting we need to reset to the original attributes.
  atexit(disableRawMode);

  struct termios new_attributes = E.original_termios;

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

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      printf("didn't read the 1st byte");
      return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      printf("didn't read the 2nd byte");
      return '\x1b';
    }
    if (seq[0] == '[') {
      printf("%c, %c", seq[1], seq[2]);
      switch (seq[1]) {
      case 'A':
        return ARROW_UP;
      case 'B':
        return ARROW_DOWN;
      case 'C':
        return ARROW_RIGHT;
      case 'D':
        return ARROW_LEFT;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
  return c;
}

void editorProcessKeypress() {
  char c = editorReadKey();

  if (E.mode == NORMAL) {
    switch (c) {
    case ARROW_DOWN:
      if (E.cur_row < E.num_rows - 1)
        E.cur_row++;
      if (editorScroll()) {
        editorRefreshScreen();
      } else {
        moveCursorToCurrentPos();
      }
      break;
    case ARROW_UP:
      if (E.cur_row > 0)
        E.cur_row--;
      if (editorScroll()) {
        editorRefreshScreen();
      } else {
        moveCursorToCurrentPos();
      }
      break;
    case ARROW_RIGHT:
      if (E.cur_col < E.screen_cols - 1)
        E.cur_col++;
      moveCursorToCurrentPos();
      break;
    case ARROW_LEFT:
      if (E.cur_col > 0)
        E.cur_col--;
      moveCursorToCurrentPos();
      break;
    case 'i':
      E.mode = INSERT;
      break;
    case CTRL_KEY('q'):
      exit(0);
      break;
    case CTRL_KEY('r'):
      editorRefreshScreen();
      break;
    default:
      break;
    }
  }

  if (E.mode == INSERT) {
    switch (c) {
    case '\x1b':
      E.mode = NORMAL;
      break;
    case CTRL_KEY('q'):
      exit(0);
      break;
    case CTRL_KEY('r'):
      editorRefreshScreen();
      break;
    default:
      printf("%d ('%c')\r\n", c, c);
    }
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
    ++i;
  }

  if (buf[0] != '\x1b' || buf[1] != '[') {
    return -1;
  }

  // We're tellig sscanf that the buffer will be of type: int followed by a
  // semicolon followed by an int. And we want the two int values assigned to
  // rows and cols respectively.
  if (sscanf(&buf[2], "%d;%d", rows, cols)) {
    return -1;
  }

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
      return -1;
    }
    getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
  return -1;
};

/*** Row Operations ***/
void editorAppendRow(char *s, size_t len) {
  if (E.num_rows >= E.row_capacity) {
    int new_capacity = E.row_capacity == 0 ? 16 : E.row_capacity * 2;
    E.rows = realloc(E.rows, sizeof(erow) * new_capacity);
    if (E.rows == NULL) {
      die("realloc rows");
    }
    E.row_capacity = new_capacity;
  }

  int at = E.num_rows;
  E.rows[at].size = len;
  E.rows[at].contents = malloc(len + 1);
  if (E.rows[at].contents == NULL) {
    die("malloc row contents");
  }
  memcpy(E.rows[at].contents, s, len);
  E.rows[at].contents[len] = '\0';
  E.num_rows++;
}

/*** Init ***/
void initEditor() {
  E.cur_row = 0;
  E.cur_col = 0;
  E.row_offset = 0;
  E.mode = NORMAL;
  E.num_rows = 0;
  E.row_capacity = 0;
  E.rows = NULL;
  if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1) {
    die("getWindowSize");
  }
  atexit(editorFree);
}

void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    die("fopen");
  }

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
    }
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: kilo <filename>\n");
    exit(1);
  }

  enableRawMode();
  initEditor();
  editorOpen(argv[1]);

  editorRefreshScreen();

  while (1) {
    editorProcessKeypress();
  };

  return 0;
}
