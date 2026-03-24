# SimleSHell

A lightweight Unix-style shell written in C for CS210.  
It supports core command execution, useful built-in commands, aliases, and persistent command history.

## Features

- Run external programs by searching `PATH`
- Built-in commands:
  - `cd`
  - `exit [code]`
  - `getpath`
  - `setpath <path>`
  - `history`
  - `alias [name command...]`
  - `unalias <name>`
- Command history:
  - Stores up to the last 20 commands in memory
  - Persists history to `~/.hist_list`
  - Supports history execution with:
    - `!!` (last command)
    - `!n` (run command number `n`)
    - `!-n` (run command from `n` steps back)
- Multiple commands in one line using `;`

## Project Structure

- `shell.c` - Main shell loop, parsing, built-ins, external command execution, history
- `alias.c` - Alias map implementation (set/get/remove, encode/decode)
- `alias.h` - Alias interfaces and shared structs

## Build

Compile with GCC on Linux/WSL:

```bash
gcc -Wall -Wextra -std=c11 shell.c alias.c -o shell
```

## Run

```bash
./shell
```

## Usage Examples

```bash
$ getpath
$ setpath /usr/bin:/bin
$ cd /tmp
$ alias ll ls -la
$ ll
$ history
$ !!
$ !5
$ !-2
```

## Notes

- Path handling uses `:` as the separator (Unix-style `PATH` format).
- History file location is based on the `HOME` environment variable.
- This shell is a learning project and does not implement advanced features like pipes, redirection, or job control.
