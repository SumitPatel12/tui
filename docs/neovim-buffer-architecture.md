# Neovim Text Buffer Architecture

Neovim uses a **custom B-tree-like hierarchical structure** called a "memline tree"—NOT gap buffers, ropes, or piece tables.

## Core Data Structure: The Memline Tree

The implementation is in [`src/nvim/memline_defs.h`](https://github.com/neovim/neovim/blob/master/src/nvim/memline_defs.h) and [`src/nvim/memline.c`](https://github.com/neovim/neovim/blob/master/src/nvim/memline.c).

```c
// From memline_defs.h
typedef struct {
  linenr_T ml_line_count;       // number of lines in the buffer
  memfile_T *ml_mfp;            // pointer to associated memfile
  infoptr_T *ml_stack;          // stack of pointer blocks (navigation)
  int ml_stack_top;             // current top of ml_stack
  int ml_stack_size;            // total entries in ml_stack
  // ... caching and state fields
  bhdr_T *ml_locked;            // cached block
  linenr_T ml_locked_low;       // first line in ml_locked
  linenr_T ml_locked_high;      // last line in ml_locked
  chunksize_T *ml_chunksize;    // 800-line chunks for fast line2byte/byte2line
  int ml_numchunks;
  int ml_usedchunks;
} memline_T;
```

## Tree Architecture

**Three-level tree structure:**

1. **Block 0** - Recovery/metadata block (stores file info, timestamps, encoding)
2. **Pointer Blocks (Block 1+)** - Internal nodes in the tree (branch factor of ~128)
3. **Data Blocks** - Leaf nodes containing actual text lines

From the code comments in `memline.c`:

```c
// memline structure: the contents of a buffer.
// Essentially a tree with a branch factor of 128.
// Lines are stored at leaf nodes.
// Nodes are stored on ml_mfp (memfile_T):
//   pointer_block: internal nodes
//   data_block: leaf nodes
//
// Memline also has "chunks" of 800 lines that are separate from the 128-tree
// structure, primarily used to speed up line2byte() and byte2line().
//
// Motivation: If you have a file that is 10000 lines long, and you insert
//             a line at linenr 1000, you don't want to move 9000 lines in
//             memory.  With this structure it is roughly (N * 128) pointer
//             moves, where N is the height (typically 1-3).
```

## Pointer Block Structure

```c
typedef struct {
  blocknr_T pe_bnum;            // block number
  linenr_T pe_line_count;       // number of lines in this branch
  linenr_T pe_old_lnum;         // lnum for recovery
  int pe_page_count;            // number of pages in block
} PointerEntry;

typedef struct {
  uint16_t pb_id;               // ID for pointer block: PTR_ID
  uint16_t pb_count;            // number of pointers in this block
  uint16_t pb_count_max;        // maximum value for pb_count
  PointerEntry pb_pointer[];    // list of pointers to blocks
} PointerBlock;
```

## Data Block Structure

```c
typedef struct {
  uint16_t db_id;               // ID for data block: DATA_ID
  unsigned db_free;             // free space available
  unsigned db_txt_start;        // byte where text starts
  unsigned db_txt_end;          // byte just after data block
  long db_line_count;           // number of lines in this block
  unsigned db_index[];          // index for start of each line
                                // followed by empty space up to db_txt_start
                                // followed by the text in the lines
} DataBlock;
```

**Key design feature:** Text lines in a data block are stored **in reverse order** from their line indices. The first line's text is at the end of the block, and subsequent lines are inserted before it. This allows efficient insertion without moving existing text.

## Text Manipulation Operations

### ml_append() - Insert Lines

**Process:**
1. Locate the data block containing the line (via `ml_find_line()`)
2. Check if there's sufficient space in the data block
3. If yes: Adjust indices, move text, insert new line
4. If no: Split the data block and possibly split pointer blocks up the tree

**Performance:** O(log₁₂₈ N) where N is total lines. Minimal data movement.

### ml_delete() - Remove Lines

**Process:**
1. Find the data block containing the line
2. Calculate line size (bytes to remove)
3. Move remaining text forward in the block
4. Adjust all line indices
5. If block becomes empty: remove from pointer block; propagate deletion up if needed

**Key optimization:** Only moves text within the affected data block, not entire file.

### ml_replace() - Modify Lines

**Process:**
1. Buffer the change in memory (line stored in `ml_line_ptr`)
2. On next operation, flush changes via `ml_flush_line()`
3. Either replaces line in-place if size permits, or marks for rewriting

**Optimization:** Defers disk writes via buffering; enables batch updates.

## Diagram: Tree Structure

```
┌─────────────────────────────────────────────────────┐
│ Block 1: ROOT POINTER BLOCK                         │
│ ┌──────────────────────────────────────────────┐    │
│ │ pb_pointer[0] → Block 5, lines 1-50          │    │
│ │ pb_pointer[1] → Block 6, lines 51-100        │    │
│ │ pb_pointer[2] → Block 7, lines 101-150       │    │
│ └──────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────┘
          │                    │                    │
          ▼                    ▼                    ▼
    ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
    │ POINTER      │   │ POINTER      │   │ POINTER      │
    │ BLOCK 5      │   │ BLOCK 6      │   │ BLOCK 7      │
    ├──────────────┤   ├──────────────┤   ├──────────────┤
    │ ptr → 2      │   │ ptr → 4      │   │ ptr → 8      │
    │ ptr → 3      │   │ ptr → 9      │   │ ptr → 10     │
    └──────────────┘   └──────────────┘   └──────────────┘
        │    │            │    │            │     │
        ▼    ▼            ▼    ▼            ▼     ▼
      ┌──┐ ┌──┐        ┌──┐ ┌──┐        ┌──┐  ┌──┐
      │2 │ │3 │        │4 │ │9 │        │8 │  │10│
      └──┘ └──┘        └──┘ └──┘        └──┘  └──┘
     DATA  DATA       DATA  DATA       DATA   DATA
    BLOCKS BLOCKS    BLOCKS BLOCKS    BLOCKS BLOCKS
   (lines) (lines)  (lines) (lines)  (lines)(lines)
     1-25  26-50    51-75  76-100   101-125 126-150
```

## Diagram: Data Block Layout (Reverse Text Storage)

```
┌─────────────────────────────────────────────────────────┐
│ DATA BLOCK INTERNAL STRUCTURE                           │
├─────────────────────────────────────────────────────────┤
│ db_id:    DATA_ID                                       │
│ db_free:  available space                               │
│ db_txt_start:  offset where text begins                 │
│ db_line_count: 3 lines in this block                    │
├─────────────────────────────────────────────────────────┤
│ INDEX ARRAY (grows downward)                            │
│ [0]: offset 450 ◄─── line 1 text position               │
│ [1]: offset 380 ◄─── line 2 text position               │
│ [2]: offset 320 ◄─── line 3 text position               │
├─────────────────────────────────────────────────────────┤
│ FREE SPACE (gap in middle)                              │
├─────────────────────────────────────────────────────────┤
│ TEXT AREA (grows upward from db_txt_start)              │
│ offset 320: "Line 3\0"                                  │
│ offset 380: "Line 2 is longer\0"                        │
│ offset 450: "Line 1\0"                                  │
└─────────────────────────────────────────────────────────┘

Insertion of "Line 1.5":
- Adjust db_txt_start downward by size of new line
- Shift all existing text
- Update indices [1] and [2]
- Add new index [1] for new line
```

## Cut/Paste and Undo Operations

The data structure supports cut/paste through the **undo system** (`src/nvim/undo.c`). When you:
- **Cut**: `ml_delete()` removes lines, tracked in undo headers
- **Copy**: Lines are read via `ml_get_buf()` (uses cached blocks)
- **Paste**: `ml_append()` inserts lines, creating new undo records

The tree structure's immutability per block allows efficient undo by:
1. Keeping old pointer blocks pointing to old data blocks
2. Creating new pointer blocks for modified structures
3. Reverting by restoring old pointer block references

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Get line | O(log₁₂₈ N) | Cached in `ml_locked` |
| Append/Insert | O(log₁₂₈ N) | May trigger block splits |
| Delete | O(log₁₂₈ N) | Only affects path to line |
| Replace | O(1) buffered | Deferred to flush |
| Seek to line N | O(log₁₂₈ N) | Uses chunk cache for common case |

## Memory Management

- **Memfile**: Wraps blocks in memory with optional swap file backing
- **Block caching**: Recent blocks cached in `ml_locked`
- **Chunks**: 800-line summaries in `ml_chunksize` for fast line2byte conversions
- **Recovery**: Block 0 enables crash recovery via negative block numbers

## Why This Design?

This design is highly optimized for **vim-style line-oriented editing** where insertions/deletions happen at specific lines, making it ideal for the Vim/Neovim editing model while avoiding the overhead of global buffer restructuring.
