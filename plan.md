# Plan: Building a TUI Core and Git Application in C

## Goal
Build a reusable TUI core library from scratch (no ncurses/notcurses) and use it to create a Git log explorer with notifications.

## Resources
These are the essential resources for building a TUI from scratch:

1.  **Build Your Own Text Editor (Kilo)** - [view source](http://antirez.com/news/108)
    *   *Why*: The gold standard tutorial for entering raw mode, processing input, and basic screen rendering in C.
2.  **VT100 User Guide** - [vt100.net](https://vt100.net/docs/vt100-ug/chapter3.html)
    *   *Why*: The definitive reference for ANSI escape codes (cursor movement, colors, clearing).
3.  **Xterm Control Sequences** - [invisible-island.net](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
    *   *Why*: Comprehensive list of all modern terminal sequences.
4.  **Termios Man Page** - `man termios`
    *   *Why*: Documentation for the struct used to switch terminal modes.

5.  **Text Editor Construction (Video Series)** - [Tsoding Daily](https://www.youtube.com/watch?v=ERV8lGikC7I)
    *   *Why*: Tsoding is a fantastic resource. Watch his "ded" (text editor) development streams. He writes C from scratch, dealing with raw memory and terminal escape codes live.
    *   *Focus*: Watch how he handles the "render loop" and "buffer management".

6.  **"Hacking the Terminal" (Blog)** - [Viam Blog on TUI](https://www.viam.com/post/hacking-the-terminal-building-a-tui-in-go) (Concepts apply to C)
    *   *Why*: Although in Go, this blog explains the *architecture* of a TUI (Model, View, Update loop) very clearly.

7.  **Termbox2 Source Code** - [github.com/termbox/termbox2](https://github.com/termbox/termbox2/blob/master/termbox.c)
    *   *Why*: `termbox` is a tiny, single-file header library. Reading `termbox.c` is the best way to see exactly how to implement `tui.c`. It's readable and less than 3k lines.
    *   *Look for*: `tb_present()` (the diffing strategy) and `tb_poll_event()` (input parsing).

8.  **Refactoring a TUI (Video)** - [Bisqwit's "Programming a DOS text editor"](https://www.youtube.com/watch?v=q73U213W0bU)
    *   *Why*: An old-school cool video showing bit-banging VGA text mode. While not modern terminal ANSI, the *concepts* of direct buffer manipulation and screen redraws are identical.

9.  **ANSI Escape Codes Fast Track** - [Notes on Programming](https://www.lihaoyi.com/post/BuildyourownCommandLinewithANSIescapecodes.html)
    *   *Why*: A visual guide to what `\x1b[31m` actually does, with interactive examples.


---

## Phase 1: The TUI Core Library
We will build a library (e.g., `libtui`) that handles the low-level terminal interactions.

### Step 1.1: Raw Mode & Lifecycle
*   **Goal**: Take control of the terminal input/output.
*   **Implementation**:
    *   Use `tcgetattr` to read current attributes.
    *   Disable `ICANON`, `ECHO`, `ISIG` (Ctrl-C/Z processing), and `IEXTEN`.
    *   Implement `atexit` handlers to restore the terminal state to "cooked" mode on exit (CRITICAL).
    *   Handle `SIGWINCH` to detect window resizing.

### Step 1.2: Input Processing
*   **Goal**: Convert raw byte streams into meaningful keys.
*   **Implementation**:
    *   Read from `STDIN` byte-by-byte.
    *   Detect escape sequences (starting with `\x1b`).
    *   Map sequences like `\x1b[A` to `KEY_UP`, `\x1b[C` to `KEY_RIGHT`, etc.
    *   Return a structured `Event` (Key or Resize).

### Step 1.3: The Renderer (Double Buffering)
*   **Goal**: Update the screen without flickering.
*   **Implementation**:
    *   **Buffers**: Create two 2D arrays of cells (Current & Next).
        *   `struct Cell { char c; Color fg; Color bg; }`
        *   `struct Buffer { int w, h; Cell *cells; }`
    *   **Diffing**: Compare Current vs Next. Only emit ANSI codes for cells that changed.
    *   **Optimization**: Move cursor only when necessary.
    *   **Render**: Write the diff buffer to `STDOUT` in one `write()` call.

### Step 1.4: Drawing Primitives
*   **Goal**: High-level drawing API.
*   **Implementation**:
    *   `tui_draw_char(x, y, char, color)`
    *   `tui_draw_str(x, y, string, color)`
    *   `tui_draw_rect(x, y, w, h, color)` -> useful for backgrounds/popups.

---

## Phase 2: The Application (Git TUI)
We will use our custom core to build the application.

### Step 2.1: Data Layer (Git Bindings)
*   **Goal**: Get data from git.
*   **Implementation**:
    *   Use `popen()` to run `git log --pretty=format:...`
    *   Parse the output (lines) into a struct `Commit`.
    *   Store in a dynamic array (Vector).

### Step 2.2: The Table Widget
*   **Goal**: Display commits in columns.
*   **Implementation**:
    *   Calculate column widths based on screen width.
    *   Loop through the visible range (scroll_offset to scroll_offset + height).
    *   Format strings into the Next Buffer.
    *   Handle truncation (e.g., commit messages that are too long).

### Step 2.3: Interaction & Notifications
*   **Goal**: User interaction.
*   **Implementation**:
    *   **Navigation**: Map `j`/`k` or arrows to change the `selected_index`.
    *   **Scroll**: If `selected_index` moves off-screen, adjust `scroll_offset`.
    *   **Notifications**:
        *   Implement a "Layer" or simply draw the notification *last* (on top of the table) in the buffer.
        *   Add a timer or key-press dismissal for the notification.

---

## Directory Structure Idea
```
/
├── core/
│   ├── tui.h        # Public API
│   ├── tui.c        # Raw mode, input, renderer
│   └── buffer.c     # Double buffering logic
├── app/
│   ├── main.c       # Application entry
│   ├── git.c        # Git command parsing
│   └── ui.c         # Layout & Drawing logic
└── Makefile
```
