#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#define MAX_COMMAND_LINE_LEN 1024
#define MAX_COMMAND_LINE_ARGS 128

char prompt[] = "> ";
char delimiters[] = " \t\r\n";
extern char **environ;



int main() {
    // Task 4: Ignore SIGINT (Ctrl-C) in the parent shell
    signal(SIGINT, SIG_IGN);

    // Stores the string typed into the command line.
    char command_line[MAX_COMMAND_LINE_LEN];
    char cmd_bak[MAX_COMMAND_LINE_LEN];
  
    // Stores the tokenized command line input.
    char *arguments[MAX_COMMAND_LINE_ARGS];
    
    while (true) {
        // 1. Print the Prompt
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s%s", cwd, prompt);
        } else {
            perror("getcwd failed");
            printf("%s", prompt);
        }
        fflush(stdout); // Ensure the prompt prints immediately

        // 2. Read the input from the user
        // If fgets fails (e.g., EOF), break out of the loop
        if ((fgets(command_line, MAX_COMMAND_LINE_LEN, stdin) == NULL) && ferror(stdin)) {
            perror("fgets error");
            exit(1);
        }

        // If the user presses Ctrl+D (EOF), exit gracefully
        if (feof(stdin)) {
            printf("\n");
            exit(0);
        }

        // 3. Tokenize the input and expand variables
        int arg_count = 0;
        char *token = strtok(command_line, delimiters);

        while (token != NULL && arg_count < MAX_COMMAND_LINE_ARGS - 1) {
            // Check if the token is an environment variable starting with '$'
            if (token[0] == '$') {
                // Get the variable name (skip the '$') and lookup its value
                char *env_val = getenv(token + 1);
                if (env_val != NULL) {
                    arguments[arg_count++] = env_val;
                } else {
                    arguments[arg_count++] = ""; // Variable not found, replace with empty string
                }
            } else {
                arguments[arg_count++] = token;
            }
            token = strtok(NULL, delimiters);
        }
        // Null-terminate the argument array
        arguments[arg_count] = NULL;

        // Skip to next iteration if no command was entered
        if (arg_count == 0) {
            continue;
        }

        // Task 3: Check for background execution '&'
        bool background = false;
        if (strcmp(arguments[arg_count - 1], "&") == 0) {
            background = true;
            arguments[arg_count - 1] = NULL; // Remove '&' from arguments so execvp doesn't choke on it
            arg_count--; // Update argument count
        }

        // 4. Implement Built-In Commands
        if (strcmp(arguments[0], "exit") == 0) {
            exit(0);
        } 
        else if (strcmp(arguments[0], "cd") == 0) {
            if (arguments[1] != NULL) {
                if (chdir(arguments[1]) != 0) {
                    perror("cd failed");
                }
            } else {
                printf("cd: missing argument\n");
            }
        } 
        else if (strcmp(arguments[0], "pwd") == 0) {
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                printf("%s\n", cwd);
            } else {
                perror("pwd failed");
            }
        } 
        else if (strcmp(arguments[0], "echo") == 0) {
            // Loop through arguments starting at index 1 and print them
            for (int j = 1; arguments[j] != NULL; j++) {
                printf("%s ", arguments[j]);
            }
            printf("\n");
        } 
        else if (strcmp(arguments[0], "env") == 0) {
            // Traverse the global 'environ' array and print each pair
            for (char **env = environ; *env != 0; env++) {
                printf("%s\n", *env);
            }
        } 
        else if (strcmp(arguments[0], "setenv") == 0) {
            // Requires two arguments: variable name and value
            if (arguments[1] != NULL && arguments[2] != NULL) {
                if (setenv(arguments[1], arguments[2], 1) != 0) {
                    perror("setenv failed");
                }
            } else {
                printf("setenv: missing arguments (usage: setenv <var> <value>)\n");
            }
        } 
        else {
            // Task 2 & 3: Process Creation and Execution
            pid_t pid = fork();
            
            if (pid < 0) {
                // Fork failed
                perror("fork failed");
            } else if (pid == 0) {
                // Task 4: Restore default Ctrl-C behavior for the child process
                signal(SIGINT, SIG_DFL);

                // Task 6: I/O Redirection
                for (int i = 0; arguments[i] != NULL; i++) {
                    if (strcmp(arguments[i], ">") == 0) {
                        // Output redirection: open for writing, create if it doesn't exist, truncate to 0 length
                        int fd = open(arguments[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd < 0) {
                            perror("open output file failed");
                            exit(1);
                        }
                        dup2(fd, STDOUT_FILENO); // Redirect stdout (1) to the file
                        close(fd);
                        arguments[i] = NULL; // Cut off the arguments array here so execvp ignores ">" and the filename
                        break;
                    } 
                    else if (strcmp(arguments[i], "<") == 0) {
                        // Input redirection: open for reading only
                        int fd = open(arguments[i+1], O_RDONLY);
                        if (fd < 0) {
                            perror("open input file failed");
                            exit(1);
                        }
                        dup2(fd, STDIN_FILENO);  // Redirect stdin (0) to the file
                        close(fd);
                        arguments[i] = NULL; // Cut off the arguments array here
                        break;
                    }
                }

                // Child process: execute the command
                if (execvp(arguments[0], arguments) == -1) {
                    perror("execvp failed");
                    exit(1); 
                }
            } else {
                // Parent process (the shell)
                if (background) {
                    // Task 3: Background process, do NOT wait
                    printf("[Background process started with PID %d]\n", pid);
                } else {
                    // Task 5: Foreground process with 10-second timeout
                    int status;
                    int time_elapsed = 0;
                    pid_t result = 0;
                    
                    // Loop for up to 10 seconds
                    while (time_elapsed < 10) {
                        // WNOHANG tells waitpid to return 0 immediately if the child is still running
                        result = waitpid(pid, &status, WNOHANG);
                        
                        if (result == pid) {
                            // Process finished naturally within the time limit
                            break;
                        } else if (result == -1) {
                            perror("waitpid failed");
                            break;
                        }
                        
                        // Wait 1 second before checking again
                        sleep(1);
                        time_elapsed++;
                    }
                    
                    // If result is still 0, the process is still running after 10 seconds
                    if (result == 0) {
                        printf("\n[Process %d exceeded 10 seconds limit. Terminating...]\n", pid);
                        // Send the SIGKILL signal to forcibly terminate the child process
                        kill(pid, SIGKILL);
                        // Call waitpid one last time to clean up the terminated (zombie) process
                        waitpid(pid, &status, 0); 
                    }
                }
            }
        }
    }
    return 0;
}