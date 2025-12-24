# Resilient TUI Development in C - Research Notes

## 1. Libraries for TUI Development

### 1.1 ncurses
**De-facto standard C TUI library.** Used by tig, mutt, htop.

**Features:**
- Window and panel abstractions (`WINDOW*`, panel library)
- Input handling (`getch`, keypad, mouse)
- Color pairs (16/256 colors)
- Extra libs: menus, forms, panels
- Uses terminfo/termios under the hood

**Pros:**
- Extremely portable (Linux/BSD/macOS, Windows via PDCurses)
- Stable API, available everywhere
- Handles terminal state, resize, key decoding, terminfo
- Excellent documentation: "NCURSES Programming HOWTO", man pages

**Cons:**
- API feels dated: global state, odd naming
- Color model uses "color pairs", not native RGB
- Limited built-in widgets
- Truecolor not first-class

### 1.2 notcurses
**Modern C17 "blingful" TUI library.** Targets modern terminals (kitty, alacritty, ghostty).

**Features:**
- Native 24-bit RGB color everywhere
- Planes (layered surfaces) with z-ordering and transparency
- Built-in widgets: menus, selectors, trees, progress bars
- Advanced terminal capability detection

**Pros:**
- Best match for true-color ghostty target
- More GUI-like design with planes and widgets
- Strong capability detection
- Good performance and partial rendering

**Cons:**
- Extra dependencies (libunistring, terminfo, optional FFmpeg)
- Not a curses drop-in
- Overkill for simple text tables

### 1.3 termbox / termbox2
**Minimal cell-based TUI library.** Terminal as a 2D array of cells.

**Features:**
- Very small API (~12 functions): init/shutdown, width/height, set cell, clear, present, poll events

**Pros:**
- Tiny API, easy to understand
- Full control over layout/rendering
- MIT license, embedded-friendly

**Cons:**
- Less portable than ncurses
- Limited higher-level widgets
- Truecolor support varies by fork

### 1.4 Other Options
- **CDK (Curses Development Kit):** Widget library on top of ncurses
- **S-Lang / newt:** Older alternatives, niche use

---

## 2. Building TUIs Without Libraries

### 2.1 Terminal Mode via termios

```c
#include <termios.h>
#include <unistd.h>

static struct termios orig;

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig);
    struct termios raw = orig;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN]  = 0;  // non-blocking
    raw.c_cc[VTIME] = 1;  // 0.1s timeout
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void restore_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
}
```

### 2.2 Essential ANSI/VT Escape Sequences

| Action | Sequence |
|--------|----------|
| Clear screen | `\x1b[2J` |
| Home cursor | `\x1b[H` |
| Move cursor | `\x1b[<row>;<col>H` |
| Hide cursor | `\x1b[?25l` |
| Show cursor | `\x1b[?25h` |
| Enter alt buffer | `\x1b[?1049h` |
| Exit alt buffer | `\x1b[?1049l` |

### 2.3 Input Decoding
In raw mode, decode bytes into keys:
- Single chars: ASCII, UTF-8 multi-byte
- Escape sequences: Arrow up = `\x1b[A`, etc.
- Requires a state machine for CSI sequence parsing

### 2.4 Double Buffering

```c
typedef struct {
    uint32_t codepoint;
    uint32_t fg_rgb;
    uint32_t bg_rgb;
    uint8_t  attr;
} Cell;

typedef struct {
    int rows, cols;
    Cell *cells;
} Screen;
```

**Recommendation:** Building robust TUI from raw termios+ANSI is doable but large (weeks of effort). Use ncurses or notcurses unless you have specific needs.

---

## 3. Common Pitfalls and Errors

### 3.1 Terminal State Restoration
**Problem:** On exit/crash, terminal stays in raw mode, cursor hidden.

**Solution:**
```c
void cleanup(void) {
    // restore termios
    // show cursor
    // exit alternate screen
}

int main(void) {
    setup();
    atexit(cleanup);
    // main loop
}
```

### 3.2 Signal Handling

**SIGWINCH (resize):**
```c
static volatile sig_atomic_t g_resized = 0;
void sigwinch_handler(int signo) { g_resized = 1; }
// In main loop: if (g_resized) { recompute_layout(); g_resized = 0; }
```

**SIGINT/SIGTERM:**
```c
static volatile sig_atomic_t g_should_exit = 0;
void sigint_handler(int signo) { g_should_exit = 1; }
```

**SIGTSTP/SIGCONT (Ctrl-Z):** Restore terminal mode before suspending.

### 3.3 Buffer Management
- Don't mix `printf` with direct `write()` - buffering issues
- Either disable stdout buffering or use consistent I/O

### 3.4 Blocking I/O
- Use `select`/`poll`/`epoll` to multiplex stdin and other FDs
- Or use `nodelay(stdscr, TRUE)` in ncurses

### 3.5 Resize/Layout Bugs
- Guard against `rows < some_min`
- Remember ncurses uses `(y, x)` not `(x, y)`

---

## 4. Best Practices & Layout Designs

### 4.1 Core Architecture

**Terminal Backend Abstraction:**
```c
typedef struct TerminalOps {
    void (*init)(void);
    void (*shutdown)(void);
    void (*get_size)(int *rows, int *cols);
    void (*clear)(void);
    void (*draw_str)(int row, int col, const char *s, Color fg, Color bg, Attr attr);
    void (*flush)(void);
    int  (*poll_event)(Event *ev, int timeout_ms);
} TerminalOps;
```

**Event Loop Pattern:**
```c
while (!quit) {
    Event ev;
    int got = term.poll_event(&ev, 50);
    if (got) handle_event(&ev);
    if (g_resized) { recompute_layout(); g_resized = 0; }
    if (model_dirty) render(&model, &layout);
}
```

### 4.2 Layout System

```c
typedef struct Rect { int y, x, h, w; } Rect;

typedef struct Layout {
    Rect main_table;
    Rect commit_detail;
    Rect status_bar;
} Layout;
```

### 4.3 Essential Widgets (tig/lazygit patterns)

1. **Table/List Widget:** For git log - array of commits, column defs, selection, scroll
2. **Text/Detail View:** Full commit message, scrollable
3. **Status Bar:** Branch, HEAD, position, mode hints
4. **Popup/Prompt:** For commands, search, confirmations

### 4.4 Model/View/Controller Separation
- **Model:** commits list, selection index, scroll offsets, modes
- **View:** draws into terminal given model and layout
- **Controller:** maps input events to model actions

---

## 5. Vim Motions Implementation

### 5.1 Available Libraries
No popular lightweight "vim motions" C library exists. The common approach: **implement motions yourself**.

Reference implementations: tig, lazygit, htop use vim-inspired bindings internally.

### 5.2 Modal Input Design

**Modes:**
```c
typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND
} Mode;

typedef struct {
    Mode mode;
    int  pending_count;  // e.g., "3" in "3j"
    char pending_op;     // 'd', 'y', 'c', or 0
    int  last_motion;    // for "." repetition
} VimState;
```

**Key Pipeline:**
1. Terminal decodes keys into `KeyEvent`
2. Input handler checks mode:
   - **INSERT:** send keys to active widget
   - **NORMAL:** process motions/operators

**Motion Implementation:**
```c
typedef struct { int row; int col; } Cursor;

Cursor motion_down(Cursor cur, int count, int max_row) {
    cur.row += count;
    if (cur.row > max_row) cur.row = max_row;
    return cur;
}
```

### 5.3 Essential Vim Keys for TUI Navigation

| Key | Action |
|-----|--------|
| `h,j,k,l` | Left/Down/Up/Right |
| `{count}j/k` | Move count lines |
| `gg` | Go to top |
| `G` | Go to bottom |
| `Ctrl-f/b` | Page down/up |
| `/` | Search |
| `n/N` | Next/prev search |
| `q` | Quit |

### 5.4 Multi-key Sequences
For `gg` vs single `g`:
- Use a keymap/trie structure
- Short timeout: if `g` pressed and next isn't `g`, treat as unknown

---

## 6. Git Output Formatting & Tabular Display

### 6.1 Getting Structured Data from Git

**Use custom format with NUL separators:**
```sh
git log --date=short \
  --pretty=format:'%H%x00%h%x00%ad%x00%an%x00%s%x00%D%x00' \
  --no-color
```

Fields:
- `%H` full hash
- `%h` short hash
- `%ad` author date
- `%an` author name
- `%s` subject
- `%D` ref names
- `%x00` is NUL separator

**From C:**
```c
FILE *fp = popen("git log ...", "r");
// read until EOF, split on '\0'
```

### 6.2 Data Structures

```c
typedef struct {
    char *hash_full;
    char *hash_short;
    char *date;
    char *author;
    char *subject;
    char *refs;
    bool  is_head;
} Commit;

typedef struct {
    Commit **rows;
    size_t   count;
} CommitList;
```

### 6.3 Column Layout

```c
typedef enum { COL_HASH, COL_DATE, COL_AUTHOR, COL_SUBJECT, COL_REFS } ColumnId;

typedef struct {
    ColumnId id;
    int      min_width;
    int      max_width;  // 0 for flex
} ColumnDef;
```

**Example layout:**
- HASH: 8 chars
- DATE: 10 chars
- AUTHOR: 16 chars (truncate)
- SUBJECT: flex
- REFS: 12 chars (truncate)

### 6.4 How TUI Core Helps
Your TUI framework provides:
- Consistent coordinate system
- Color management
- Event handling for navigation
- Widget abstractions for rendering tables

---

## 7. Color Support for Ghostty

### 7.1 ANSI SGR Color Codes

**Standard 16 colors:**
```c
// Foreground: 30-37 (normal), 90-97 (bright)
// Background: 40-47 (normal), 100-107 (bright)
printf("\x1b[1;31mError\x1b[0m\n"); // bold red
```

**256-color mode:**
```c
// Foreground: \x1b[38;5;<n>m
// Background: \x1b[48;5;<n>m
printf("\x1b[38;5;208mOrange\x1b[0m\n");
```

**Truecolor (24-bit) - Best for Ghostty:**
```c
void set_fg_rgb(int r, int g, int b) {
    printf("\x1b[38;2;%d;%d;%dm", r, g, b);
}

void set_bg_rgb(int r, int g, int b) {
    printf("\x1b[48;2;%d;%d;%dm", r, g, b);
}

void reset_colors(void) {
    printf("\x1b[0m");
}
```

### 7.2 Color Capability Detection

```c
// Check COLORTERM for "truecolor" or "24bit"
// Or check TERM for "-truecolor" or "-direct" suffix
// Or use tput colors >= 256

typedef enum { COLOR_MODE_16, COLOR_MODE_256, COLOR_MODE_TRUE } ColorMode;

typedef struct {
    ColorMode mode;
} ColorConfig;
```

### 7.3 Library Color Support

**ncurses:**
- Uses color pairs: `init_pair(id, fg, bg)`
- Truecolor requires raw escape codes workaround

**notcurses:**
- All APIs use RGB directly
- Auto-quantizes for lesser terminals
- Ideal for ghostty truecolor

---

## 8. Recommended Implementation Path

### Step 1: Choose Terminal Backend (0.5 day)
- **ncurses:** Maximum compatibility, copy tig patterns
- **notcurses:** True-color, modern feel for ghostty

Wrap in your own `TerminalOps` abstraction.

### Step 2: Event Loop + Layout + Basic Widgets (1-2 days)
- Event loop with key input, resize handling
- Layout structure for table, detail, status bar
- Table widget with up/down navigation

### Step 3: Integrate Git Log (1-2 days)
- `popen("git log ...")` with structured format
- Parse into `CommitList`
- Display in table widget

### Step 4: Add Vim-style Navigation (1-2 days)
- Implement `Mode` and `VimState`
- Support: `hjkl`, `{count}j/k`, `gg`/`G`, `/` search

### Step 5: Colors and Polish (< 1 day)
- Color detection
- Theme mapping (hash=dim, HEAD=bold, branches=colored)
- Test in ghostty and fallback terminals

---

## 9. Architecture Guardrails

**Avoid:**
- Going raw ANSI/termios too early → weeks of debugging
- Over-ambitious vim emulation → complexity explosion
- Over-engineered widget set → never finish

**Maintain:**
- Strict separation: terminal backend / model logic / input-motion / rendering
- Shell out to git rather than libgit2 initially
- Implement only the vim motions you'll actually use

---

## 10. References

- **ncurses:** https://invisible-island.net/ncurses/
- **notcurses:** https://github.com/dankamongmen/notcurses
- **termbox2:** https://github.com/termbox/termbox2
- **tig:** https://github.com/jonas/tig (git TUI reference)
- **NCURSES HOWTO:** https://tldp.org/HOWTO/NCURSES-Programming-HOWTO/
- **XTerm Control Sequences:** https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
