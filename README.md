


Implement a program called `exec_lines` that reads a stream of lines (sequences of bytes terminated by `\n`) from standard input, using a configurable buffer size, and executes each line as if it were a command entered in the terminal.

To simplify the problem, assume that:

* The only control character present in the input is `\n`.
* The only token separator is a single space character (no tabs).
* Each line has a maximum allowed length. If a line exceeds this limit, the program must report an error and terminate execution.

Each input line may include a redirection or a pipe. Therefore, supported command formats are:

```
command options
command options < input_file
command options > output_file
command options >> output_file
command1 options1 | command2 options2
```

Where `command` refers to any executable program.

### Examples

```
cat my_file
wc -l < input_file
ls -la > output_file
df -h -u >> output_file
ls -l -a | wc -l
```

---

## Usage

```
./exec_lines [-b BUF_SIZE] [-l MAX_LINE_SIZE] [-p NUM_PROCS]
```

### Options

* **`-b BUF_SIZE`**
  Size of the input buffer used to read from standard input.
  Default value: `16`.

* **`-l MAX_LINE_SIZE`**
  Maximum allowed length of an input line.
  Default value: `32`.
  If a line exceeds this limit, the program must print an error message and terminate.

* **`-p NUM_PROCS`**
  Maximum number of commands executed concurrently.
  Default value: `1`.

---

## Execution Model

* If `NUM_PROCS = 1`, commands are executed **sequentially**:
  the program waits for each command to finish before starting the next one.

* If `NUM_PROCS = N`, the program executes up to **N commands in parallel**:

  * The first N lines are launched immediately (if available).
  * As soon as a process finishes, a new command is started.
  * This ensures that at most N processes are running concurrently at any time.


