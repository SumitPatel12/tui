Building a TUI Core in C from Scratch
Overview
Build a small "terminal core" around termios + ANSI escape sequences, a retained off-screen buffer, and a simple event loop (select/poll) that feeds an input state machine (including Vim modes) and a tree of widgets. Start by targeting xterm-compatible terminals (80–90% of real usage) and add compatibility/features incrementally.

Estimated Effort: XL (>2 weeks of focused work)

1. Terminal Fundamentals
1.1 How Terminals Work (TTY, PTY, Emulators)
TTY (teletype): A character device that line-disciplined input/output (historically physical; now virtual).
PTY (pseudo-terminal): A pair of devices: master and slave.
Your TUI typically runs attached to a TTY that is actually the slave end of a PTY managed by a terminal emulator (xterm, iTerm2, etc.).
Terminal emulator:
Reads bytes from program's stdout/stderr.
Interprets control sequences & prints glyphs.
Encodes user key presses / mouse events into bytes and writes them to stdin.
Key consequence: Your program talks only via stdin/stdout/stderr. All "graphics" and input semantics are defined by character streams and terminal conventions.

1.2 ANSI / ECMA-48 Escape Sequences (Categories)
Primary reference: ECMA-48 ("Control Functions for Coded Character Sets") and XTerm Control Sequences.

Broad categories:

C0 control characters: Single bytes < 0x20 (e.g., BEL, BS, HT, LF).
ESC (0x1B) sequences:
CSI: ESC [ ... (Control Sequence Introducer)
Examples:
Cursor movement: ESC[<row>;<col>H, ESC[<n>A/B/C/D
Erase: ESC[J, ESC[K
SGR (Select Graphic Rendition – colors/styles): ESC[31m, ESC[1;4;32m
OSC: ESC ] ... BEL (Operating System Command) – mostly for title setting, clipboard.
DCS, SOS, etc. – advanced, rarely needed for basic TUI.
Private / xterm extensions:
Mouse tracking: ESC[?1000h, ESC[?1002h, ESC[?1006h, etc.
Alternate screen: ESC[?1049h / ESC[?1049l.
For a core TUI, you mainly need CSI cursor positioning, clearing, SGR attributes, and some private modes (alternate screen, mouse).

1.3 Terminal Capabilities and Feature Detection
By default, assume xterm-compatible behavior (good baseline).

Environment:
TERM env var: e.g., xterm-256color, linux, screen-256color.
Simple approach (recommended to start):
Assume at least ANSI + 8 colors; optionally 256 colors for *256color.
Provide a config flag for "dumb" terminals (no colors, minimal cursor control).
More advanced:
Query terminal: Device Attributes ESC[c and parse response.
Respond to DA2, DECRQSS, etc. (but this gets complex).
Optionally parse terminfo database files manually (not via curses APIs).
1.4 Raw Mode vs Cooked Mode
Use POSIX termios to switch stdin into raw mode.

Cooked (canonical) mode:
Kernel performs line editing, echo, signals on Ctrl-C, etc.
Raw mode:
Input delivered byte-by-byte, no echo, no signal chars.
Required for precise key/escape handling and Vim-style modes.
Code to enter raw mode:

#include <termios.h>
#include <unistd.h>
#include <stdbool.h>

static struct termios g_orig_termios;
static bool g_in_raw_mode = false;

int term_enter_raw_mode(void) {
    if (g_in_raw_mode) return 0;

    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) < 0) return -1;
    raw = g_orig_termios;

    // cfmakeraw is convenient but non-standard; implement manually if needed
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;  // non-blocking read with timeout
    raw.c_cc[VTIME] = 1;  // tenths of seconds

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return -1;
    g_in_raw_mode = true;
    return 0;
}

void term_restore_mode(void) {
    if (!g_in_raw_mode) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    g_in_raw_mode = false;
}
2. Core Architecture
2.1 Module Organization (Suggested)
termio.c/h      – raw mode, terminal writing helpers, resize detection
screen.c/h      – off-screen buffer, diffing, rendering
input.c/h       – raw input, key/escape parsing, mouse, UTF-8 decoding
event_loop.c/h  – select/poll-driven loop, timers, dispatch
widget.c/h      – widget base type, composition and layout
vim_mode.c/h    – Vim modal state machine and key bindings
notify.c/h      – notification/toast management and painting
app.c/h         – high level application/state
2.2 Event Loop Design
Use select() or poll() on:

STDIN_FILENO for input.
Optional timer mechanism:
Simple: track next wake-up deadline in userland; set select() timeout.
More robust (Linux): timerfd_create() and monitor the fd.
Pseudo-structure:

typedef struct {
    int quit;
    // timers, scheduled callbacks, etc.
} EventLoop;

void event_loop_run(EventLoop *loop) {
    while (!loop->quit) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int maxfd = STDIN_FILENO;

        struct timeval timeout = compute_next_timeout(loop);

        int n = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            // handle fatal error
        }

        if (n > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            input_on_readable();
        }

        timers_on_tick(loop);       // handle expired timers
        app_on_idle();              // background tasks if needed

        if (screen_needs_redraw()) {
            screen_render_diff();
        }
    }
}
2.3 Buffer Management / Double Buffering
Maintain a logical screen buffer separate from the terminal.

Data structure for a cell:

typedef struct {
    uint32_t ch;       // Unicode codepoint (U+0000..U+10FFFF)
    uint8_t  fg;       // color index (0-255)
    uint8_t  bg;       // color index (0-255)
    uint16_t attrs;    // bitfield: bold, underline, reverse, etc.
    uint8_t  width;    // 0,1,2 (0 for continuation of wide char)
} Cell;

typedef struct {
    int width;
    int height;
    Cell *cells;       // size = width * height
} ScreenBuffer;
Use:

ScreenBuffer back_buffer; – what you want on screen.
ScreenBuffer front_buffer; – what you believe the terminal currently shows.
Rendering step:

Compare back_buffer vs front_buffer.
Emit minimal ANSI sequences to transform terminal from front to back.
Update front_buffer to match.
2.4 Screen Abstraction Layer
API surface:

void screen_init(int width, int height);
void screen_resize(int new_width, int new_height);
void screen_clear(void);
void screen_put(int x, int y, uint32_t ch, uint8_t fg, uint8_t bg, uint16_t attrs);
void screen_draw_string(int x, int y, const char *utf8, uint8_t fg, uint8_t bg, uint16_t attrs);
void screen_invalidate_rect(int x, int y, int w, int h);  // mark dirty
void screen_render_diff(void);  // write to terminal
Widgets draw into back_buffer only. The terminal layer (termio) is only used by screen_render_diff().

3. Input Handling
3.1 Reading Raw Input
Use non-blocking read() on stdin within the event loop.

Basic pattern:

#define INPUT_BUF_SIZE 4096

static unsigned char input_buf[INPUT_BUF_SIZE];
static size_t input_buf_len = 0;

void input_on_readable(void) {
    ssize_t n = read(STDIN_FILENO, input_buf + input_buf_len,
                     INPUT_BUF_SIZE - input_buf_len);
    if (n <= 0) return;
    input_buf_len += (size_t)n;

    // process as many complete events as possible
    size_t consumed = input_process_bytes(input_buf, input_buf_len);
    if (consumed > 0 && consumed <= input_buf_len) {
        memmove(input_buf, input_buf + consumed, input_buf_len - consumed);
        input_buf_len -= consumed;
    }
}
3.2 Parsing Escape Sequences (Keyboard)
You need a state machine for interpreting:

Single-byte ASCII.
UTF-8 multibyte sequences.
ESC-based sequences for special keys.
Common key encodings (xterm-ish):

Key	Sequence
Arrow Up	ESC [ A
F1	ESC O P or ESC [ 11 ~
Home	ESC [ H
End	ESC [ F
PageUp	ESC [ 5 ~
PageDown	ESC [ 6 ~
Design:

typedef enum {
    KEY_CHAR,        // normal UTF-8 character
    KEY_ESC,         // bare ESC (as a key)
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN,
    KEY_F1, KEY_F2, // ...
    // vim-style composed later
} KeyCode;

typedef struct {
    KeyCode code;
    uint32_t ch;     // valid when code == KEY_CHAR
    uint32_t mods;   // bitmask: CTRL, ALT, SHIFT
} KeyEvent;
Important: You may want a timeout (~50–100ms) to distinguish:

Bare ESC (user pressed Escape) vs
Start of an escape sequence where some bytes haven't yet arrived.
3.3 UTF-8 Decoding
Implement a small decoder that converts bytes to Unicode codepoints; reject invalid sequences gracefully.

typedef struct {
    uint32_t codepoint;
    int expected;   // total bytes expected
    int seen;       // bytes seen so far
} Utf8Decoder;

void utf8_decoder_reset(Utf8Decoder *d);
int utf8_decode_step(Utf8Decoder *d, unsigned char byte, uint32_t *out_cp);
// returns 1 if completed a codepoint (out_cp valid), 0 if waiting more, -1 on error
3.4 Mouse Input Support
Enable mouse tracking (xterm):

Basic / button tracking: ESC[?1000h
Button + motion: ESC[?1002h
SGR (modern) mouse: ESC[?1006h – recommended since it supports >223 coordinates and scroll.
Send at startup:

void term_enable_mouse(void) {
    write(STDOUT_FILENO, "\x1b[?1000h\x1b[?1002h\x1b[?1006h", 24);
}
void term_disable_mouse(void) {
    write(STDOUT_FILENO, "\x1b[?1000l\x1b[?1002l\x1b[?1006l", 24);
}
SGR mouse events format (from terminal):

ESC[<b;x;yM – button press or drag
ESC[<b;x;y m – button release
Parse as:

typedef enum { MOUSE_PRESS, MOUSE_RELEASE, MOUSE_MOVE, MOUSE_SCROLL } MouseType;

typedef struct {
    MouseType type;
    int button;   // 1 left, 2 middle, 3 right, 4/5 scroll, etc.
    int x, y;     // 1-based coordinates
    uint32_t mods; // ctrl/alt/shift bits from b
} MouseEvent;
3.5 Vim-Style Modal Input
Represent Vim modes:

typedef enum {
    VIM_NORMAL,
    VIM_INSERT,
    VIM_COMMAND
} VimMode;

typedef struct {
    VimMode mode;
    // For command-line (" :wq " etc.)
    char cmdline[256];
    size_t cmdlen;

    // For operator-pending (e.g., 'd' then 'w')
    int operator_pending; // enum or KeyCode
} VimState;
Dispatch logic (simplified):

void vim_handle_key(VimState *vs, const KeyEvent *ev) {
    switch (vs->mode) {
    case VIM_INSERT:
        // Most keys insert; ESC returns to NORMAL
        break;
    case VIM_NORMAL:
        // Map single keys: h/j/k/l, d, y, p, etc.
        // Implement operator-pending for d, y, c, etc.
        break;
    case VIM_COMMAND:
        // Keys manipulate vs->cmdline until Enter or ESC
        break;
    }
}
4. Output / Rendering
4.1 Core ANSI Sequences
Basic operations:

Operation	Sequence
Cursor position	ESC[<row>;<col>H (1-based)
Clear screen	ESC[2J
Clear to end of line	ESC[K
Hide cursor	ESC[?25l
Show cursor	ESC[?25h
Alternate screen on	ESC[?1049h
Alternate screen off	ESC[?1049l
Color / style (SGR):

Style	Sequence
Reset	ESC[0m
Bold	ESC[1m
Underline	ESC[4m
Reverse	ESC[7m
Foreground 8-color	ESC[30–37m
Background 8-color	ESC[40–47m
256-color fg	ESC[38;5;<n>m
256-color bg	ESC[48;5;<n>m
4.2 Efficient Screen Updates (Diff-Based Rendering)
Algorithm outline (screen_render_diff()):

Keep current cursor position and current attributes (current_fg, current_bg, current_attrs) as state.
Iterate over each row and column comparing front_buffer and back_buffer.
When you encounter a differing cell:
Move cursor if necessary (CSI H or relative).
If attributes changed, emit minimal SGR sequence.
Emit cell's UTF-8 glyph.
Copy the cell into front_buffer.
Group runs of identical attributes in a row to reduce SGR/CSI noise.
Example pseudo-diff:

for (int y = 0; y < h; y++) {
    int x = 0;
    while (x < w) {
        Cell *old = &front->cells[y*w + x];
        Cell *new = &back->cells[y*w + x];

        if (cells_equal(old, new)) { x++; continue; }

        term_move_cursor(y+1, x+1); // 1-based
        apply_attrs(new, &cur_attrs);

        // Write contiguous run of different cells on this row
        int run_start = x;
        while (x < w && !cells_equal(&front->cells[y*w + x], &back->cells[y*w + x]) &&
               attrs_equal(&back->cells[y*w + run_start], &back->cells[y*w + x])) {
            char utf8[8];
            int len = encode_utf8(back->cells[y*w + x].ch, utf8);
            term_write(utf8, len);
            front->cells[y*w + x] = back->cells[y*w + x];
            x++;
        }
    }
}
4.3 Unicode/UTF-8 Width Considerations
Many characters are width 1; East Asian wide characters are width 2; combining marks are width 0.
Use wcwidth(3) and mbtowc(3) or mbrtowc(3) from libc as a pragmatic choice.
When placing characters:
Set cell.width = 2 for wide chars at first cell, and cell.width = 0 and ch = 0 for trailing cell.
Ensure operations respect width (cursor movement, selection, etc.).
4.4 Terminal Resize Handling (SIGWINCH)
Terminals send SIGWINCH when size changes.
Use ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) to get new size.
Signal handling pattern:

#include <signal.h>
#include <sys/ioctl.h>

static volatile sig_atomic_t g_resize_pending = 0;

static void sigwinch_handler(int signo) {
    (void)signo;
    g_resize_pending = 1;
}

void signal_init(void) {
    struct sigaction sa = {0};
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);
}

void check_resize(void) {
    if (!g_resize_pending) return;
    g_resize_pending = 0;

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        screen_resize(ws.ws_col, ws.ws_row);
        app_on_resize(ws.ws_col, ws.ws_row);
    }
}
Call check_resize() each loop iteration.

5. Widget / Component System
5.1 Widget Base Type
Use a simple retained widget tree.

typedef struct Widget Widget;

typedef struct {
    int x, y, w, h;
} Rect;

typedef enum {
    WIDGET_CONTAINER,
    WIDGET_LABEL,
    WIDGET_TEXTBOX,
    WIDGET_LIST,
    WIDGET_STATUSBAR,
    WIDGET_NOTIFICATION_AREA,
    // ...
} WidgetType;

struct Widget {
    WidgetType type;
    Rect bounds;
    int z_index;            // ordering within siblings
    int focusable;

    struct Widget *parent;
    struct Widget *first_child;
    struct Widget *next_sibling;

    void (*draw)(Widget *w, ScreenBuffer *buf);
    int  (*on_key)(Widget *w, const KeyEvent *ev);
    int  (*on_mouse)(Widget *w, const MouseEvent *ev);
    void (*on_focus_change)(Widget *w, int focused);
    void (*on_resize)(Widget *w, int new_w, int new_h);

    void *userdata;         // widget-specific state
};
5.2 Layout Management Approaches
Keep layout simple:

Use containers that implement a strategy:
Horizontal split, vertical split.
Fixed size vs "flex" (share remaining space).
Each container computes children's Rect during layout pass.
API sketch:

void widget_add_child(Widget *parent, Widget *child);
void widget_remove_child(Widget *child);

void layout_apply(Widget *root) {
    layout_widget(root, root->bounds);
}

void layout_widget(Widget *w, Rect r) {
    w->bounds = r;

    if (w->type == WIDGET_CONTAINER) {
        // e.g., vertical stack layout
        int child_y = r.y;
        for (Widget *c = w->first_child; c; c = c->next_sibling) {
            Rect cr = { r.x, child_y, r.w, child_height_estimate(c) };
            layout_widget(c, cr);
            child_y += cr.h;
        }
    } else {
        if (w->on_resize) w->on_resize(w, r.w, r.h);
    }
}
5.3 Focus Management
Keep a global:

Widget *g_focused_widget;
Functions:

void focus_set(Widget *w) {
    if (g_focused_widget == w) return;
    if (g_focused_widget && g_focused_widget->on_focus_change)
        g_focused_widget->on_focus_change(g_focused_widget, 0);
    g_focused_widget = w;
    if (w && w->on_focus_change)
        w->on_focus_change(w, 1);
}

void app_dispatch_key(const KeyEvent *ev) {
    if (g_focused_widget && g_focused_widget->on_key) {
        if (g_focused_widget->on_key(g_focused_widget, ev)) return;
    }
    // fall back: global keybindings, Vim mode, etc.
}
5.4 Z-Ordering / Overlapping Elements
Within a container, sort children by z_index.
For drawing:
Precompute a flat list of widgets sorted by global z-order.
Draw from lowest to highest; later drawing overwrites cells.
Simpler: maintain separate overlay widgets (status bars, popups, notifications) and always draw them last.

6. Notifications System
6.1 Data Structure
typedef enum {
    NOTIFY_INFO,
    NOTIFY_WARN,
    NOTIFY_ERROR,
} NotifyLevel;

typedef struct Notification {
    char text[256];
    NotifyLevel level;
    uint64_t created_ms;
    uint64_t duration_ms; // e.g., 3000ms
    int sticky;           // 1 if must be dismissed by action
    struct Notification *next;
} Notification;

typedef struct {
    Notification *head;
    int max_on_screen;    // e.g., 3
    int position;         // top or bottom of screen
} NotifyManager;
6.2 Timer-Based Auto-Dismiss
timers_on_tick() checks current time.
Remove notifications whose now_ms - created_ms >= duration_ms and !sticky.
Mark notification area as dirty.
6.3 Rendering / Stack and Priority
For stacking:
Sort notifications by level (ERROR > WARN > INFO) or by creation time.
Choose up to max_on_screen.
Render as overlay widget at top or bottom rows:
Each notification takes a line (or more).
Color based on level.
Widget draw callback:

void notify_widget_draw(Widget *w, ScreenBuffer *buf) {
    NotifyManager *nm = w->userdata;
    int y_start = (nm->position == TOP) ? 0 : (buf->height - nm->max_on_screen);

    int y = y_start;
    for (Notification *n = nm->head; n && y < buf->height; n = n->next, y++) {
        uint8_t fg, bg;
        pick_colors_for_level(n->level, &fg, &bg);

        int x = 0;
        char line[buf->width + 1];
        snprintf(line, sizeof(line), " %s", n->text);
        // truncate / pad
        render_text_in_line(buf, x, y, line, fg, bg);
    }
}
7. Vim Mode Implementation
7.1 Modal State Machine
The main idea:

Insert mode:
Printable keys insert text.
ESC returns to NORMAL.
Normal mode:
Keys map to commands (motions, operators).
Some keys initiate multi-key sequences (e.g., d, y, g, z).
Command mode:
: enters command-line; editing keys operate on a small buffer.
7.2 Key Binding System
Define actions:

typedef enum {
    CMD_NOP,
    CMD_MOVE_LEFT,
    CMD_MOVE_RIGHT,
    CMD_MOVE_UP,
    CMD_MOVE_DOWN,
    CMD_ENTER_INSERT,
    CMD_DELETE_MOTION,
    CMD_YANK_MOTION,
    CMD_SAVE_FILE,
    CMD_QUIT,
    CMD_CUSTOM, // app-specific
    // ...
} CommandId;
Key binding entry (simple):

typedef struct {
    const char *seq;    // e.g., "h", "j", "k", "l", "dd", "dw"
    VimMode modes;      // bitmask of modes where it applies
    CommandId cmd;
} KeyBinding;
To support sequences beyond 1 char, use a trie:

typedef struct KeyNode {
    uint32_t ch;               // Unicode codepoint or special key
    CommandId cmd;             // CMD_NOP if intermediate
    struct KeyNode *child;     // first child
    struct KeyNode *sibling;   // next alternative
} KeyNode;
Workflow:

Maintain KeyNode *current_node = root; and pending_sequence string.
When a key arrives, traverse down the trie:
If you reach a node with cmd != CMD_NOP and there's no longer possible extension, execute cmd and reset.
If ambiguous (can be more keys): start a short timeout to decide between executing now vs waiting for more keys (like d vs dd).
If no match: fallback (just treat first key, reset).
7.3 Motions and Operators
Define motions as functions on a cursor / selection:

typedef struct {
    int line;
    int col;
} CursorPos;

typedef CursorPos (*MotionFn)(CursorPos start);

typedef struct {
    CommandId op;  // DELETE, YANK, CHANGE
    MotionFn motion;
} OperatorContext;
When user presses an operator (e.g., d):
set vs->operator_pending = CMD_DELETE_MOTION.
Next key determines motion (e.g., w => motion_word).
Execute motion to get end position.
Apply operation to range [start, end).
Clear operator_pending.
7.4 Command Parsing
For : commands:

Buffer ASCII characters, support simple editing (backspace, left/right).
On Enter, parse:
:w, :q, :wq, :set ..., or custom commands.
Use a simple tokenizer; no need for full Vim ex-compat.
8. Common Pitfalls
8.1 Terminal Compatibility Issues
Different terminals encode some keys differently (F-keys, Home/End).
Strategy:
Support common xterm encodings.
Add a config/test mode that prints raw bytes for keys so user can update bindings.
Avoid advanced/rare control sequences; stick to widely-supported ANSI + xterm extensions.
8.2 Race Conditions in Input Handling
ESC vs Alt-modified keys vs F-keys sequences: treat as discussed with small timeouts.
Don't block in read(); always use select()/poll() with appropriate timeouts.
Don't manipulate terminal (resize, etc.) from signal handler; just set flags.
8.3 Memory Management Concerns
Use contiguous arrays for ScreenBuffer to improve locality.
Carefully manage widget lifetimes; centralize allocation/free functions.
Avoid frequent malloc/free in inner loops (rendering, input parsing); use small fixed buffers or pools.
8.4 Signal Handling Gotchas
Signal handlers must be async-signal-safe. Only set flags or write to a self-pipe.
Install handlers for:
SIGWINCH (resize).
Possibly SIGINT/SIGTERM to restore terminal and exit gracefully.
On abnormal exit, ensure you restore termios (use atexit() as a backstop).
8.5 UTF-8 Width Calculation Issues
Some terminals' width decisions can differ from wcwidth().
Edge cases: emojis, ZWJ sequences, combining marks.
Pragmatic guidance:
Use wcwidth()/wcswidth() and accept small mismatches.
In logs/tests, detect odd alignments and treat as "non-critical cosmetic" issues.
9. Testing Strategy
9.1 Architecture for Testability
Abstract terminal I/O behind an interface:

typedef struct {
    ssize_t (*write)(void *userdata, const void *buf, size_t len);
    ssize_t (*read)(void *userdata, void *buf, size_t len);
    int     (*get_size)(void *userdata, int *cols, int *rows);
    void   *userdata;
} TermBackend;
Production: backend wraps STDIN_FILENO, STDOUT_FILENO and ioctl().
Tests: backend uses memory buffers for I/O.
9.2 Unit Tests
Test input parser:
Feed known byte sequences and check emitted KeyEvent / MouseEvent list.
Test layout functions:
Input: widget tree + container sizes.
Output: bounds of each child.
Test diff renderer:
Use fake TermBackend capturing writes into a string.
Compare produced escape sequences against expected golden files.
9.3 Integration Tests with PTY
On Unix, use openpty() and spawn your TUI in a child process:

Parent:
Feeds input (byte sequences).
Records output over time.
Assert:
At certain steps, output matches expectations (or at least contains certain sequences).
This allows realistic testing of termios interactions without a real user.

9.4 Regression Tests for Vim Keymaps
Maintain a table of sequences (e.g. "dw", "3dd") and expected buffer states.
Run them through the Vim mode state machine and check resulting text and cursor.
10. Performance Considerations
10.1 Minimizing Writes to the Terminal
Diff-based rendering, as described.
Batch output:
Collect sequences into an in-memory buffer and write() once per frame.
Or use writev() to avoid multiple syscalls.
Avoid clearing the entire screen on every frame; only update changed cells.
10.2 Efficient Data Structures
Use flat arrays for screen buffers (Cell[width * height]).
Keep widget tree reasonably shallow; avoid O(N^2) layout or drawing.
Precompute common escape sequences (like SGR for colors) in small string buffers.
10.3 Memory Pooling
For short-lived objects (notifications, temporary buffers), use freelists:
Preallocate arrays and maintain a simple free list of indices.
Avoid per-frame heap allocations in the hot path (render, input).
11. Implementation Roadmap (Step-by-Step)
Phase 1: Terminal Core (S–M effort)
 Implement raw mode (termios), alternate screen enter/leave, cursor hide/show
 Implement screen.c with double buffers and minimal drawing (no diff yet, just full redraw)
 Handle SIGWINCH and resize
Phase 2: Input + UTF-8 (M effort)
 Implement non-blocking input reader, UTF-8 decoder, and simple KeyEvent model (letters, ESC, arrows)
 Add minimal escape parsing for arrows/Home/End
Phase 3: Event Loop (S effort)
 Create event_loop.c around select() with idle + timer callbacks
 Integrate input, resize handling, and screen redraw
Phase 4: Basic Widgets & Layout (M effort)
 Implement widget base struct, simple container, label, status bar
 Add focus handling and basic key dispatch to focused widget
Phase 5: Vim Mode (M–L effort)
 Implement VIM_NORMAL and VIM_INSERT modes controlling a simple text buffer widget
 Add keybinding table and small trie for multi-key sequences
 Add VIM_COMMAND with : commands like :q and :w
Phase 6: Notifications (S–M effort)
 Implement NotifyManager and notification widget overlay (top or bottom)
 Add timer-based auto-dismiss and priority coloring
Phase 7: Mouse Support + Improved Diff Rendering (M–L effort)
 Enable SGR mouse, parse MouseEvents, route to widgets
 Upgrade screen renderer to full diff-based approach and attribute coalescing
Phase 8: Polish & Testing (ongoing)
 Add regression tests for UTF-8, resize, basic Vim combos
 Stabilize handling for ESC timeouts and ambiguous sequences
12. Trade-offs and Design Decisions
Why no terminfo parsing initially?
It greatly complicates the implementation for limited gain. Most users run in xterm-compatible terminals; start there and add compatibility only when real issues surface.

Why retained-mode widget tree instead of immediate mode?
Retained-mode simplifies focus, input routing, notifications, and z-ordering. It's also easier to unit test.

Why diff-based rendering later, not first?
A full-screen redraw each frame is simpler and usually acceptable initially; optimizing diffing is worthwhile once basic behavior is working.

13. Risks and Guardrails
Terminal restoration on crash: ensure termios is restored in all exit paths (use atexit(), signal handlers).
Keymapping differences: allow the user to dump raw key codes to adjust mappings without recompiling core logic.
UTF-8 oddities: treat width anomalies as cosmetic; avoid trying to fully emulate Vim's exact behavior until necessary.
Signal safety: never call non-async-signal-safe functions inside signal handlers.
14. When to Consider the Advanced Path
Move beyond this simple architecture if:

You must support non-ANSI terminals or very old environments.
You need advanced features like true-color with per-terminal quirks, rich clipboard integration, or non-UTF-8 encodings.
Your app has very large screens and frequent updates, and profiling shows diff-based rendering is insufficient.
15. Optional Advanced Features
Implement a terminfo parser in C (read /usr/share/terminfo, parse compiled format); compute capability strings at startup.
Add asynchronous I/O via epoll/kqueue with multiple fds (network, files).
Implement composable layouts (flexbox-like) and a proper focus ring with tab order, roving focus.
Add macro recording, undo/redo, and advanced Vim motions/operators that mirror real Vim.
16. References
Use these as primary documentation while building:

ECMA-48: "Control Functions for Coded Character Sets" (search for "ECMA-48 PDF"; official ECMA site hosts it)
XTerm Control Sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
POSIX termios: man 3 termios, man tcsetattr, man tcgetattr
Signal handling: man 7 signal
PTYs: man 3 openpty, man 7 pty
Mouse tracking in xterm: section in XTerm control sequences doc
