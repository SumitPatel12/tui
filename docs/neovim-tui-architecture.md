# Neovim Terminal UI (TUI) Architecture

Neovim implements its TUI through a sophisticated multi-layered system that handles window management, screen rendering, syntax highlighting, and notifications.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Neovim Core                             │
│          (drawscreen.c, drawline.c, grid.c)                 │
│   Manages windows, buffers, syntax highlighting             │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│                 Highlight System                            │
│   (highlight_defs.h, highlight_group.c)                     │
│   • Define highlight groups (HlAttrs)                       │
│   • Map syntax elements to colors/attributes                │
│   • RGB vs terminal color support                           │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│         UI Abstraction & Grid Management                    │
│   (ui.c, ui_compositor.c, grid.h)                           │
│   • Multi-grid support (windows, floating)                  │
│   • Compositing for floating windows                        │
│   • Event generation (grid_line, etc.)                      │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│              TUI Client (ui_client.c)                       │
│   • RPC bridge between server and TUI                       │
│   • Receives grid_line, highlight events                    │
│   • Manages terminal properties                             │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│           TUI Rendering Layer (tui.c)                       │
│   • Window/grid rendering                                   │
│   • Syntax highlighting output                              │
│   • Screen management & scrolling                           │
│   • Terminal capabilities detection                         │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│     Input & Terminfo (input.c, terminfo.c)                  │
│   • Terminal I/O handling                                   │
│   • Keyboard protocol (Kitty, Xterm)                        │
│   • Terminal capability database                            │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┘
│              Terminal Output (TTY)                          │
│   • ANSI escape sequences                                   │
│   • Color codes, cursor movement                            │
│   • Mouse/input events                                      │
└─────────────────────────────────────────────────────────────┘
```

## Window Creation & Grid Management

### TUIData Structure

```c
struct TUIData {
  Loop *loop;                    // Event loop for async operations
  char buf[OUTBUF_SIZE];         // 64KB output buffer for escape sequences
  size_t bufpos;                 // Current position in buffer
  UGrid grid;                    // Unicode grid (actual screen representation)
  kvec_t(Rect) invalid_regions;  // Regions needing redraw
  int row, col;                  // Current cursor position
  
  // Terminal capabilities
  TerminfoEntry ti;              // Terminfo database entry
  bool can_change_scroll_region;
  bool can_set_lr_margin;
  bool has_sync_mode;
  
  // Attributes and coloring
  kvec_t(HlAttrs) attrs;         // Array of highlight attribute definitions
  int print_attr_id;             // Currently active attribute ID
  HlAttrs clear_attrs;           // Default colors for clearing
  
  // Input handling
  TermInput input;               // Keyboard input processor
  
  // Terminal modes
  struct {
    bool grapheme_clusters : 1;
    bool theme_updates : 1;
    bool resize_events : 1;
  } modes;
};
```

### UGrid Structure

```c
typedef struct {
  schar_T data;      // Single character (up to 32-bit)
  sattr_T attr;      // Attribute/color ID
} UCell;

typedef struct {
  int row, col;
  int width, height;
  UCell **cells;     // 2D array of cells
} UGrid;
```

### Window Initialization

The TUI is started via `tui_start()`:

```c
void tui_start(TUIData **tui_p, int *width, int *height, char **term, bool *rgb)
{
  TUIData *tui = xcalloc(1, sizeof(TUIData));
  tui->loop = &main_loop;
  
  // Initialize grid
  ugrid_init(&tui->grid);
  
  // Start terminal and detect capabilities
  tui_terminal_start(tui);
  
  // Query terminal features (extended underline, Kitty protocol, etc.)
  tui_query_extended_underline(tui);
  tui_query_kitty_keyboard(tui);
  
  // Report back capabilities
  *width = tui->width;
  *height = tui->height;
  *term = tui->term;
  *rgb = tui->rgb;
}
```

## Screen Rendering Flow

```
update_screen() [drawscreen.c]
    │
    ├─→ For each window, call win_line() [drawline.c]
    │       │
    │       ├─→ Resolve syntax highlighting for each character
    │       ├─→ Apply decorations (line numbers, folds, etc.)
    │       └─→ Call grid_put_linebuf() with line data + attributes
    │
    ├─→ Invalidate affected regions in TUI grid
    │
    └─→ tui_flush() [tui.c]
            │
            ├─→ Process invalid_regions queue
            ├─→ For each region, call print_cell_at_pos()
            ├─→ Generate escape sequences for colors/attributes
            └─→ flush_buf() → uv_write() → TTY
```

### Core Rendering Function

```c
void tui_flush(TUIData *tui)
{
  UGrid *grid = &tui->grid;
  
  // Process all invalidated regions
  while (kv_size(tui->invalid_regions)) {
    Rect r = kv_pop(tui->invalid_regions);
    
    for (int row = r.top; row < r.bot; row++) {
      // Find trailing spaces to optimize clearing
      int clear_col;
      for (clear_col = r.right; clear_col > 0; clear_col--) {
        UCell *cell = &grid->cells[row][clear_col - 1];
        if (!(cell->data == schar_from_ascii(' ')
              && cell->attr == clear_attr)) {
          break;
        }
      }
      
      // Print each cell in the row
      UGRID_FOREACH_CELL(grid, row, r.left, clear_col, {
        print_cell_at_pos(tui, row, curcol, cell, ...);
      });
      
      // Clear remaining trailing spaces
      if (clear_col < r.right) {
        clear_region(tui, row, row + 1, clear_col, r.right, clear_attr);
      }
    }
  }
  
  // Position cursor and send to terminal
  cursor_goto(tui, tui->row, tui->col);
  flush_buf(tui);  // Actually write to TTY
}
```

### Cursor Movement Optimization

Neovim optimizes cursor positioning by choosing the cheapest movement:

```c
static void cursor_goto(TUIData *tui, int row, int col)
{
  // If at home position, use home sequence (cheaper than absolute positioning)
  if (row == 0 && col == 0) {
    terminfo_out(tui, kTerm_cursor_home);  // Usually "\x1b[H" or similar
    return;
  }
  
  // On same row, try relative horizontal movement
  if (row == grid->row) {
    if (col < grid->col) {
      // Moving left: BS is cheaper than CUB for short distances
      int n = grid->col - col;
      if (n <= 4) {
        while (n--) terminfo_out(tui, kTerm_cursor_left);  // "\x1b[D"
      } else {
        terminfo_print_num1(tui, kTerm_parm_left_cursor, n);
      }
      return;
    }
  }
  
  // On same column, try relative vertical movement
  if (col == grid->col) {
    if (row > grid->row) {
      int n = row - grid->row;
      if (n <= 4) {
        while (n--) terminfo_out(tui, kTerm_cursor_down);
      } else {
        terminfo_print_num1(tui, kTerm_parm_down_cursor, n);
      }
      return;
    }
  }
  
  // Fall back to absolute positioning
  terminfo_print_num2(tui, kTerm_cursor_address, row, col);
}
```

### Character Output with Attributes

```c
static void print_cell(TUIData *tui, char *buf, sattr_T attr)
{
  update_attrs(tui, attr);  // Send attribute changes if needed
  out(tui, buf, strlen(buf));  // Queue the character
  grid->col++;
}
```

## Syntax Highlighting & Coloring

### Highlight Attributes Structure

```c
typedef struct {
  int16_t rgb_ae_attr, cterm_ae_attr;  // HlAttrFlags (bold, italic, etc.)
  RgbValue rgb_fg_color, rgb_bg_color, rgb_sp_color;  // 24-bit colors
  int16_t cterm_fg_color, cterm_bg_color;  // 256-color or 16-color
  int32_t hl_blend;   // Blending/transparency (0-100)
  int32_t url;        // Hyperlink ID
} HlAttrs;

// Attribute flags
typedef enum {
  HL_INVERSE         = 0x01,
  HL_BOLD            = 0x02,
  HL_ITALIC          = 0x04,
  HL_UNDERLINE_MASK  = 0x38,  // Multiple underline styles
  HL_UNDERCURL       = 0x10,
  HL_UNDERDOUBLE     = 0x18,
  HL_UNDERDOTTED     = 0x20,
  HL_UNDERDASHED     = 0x28,
  HL_STANDOUT        = 0x0040,
  HL_STRIKETHROUGH   = 0x0080,
  HL_ALTFONT         = 0x0100,
  HL_FG_INDEXED      = 0x1000,  // Use indexed colors instead of RGB
  HL_BG_INDEXED      = 0x0800,
} HlAttrFlags;
```

### Highlight Application

The `update_attrs()` function sends appropriate ANSI escape sequences:

```c
static void update_attrs(TUIData *tui, int attr_id)
{
  HlAttrs attrs = kv_A(tui->attrs, (size_t)attr_id);
  int attr = tui->rgb ? attrs.rgb_ae_attr : attrs.cterm_ae_attr;
  
  // Reset to default first
  if (!tui->default_attr) {
    terminfo_out(tui, kTerm_exit_attribute_mode);  // "\x1b[0m" (SGR 0)
  }
  
  // Apply text attributes
  if (attr & HL_BOLD) {
    terminfo_out(tui, kTerm_enter_bold_mode);      // "\x1b[1m"
  }
  if (attr & HL_ITALIC) {
    terminfo_out(tui, kTerm_enter_italics_mode);   // "\x1b[3m"
  }
  
  // Handle underline styles (supports multiple variants)
  if (tui->ti.defs[kTerm_set_underline_style]) {
    // Terminal supports extended underlines (CSS-like)
    if (attr & HL_UNDERCURL) {
      terminfo_print_num1(tui, kTerm_set_underline_style, 3);  // "\x1b[4:3m"
    }
    if (attr & HL_UNDERDOUBLE) {
      terminfo_print_num1(tui, kTerm_set_underline_style, 2);  // "\x1b[4:2m"
    }
  }
  
  // RGB or indexed colors
  if (tui->rgb && !(attr & HL_FG_INDEXED)) {
    // 24-bit truecolor foreground
    int fg = attrs.rgb_fg_color;
    if (fg != -1) {
      out_printf(tui, 128, "\x1b[38:2::%d:%d:%dm",
                 (fg >> 16) & 0xff,  // R
                 (fg >> 8) & 0xff,   // G
                 fg & 0xff);         // B
    }
  } else {
    // 256-color or 16-color palette
    int fg = attrs.cterm_fg_color - 1;
    if (fg != -1) {
      terminfo_print_num1(tui, kTerm_set_a_foreground, fg);
    }
  }
  
  // Handle hyperlinks (OSC 8)
  if (tui->url != attrs.url) {
    if (attrs.url >= 0) {
      const char *url = urls.keys[attrs.url];
      out_printf(tui, 256, "\x1b]8;id=%" PRIu64 ";%s\x1b\\", id, url);
    } else {
      out(tui, S_LEN("\x1b]8;;\x1b\\"));  // Close hyperlink
    }
    tui->url = attrs.url;
  }
}
```

### Highlight Group Definition

```c
typedef struct {
  char *sg_name;              // "Normal", "Comment", etc.
  int sg_attr;                // Screen attribute ID
  int sg_link;                // Link to another group
  int sg_cterm;               // Terminal attributes
  int sg_cterm_fg;            // Terminal fg (1-256, 0=unset)
  int sg_cterm_bg;            // Terminal bg (1-256, 0=unset)
  RgbValue sg_rgb_fg;         // RGB foreground (0x000000 to 0xFFFFFF)
  RgbValue sg_rgb_bg;         // RGB background
  RgbValue sg_rgb_sp;         // Special color (underline, undercurl)
  int sg_blend;               // Transparency (0-100)
} HlGroup;
```

### Terminal Color Support Detection

```c
// Determine if RGB (truecolor) is supported
tui->rgb = term_has_truecolor(tui, colorterm);

// Check for extended underlines
if (!TI_HAS(kTerm_set_underline_style)) {
  tui_query_extended_underline(tui);  // Send DECRQSS query
}
```

## Notifications & Messages

### Message Display Pipeline

Messages flow through a specialized grid:

```c
// Message structure in message.c
typedef struct msgchunk_S msgchunk_T;
struct msgchunk_S {
  msgchunk_T *sb_next;
  msgchunk_T *sb_prev;
  char sb_eol;          // Line ending flag
  int sb_msg_col;       // Column position
  int sb_hl_id;         // Highlight group ID
  char sb_text[];       // Text content
};
```

**Message Output Functions:**
- `msg_puts()` - Display message with default highlighting
- `msg_puts_hl()` - Display with specific highlight group
- `msg_scrolled` - Track message-induced scrolling
- `grid_line_flush_if_valid_row()` - Batch message output

### Message Highlighting

Messages receive highlight groups like:
- `HLF_E` - Error messages
- `HLF_W` - Warning messages  
- `HLF_I` - Info/incremental search
- `HLF_M` - "--More--" prompt
- `HLF_R` - "Return to continue" message

## Terminal Capability Detection & Mode Handling

### Terminfo Database

Neovim queries the terminal's capabilities via terminfo:

```c
// Terminal mode detection
void tui_handle_term_mode(TUIData *tui, TermMode mode, TermModeState state)
{
  switch (mode) {
  case kTermModeSynchronizedOutput:
    // Atomic screen updates
    tui->has_sync_mode = true;
    break;
    
  case kTermModeGraphemeClusters:
    // Handle complex grapheme clustering
    tui_set_term_mode(tui, mode, true);
    tui->modes.grapheme_clusters = true;
    break;
    
  case kTermModeLeftAndRightMargins:
    // Support for margin-based scrolling
    tui->has_left_and_right_margin_mode = true;
    break;
  }
}
```

### Terminal Mode Requests

```c
// Set/reset DEC private modes
static void tui_set_term_mode(TUIData *tui, TermMode mode, bool set)
{
  // DECSET (\x1b[?<mode>h) or DECRST (\x1b[?<mode>l)
  char buf[12];
  int len = snprintf(buf, sizeof(buf), "\x1b[?%d%c", (int)mode, set ? 'h' : 'l');
  out(tui, buf, (size_t)len);
}

// Query terminal mode support
static void tui_request_term_mode(TUIData *tui, TermMode mode)
{
  // DECRQM (Device Attribute Request)
  out(tui, "\x1b[?<mode>$p", ...);
}
```

### Keyboard Protocol Support

```c
static void tui_query_kitty_keyboard(TUIData *tui)
{
  // Query Kitty keyboard protocol support
  out(tui, S_LEN("\x1b[?u\x1b[c"));  // CSI ? u + DA1
}

void tui_set_key_encoding(TUIData *tui)
{
  switch (tui->input.key_encoding) {
  case kKeyEncodingKitty:
    // Progressive enhancement flags
    out(tui, S_LEN("\x1b[>3u"));  // Disambiguate + report events
    break;
  case kKeyEncodingXterm:
    out(tui, S_LEN("\x1b[>4;2m"));  // Xterm modifyOtherKeys
    break;
  case kKeyEncodingLegacy:
    break;
  }
}
```

## Input Handling

### Input Processing

```c
typedef struct {
  int in_fd;                          // stdin file descriptor
  TermKey *tk;                        // libtermkey parser
  TermKey_Terminfo_Getstr_Hook *tk_ti_hook_fn;
  KeyEncoding key_encoding;           // Kitty, Xterm, or Legacy
  OptInt ttimeoutlen;                 // Timeout for multi-byte sequences
  uv_timer_t timer_handle;            // Timeout timer
  RStream read_stream;                // libuv stream
  char key_buffer[KEY_BUFFER_SIZE];   // Input buffer
  size_t key_buffer_len;
  
  struct {
    void (*primary_device_attr)(TUIData *tui);  // DA1 response callback
  } callbacks;
} TermInput;
```

**Keyboard Protocols Supported:**
- **Kitty Protocol**: Modern, unambiguous key encoding with modifiers
- **Xterm modifyOtherKeys**: Traditional approach with SGR encoding
- **Legacy**: Basic ANSI sequences

## Complete Flow Diagram: Text with Color

```
┌──────────────────────────────────────────────────────────────────┐
│ Core Editor: Buffer content + syntax rules                       │
└────────────────────────┬─────────────────────────────────────────┘
                         │
                    update_screen()
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│ Line Drawing (drawline.c):                                       │
│ • Iterate through visible buffer lines                           │
│ • Apply syntax highlighting (syn_get_id)                         │
│ • Resolve decorations (line numbers, signs, etc.)                │
│ • Build linebuf with characters + HlAttrs IDs                    │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│ Grid Update (grid.c):                                            │
│ • Convert linebuf to UGrid cell format                           │
│ • Track invalid regions for TUI                                  │
│ • Store character + attribute ID in each cell                    │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│ UI Events (ui.c):                                                │
│ • Emit grid_line events to connected UIs                         │
│ • Include cell data and attribute IDs                            │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│ TUI Client receives grid_line events (ui_client.c):              │
│ • Decode RPC message                                             │
│ • Update TUI's internal UGrid                                    │
│ • Invalidate regions in TUI                                      │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│ TUI Flush (tui.c):                                               │
│ • Process invalid_regions queue                                  │
│ • For each cell:                                                 │
│   ├─ cursor_goto(row, col)  ← optimized movement                 │
│   └─ update_attrs(attr_id)  ← send ANSI SGR sequences            │
│   └─ out(character)         ← queue to buffer                    │
│                                                                  │
│ Update attributes generates:                                     │
│   • "\x1b[38:2:RR:GG:BBm"   ← RGB foreground                     │
│   • "\x1b[48:2:RR:GG:BBm"   ← RGB background                     │
│   • "\x1b[1m"               ← bold                               │
│   • "\x1b[3m"               ← italic                             │
│   • "\x1b[4m"               ← underline                          │
│   • "\x1b[4:3m"             ← undercurl (extended)               │
│   • "\x1b[58:2:RR:GG:BBm"   ← underline color                    │
│   • "\x1b]8;id=ID;URL\x1b\\" ← hyperlink (OSC 8)                 │
└────────────────────────┬─────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────────┐
│ Output Buffer Flush (tui.c):                                     │
│ • Batch all escape sequences in tui->buf                         │
│ • flush_buf() → uv_write() → TTY                                 │
│ • Complete ANSI escape sequence example:                         │
│   "\x1b[8;20;80t"      ← resize terminal                         │
│   "\x1b[?1002h"        ← enable mouse button tracking            │
│   "\x1b[?1006h"        ← enable SGR mouse mode                   │
│   "\x1b[2004h"         ← enable bracketed paste                  │
└────────────────────────┬─────────────────────────────────────────┘
                         │
                      Terminal
                    renders colors
                    and attributes
```

## Key Implementation Files Reference

| File | Purpose |
|------|---------|
| [tui.c](https://github.com/neovim/neovim/blob/master/src/nvim/tui/tui.c) | Main TUI rendering, screen management, attributes |
| [tui.h](https://github.com/neovim/neovim/blob/master/src/nvim/tui/tui.h) | TUI public API |
| [tui_defs.h](https://github.com/neovim/neovim/blob/master/src/nvim/tui/tui_defs.h) | Terminal mode definitions |
| [input.c](https://github.com/neovim/neovim/blob/master/src/nvim/tui/input.c) | Keyboard input processing, Kitty/Xterm protocol support |
| [terminfo.c](https://github.com/neovim/neovim/blob/master/src/nvim/tui/terminfo.c) | Terminal capability database integration |
| [highlight_defs.h](https://github.com/neovim/neovim/blob/master/src/nvim/highlight_defs.h) | Highlight attribute definitions (colors, styles) |
| [highlight_group.c](https://github.com/neovim/neovim/blob/master/src/nvim/highlight_group.c) | Highlight group management |
| [drawscreen.c](https://github.com/neovim/neovim/blob/master/src/nvim/drawscreen.c) | Top-level screen update orchestration |
| [drawline.c](https://github.com/neovim/neovim/blob/master/src/nvim/drawline.c) | Per-line rendering with syntax highlighting |
| [grid.c](https://github.com/neovim/neovim/blob/master/src/nvim/grid.c) | Low-level grid cell manipulation |
| [ugrid.h](https://github.com/neovim/neovim/blob/master/src/nvim/ugrid.h) | Unicode grid data structure |
| [ui.c](https://github.com/neovim/neovim/blob/master/src/nvim/ui.c) | UI abstraction and event generation |
| [ui_compositor.c](https://github.com/neovim/neovim/blob/master/src/nvim/ui_compositor.c) | Floating window compositing |
| [ui_client.c](https://github.com/neovim/neovim/blob/master/src/nvim/ui_client.c) | RPC bridge to TUI |
| [message.c](https://github.com/neovim/neovim/blob/master/src/nvim/message.c) | Message display and scrolling |

## Design Patterns & Optimizations

### 1. Invalidation Tracking
Neovim only redraws regions that changed, using a vector of rectangles (`kvec_t(Rect) invalid_regions`).

### 2. Output Buffering
All escape sequences go into a 64KB buffer (`tui->buf`) before flushing to minimize system calls.

### 3. Cursor Movement Optimization
The `cursor_goto()` function chooses the cheapest ANSI escape sequence based on current position:
- Home position → `\x1b[H`
- Relative movement for short distances
- Absolute positioning as fallback

### 4. Attribute Diffing
Only sends attribute changes when the new attribute differs from the current one, avoiding redundant escape sequences.

### 5. Synchronized Output
Uses DEC private mode 2026 for atomic screen updates when supported, preventing tearing during rapid updates.
