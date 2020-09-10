/* 
 * Main source code file for lsh shell program
 *
 * You are free to add functions to this file.
 * If you want to add functions in a separate file 
 * you will need to modify Makefile to compile
 * your additional functions.
 *
 * Add appropriate comments in your code to make it
 * easier for us while grading your assignment.
 *
 * Submit the entire lab1 folder as a tar archive (.tgz).
 * Command to create submission archive: 
      $> tar cvf lab1.tgz lab1/
 *
 * All the best 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <wait.h>
#include <fcntl.h>
#include <errno.h>
#include "parse.h"
#include "unistd.h"

#define TRUE 1
#define FALSE 0

int * children;
void KillChildren(int);

void RunCommand(int, Command *);

void DebugPrintCommand(int, Command *);

void PrintPgm(Pgm *);

void stripwhite(char *);

char** ParseInput(char*);

int main(void) {
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD,SIG_IGN);
    Command cmd;
    int parse_result;

    while (TRUE) {
        char *line;
        line = readline("> ");

        /* If EOF encountered, exit shell */
        if (!line) {
            break;
        }
        /* Remove leading and trailing whitespace from the line */
        stripwhite(line);
        /* If stripped line not blank */
        if (*line) {
            add_history(line);
            parse_result = parse(line, &cmd);
            RunCommand(parse_result, &cmd);
        }

        /* Clear memory */
        free(line);
    }
    return 0;
}

int CountCommands(Pgm* pgm) {
    int counter = 0;
    while (pgm != NULL) {
        counter++;
        pgm = pgm->next;
    }
    return counter;
}

void handle_file_error() {
    switch (errno) {
        case EACCES:
            printf("Access denied\n");
            break;
        case EISDIR:
            printf("File is a directory\n");
            break;
        case ENOENT:
            printf("No such file\n");
            break;
        default:
            printf("Could not open file\n");
            break;
    }
}

void handle_directory_error() {
    switch (errno) {
        case EACCES:
            printf("Permission denied\n");
            break;
        case ENOENT:
            printf("No such path\n");
            break;
        case ENOTDIR:
            printf("Not a directory\n");
            break;
        case EFAULT:
            printf("Invalid argument\n");
            break;
        default:
            printf("Could not change working directory (%i)\n", errno);
            break;
    }
}

void handle_command(char** command) {
    if (strcmp("cd", command[0]) == 0) {
        int status = chdir(command[1]); // TODO check length
        if (status == -1) {
            handle_directory_error();
        }
    } else {
        execvp(command[0], command);
        switch (errno) {
            case ENOENT:
                printf("Could not find executable: %s\n", command[0]);
                break;
            default:
                printf("Failed to execute: %s", command[0]);
                break;
        }
    }
}

#define BUFFERSIZE 80

/* Execute the given command(s). */
void RunCommand(int parse_result, Command *cmd) {
    int command_counter = CountCommands(cmd->pgm);
    __pid_t* command_pids = malloc(command_counter * sizeof(__pid_t));
    int curr_command_index = 0;
    Pgm *pgm = cmd->pgm;

    int last_in = STDIN_FILENO; // The input for the last command in the chain. (left-most command)
    if (cmd->rstdin) {
        int input = open(cmd->rstdin, O_RDONLY);
        if (input == -1) {
            handle_file_error();
            return; // TODO: Check for memory leaks.
        } else {
            last_in = input;
        }
    }

    int child_in = STDIN_FILENO;
    int child_out = STDOUT_FILENO;
    if (cmd->rstdout) {
        int out_pid = creat(cmd->rstdout, S_IRGRP | S_IRUSR | S_IWUSR | S_IWGRP | S_IROTH);
        if (out_pid == -1) {
            handle_file_error();
            return; // TODO: Check for memory leaks.
        } else {
            child_out = out_pid;
        }
    }

    while (pgm != NULL) { // loop trough commands (right to left)
        char** command = pgm->pgmlist;
        pgm = pgm->next;
        int on_last_command = pgm == NULL;

        int pipe_descriptor[2];
        if (!on_last_command) {
            int status = pipe(pipe_descriptor);
            if (status == -1) {
                printf("Pipe failed");
                // TODO: Make sure we don't have any zombies.
                exit(-1);
            } else {
                child_in = pipe_descriptor[0];
            }
        } else { // On last command
            child_in = last_in;
        }

        if (strcmp("exit", command[0]) == 0) {
            // TODO: Cleanup zombies
            exit(0);
        }
        __pid_t child = fork();
        if (child == 0) { // In child
            if (!on_last_command && pipe_descriptor[1] != STDOUT_FILENO) {
                close(pipe_descriptor[1]);
            }

            if (child_in != STDIN_FILENO) {
                dup2(child_in, STDIN_FILENO);
                close(child_in);
            }

            if (child_out != STDOUT_FILENO) {
                dup2(child_out, STDOUT_FILENO);
                close(child_out);
            }

            handle_command(command);
            exit(0);
        } else { // In parent
            if (child_in != STDIN_FILENO) {
                close(child_in);
            }

            if (child_out != STDOUT_FILENO) {
                close(child_out);
            }

            command_pids[curr_command_index] = child;
            if (!on_last_command) {
                child_out = pipe_descriptor[1];
            }
        }

        curr_command_index++;
    }

    if (!cmd->background) {
        children = command_pids;
        signal(SIGINT, KillChildren);
        for (int i = 0; i < command_counter; i++) {
            int *exitcode = 0;
            waitpid(command_pids[i], exitcode, WUNTRACED);
            // TODO do something with exitcode
        }
    }
    signal(SIGINT, SIG_IGN);
    free(command_pids);
}

void KillChildren(int status) {
    for (int i = 0; i < sizeof(children)/sizeof(__pid_t); i++) {
        kill(children[i], SIGKILL);
    }
    printf("\n");
}



/* 
 * Print a Command structure as returned by parse on stdout. 
 * 
 * Helper function, no need to change. Might be useful to study as inpsiration.
 */
void DebugPrintCommand(int parse_result, Command *cmd) {
    if (parse_result != 1) {
        printf("Parse ERROR\n");
        return;
    }
    printf("------------------------------\n");
    printf("Parse OK\n");
    printf("stdin:      %s\n", cmd->rstdin ? cmd->rstdin : "<none>");
    printf("stdout:     %s\n", cmd->rstdout ? cmd->rstdout : "<none>");
    printf("background: %s\n", cmd->background ? "true" : "false");
    printf("Pgms:\n");
    PrintPgm(cmd->pgm);
    printf("------------------------------\n");
}


/* Print a (linked) list of Pgm:s.
 * 
 * Helper function, no need to change. Might be useful to study as inpsiration.
 */
void PrintPgm(Pgm *p) {
    if (p == NULL) {
        return;
    } else {
        char **pl = p->pgmlist;

        /* The list is in reversed order so print
         * it reversed to get right
         */
        PrintPgm(p->next);
        printf("            * [ ");
        while (*pl) {
            printf("%s ", *pl++);
        }
        printf("]\n");
    }
}


/* Strip whitespace from the start and end of a string. 
 *
 * Helper function, no need to change.
 */
void stripwhite(char *string) {
    register int i = 0;

    while (isspace(string[i])) {
        i++;
    }

    if (i) {
        strcpy(string, string + i);
    }

    i = strlen(string) - 1;
    while (i > 0 && isspace(string[i])) {
        i--;
    }

    string[++i] = '\0';
}
