## Table of Contents:
- [Encodings](#encodings)
- [Unicode](#unicode)
- [Keys and Key Combination](#keys-and-key-combinations)
- [Mouse](#mouse)
- [Clipboard](#clipboard)
- [Colours](#colours)
- [Frameworks](#frameworks)

## Encodings
It's the character encodings that are used to represent the characters. It could be some old USC encoding, Asian-shift encoding (the author seems to dislike these particularly :laughing_face:).

[Character Encodings and Locales](https://tecnocode.co.uk/2011/08/27/character-encoding-and-locales/)
Terminology:
1. **Character set**: 
 - Set of symbols which can be used together.
 - Defines the symbols and semantics.
 - Not how they're encoded in memory. (Unicode, UCS, etc)
2. **Character Encoding**: Mapping from a character set to a representation of characters in memory.
3. **NULL Byte**: Single byte which has the value of 0. Typically represented as the C escape sequence '\0'.
4. **NULL Character**: For this it'd be the unicode NULL character (U+0000). Will be different for UTF-16, but you get the gist.

## Unicdoe
Even if you use UTF-8 not everything is done, ther is still the concept of full-width/double-width characters. Mostly Chinese, Japanese characters. How do you identify you ask? With a standard library function ofcourse: `wcwidth()`. You won't need to check for normal ASCII characters  but for others there's no way of knowing, so you'll have to check.

### UTF-8
UTF-8 is a variable length character encoding scheme. The maximum a character can span is 4-btyes in this scheme.
The first 128 code points (ASCII) need 1 byte, and map directly into the UTF-8 scheme. The next 1920 points, need two bytes to encode, and emcompass almost all of the Latin aplhabets. Three bytes are needed for Chinese, Japanese, Korean, and other such languages. Four bytes for emoji, and other characters.

You'll see a pattern that 1 bytes has 0 leading 1s, 2 bytes have 2 leading 1s, 3 bytes have 3 leading 1s, and 4 byte have 4 leading ones.
**Rules**:
1. *1-Byte* :
 - ASCII characters, Code ponit U+000000 to U+00007F. 
 - Byte sequence looks like: `0xxxxxxx`
2. *2-Bytes*: 
 - Code points U+000080 to U+0007FF
 - First byte starts with 110, and the second byte strats with 10.
 - Byte sequence looks like: `110xxxxx 10xxxxxx`
3. *3-Bytes*: 
 - Code points U+000800 to U+00FFFF
 - First byte starts with 1110, and the latter two bytes start with 10.
 - Byte sequence looks like: `1110xxxxx 10xxxxxx 10xxxxxx`
4. *4-Bytes*: 
 - Code points U+010000 to U+10FFFF
 - First byte starts with 11110, and the latter three bytes start with 10.
 - Byte sequence looks like: `11110xxxxx 10xxxxxx 10xxxxxx 10xxxxxx`
 
### User Locales
So, for a c program, when the main function is called it's passed an arrya of pointers to char arrays (the argv[]). These strings can very likely be arbitray in length and are encoded in the user's local character encoding. It's set using the *LC_ALL, LC_CTYPE, or LANG* environment variables.

You don't have to handle all encoding styles yourself (unless you are a masochist or something :shrug:), there is a standard solution in C for that: `libiconv.iconv()` it converts between any two character encoding known to the system, so that can be used to convert the input to UTF-8. 
To find out the users environment encoding we would use the following functions: `setlocale()` and `nl_langinfo()`.

You'd first call `setlocale()` which will determine the user's locale, and stores it. After that you'll use `nl_langinfo()` with the argument `CODESET`, it will then return a string identifying the character encoding set in the user's environment. Which we can then levarage to call the `iconv_open` function.

`printf()` doesn't know about the character encodings, it outputs exactly the bytes which are passed to tis format parameter. This means it'll work only when the encoding of the program internally is the same as the user's environment character encoding. So, ASCII is good since most encoding do support that, but other characters would need more work.

For my purposes I'll be forgoing everythig legacy. I'll be focusing on UNICODE encoding and more specifically UTF-8.

## Keys and Key Combinations
So, turns out control keys like *CTRL*, *SHIFT*, *ESCAPE*, et. al. are a bit difficult to encode as unicode characters. Especially their combinations like `CTRL + SHIFT + C` and the like.

## Mouse
There are three basic modes you can enable—1000 will only get you clicks, 1002 will get you drags, and 1003 spams you with all mouse movement. However, to get the last two ones, ncurses either ridiculously wants you to change your TERM to something like xterm-1002, or you need to write a magical sequence of e.g. "\x1b[?1002h" straight to the terminal, in addition to setting REPORT_MOUSE_POSITION.

## Clipboard
Well this won't be much of a problem unless people start pasting in stuff at which point it'll be a bit difficult to determine what to paste and how to handle input.

## Colours
Terminals support anywhere from no colors to 24 bit true colors. And boy was that a problem before.
Most of the modern terminal emulators all support true colors. Whether or not the terminal emulator we're running on supports true colors or not is something you'd want to know as a part of your TUI, and that's a bit of a difficult task.

For some of these the user can even change the lowest 16 colors, the common subset. So, you can't rely on the hue :melting_face:

## Frameworks
It's mostly [ncurses](https://invisible-island.net/ncurses/), [ratatui](https://github.com/ratatui/ratatui), [libtickit](https://www.leonerd.org.uk/code/libtickit/)


## Reference
[So you want to make a TUI...](https://p.janouch.name/article-tui.html): Big thanks to this one. Most of the links were crawled (heh) from here. And this was a really good read.
[Character Encodings and Locales](https://tecnocode.co.uk/2011/08/27/character-encoding-and-locales/)
[Dark Corners Of Unicode](https://eev.ee/blog/2015/09/12/dark-corners-of-unicode/)
