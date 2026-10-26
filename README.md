# Quash (Quite a Shell) Project Report
Jamarri Mezier
October 26, 2026

## 1. Project Overview and Objectives
The objective of this project was to design and implement a functional command-line interface (CLI) called **Quash** (Quite a Shell) in C. A shell is a critical piece of system software that acts as an interactive command interpreter, bridging the gap between the user and the underlying operating system kernel. 

Through a phased implementation approach, Quash was equipped with standard Unix shell capabilities. These included parsing user input, executing built-in commands, managing foreground and background process execution via forking, handling asynchronous OS signals, enforcing execution timeouts, and manipulating file descriptors for I/O redirection. This project provided deep, hands-on experience with the POSIX API, system calls, and the Linux process lifecycle, demonstrating the complex under-the-hood mechanics of production-grade shells like `bash`, `zsh`, and `csh`.

## 2. Architecture and Main Loop Design
At its core, Quash operates as an event-driven program governed by an infinite `while(true)` loop. The shell's architecture is divided into three primary phases per iteration: fetching input, parsing/tokenizing, and execution.

*   **Prompt Generation:** In each iteration, the shell retrieves the current working directory using `getcwd()` to dynamically construct and print the prompt (e.g., `/home/workspace> `). This provides immediate visual feedback to the user regarding their current location in the file system.
*   **Input Handling:** The shell blocks and waits for user input via `fgets()`, reading up to a predefined `MAX_COMMAND_LINE_LEN`. Using `fgets()` over `gets()` ensures buffer overflow protection. If the user presses `Ctrl+D`, the shell detects an End-of-File (EOF) state on the standard input stream and exits gracefully.
*   **Tokenization Phase:** Once a raw string is received, it must be broken down into an array of arguments that `execvp()` can understand. We utilized `strtok()` to split the string based on space delimiters. A `while` loop iterates through the string, extracting tokens and placing them into the `arguments` array while carefully tracking the argument count to prevent out-of-bounds memory access.

## 3. Implementation Details by Task

### Task 1: Built-In Commands and Variable Expansion
Before attempting to fork new processes, the shell must check if the provided command is a "built-in." Built-ins are commands that must be executed directly within the parent shell's own process space. For example, if `cd` were executed as an external process, the child process would change its directory and immediately die, leaving the parent shell's directory unchanged.
*   **`cd` (Change Directory):** Implemented using the `chdir()` system call. We pass the second argument (`arguments[1]`) to `chdir()`. If no argument is provided, error handling prevents a crash.
*   **`pwd` (Print Working Directory):** Re-calls `getcwd()` and prints the absolute path of the current directory.
*   **`echo`:** Iterates through the `arguments` array starting at index 1, printing each token followed by a space.
*   **Environment Management (`env` & `setenv`):** `env` traverses the global `extern char **environ` pointer, printing all system variables until it hits a NULL pointer. `setenv` utilizes the standard `setenv(name, value, overwrite)` function with the overwrite flag enabled to dynamically modify the shell's environment variables.
*   **Variable Expansion:** During the tokenization phase, the shell checks if any token begins with a `$` character. If it does, the shell shifts the pointer by one (`token + 1`) to strip the `$` and passes the remaining string to `getenv()`. The token is then replaced with the variable's actual value before execution. If `getenv()` returns NULL (the variable doesn't exist), it is replaced with an empty string to prevent segmentation faults.

### Tasks 2 & 3: Process Forking and Background Execution
For non-built-in commands (like `ls`, `cat`, or `grep`), the shell relies on the classic **fork-exec-wait** pattern.
*   **Process Creation (`fork`):** The shell uses `fork()` to clone itself. This creates a child process with a unique Process ID (PID) but an identical memory space. `fork()` returns `0` to the child and the child's PID to the parent.
*   **Execution (`execvp`):** Inside the child process, `execvp()` is called. `execvp` completely replaces the child's current memory image with the new program specified by the user. The `p` in `execvp` is crucial, as it automatically searches the system's `$PATH` environment variable for the executable, eliminating the need for the user to type absolute paths (e.g., `/bin/ls`).
*   **Foreground Execution (Task 2):** If the command is a standard foreground process, the parent shell calls `waitpid(pid, &status, 0)`. This suspends the parent shell's execution until the specific child process terminates, ensuring the prompt is not redrawn prematurely.
*   **Background Execution (Task 3):** During tokenization, the shell checks if the final argument is an ampersand (`&`). If found, a `background` boolean flag is set to true, and the `&` is replaced with `NULL` (so it isn't passed to `execvp`). When `background == true`, the parent process completely bypasses the `waitpid()` call. Instead, it immediately prints a confirmation message with the child's PID and redraws the prompt, allowing the user to continue issuing commands while the child process runs asynchronously.

### Task 4: Signal Handling (`SIGINT`)
A robust CLI must gracefully handle asynchronous interrupts. By default, pressing `Ctrl+C` sends a `SIGINT` (Signal Interrupt) to the foreground process group. If unhandled, this would kill the shell entirely. 
*   To prevent this, `signal(SIGINT, SIG_IGN)` is called at the very beginning of `main()`, instructing the parent shell to ignore the interrupt. 
*   However, child processes inherit their parent's signal dispositions. If a user runs a long command (like `sleep 100`) in the foreground, they need to be able to kill it with `Ctrl+C`. To fix this, `signal(SIGINT, SIG_DFL)` is explicitly called inside the `pid == 0` child block just before `execvp()`, restoring the default termination behavior exclusively for the executing command.

### Task 5: Long-Running Process Timeout
To protect the shell from hanging indefinitely on stalled or infinite-looping foreground processes, a 10-second timeout mechanism was engineered. 
*   Instead of using a blocking `waitpid()` call or setting complex global signal handlers with `alarm()`, the parent process uses a non-blocking polling loop.
*   The parent enters a `while` loop that calls `waitpid(pid, &status, WNOHANG)`. The `WNOHANG` flag instructs the kernel to return `0` immediately if the child is still running, rather than pausing the parent. 
*   The loop includes a `sleep(1)` call and increments an `elapsed_time` counter. If the child finishes naturally within 10 seconds, `waitpid` returns the child's PID, and the loop breaks.
*   If the counter hits 10 seconds and `waitpid` still returns `0`, the parent issues a `kill(pid, SIGKILL)` command to forcibly terminate the rogue process. It then issues one final `waitpid()` to clean up the resulting zombie process from the system process table.

### Task 6: I/O Redirection
Standard input (`stdin`) and output (`stdout`) manipulation allows commands to interface with files rather than the terminal screen. Unix treats everything as a file, utilizing a File Descriptor (FD) table where `0` is stdin, `1` is stdout, and `2` is stderr.
*   Before executing `execvp`, the child process scans its argument array for `<` (input) or `>` (output) symbols.
*   **Output Redirection (`>`):** The shell uses `open()` with `O_WRONLY | O_CREAT | O_TRUNC` flags. This opens the file for writing, creates it if it doesn't exist, and truncates it (wipes previous contents) if it does. `dup2(fd, STDOUT_FILENO)` is then used to clone the file descriptor into slot `1`. 
*   **Input Redirection (`<`):** The shell opens the target file with `O_RDONLY`. It then uses `dup2(fd, STDIN_FILENO)` to route the file's contents into slot `0`.
*   After `dup2` completes, `close(fd)` is called to prevent file descriptor leaks. Finally, the redirection symbols and filenames are removed from the `arguments` array by setting their index to `NULL`, ensuring `execvp()` only attempts to run the core command.

## 4. Conclusion and Design Retrospective
The development of Quash successfully demonstrates the core mechanics of a Unix-like shell. By combining process control (`fork`, `execvp`, `waitpid`), signal management, and file descriptor manipulation (`dup2`), the shell provides a stable, interactive environment capable of handling standard command-line tasks safely and efficiently. 

One of the primary challenges overcome during development was managing the strict separation of state between parent and child processes. Understanding exactly when memory is cloned (at the `fork`) and when it is replaced (at the `exec`) was critical for placing the I/O redirection logic in the correct scope. Ultimately, the Quash project served as an invaluable bridge between high-level C programming and low-level operating system architecture.
