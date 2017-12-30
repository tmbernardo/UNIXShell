#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "list.h"

#define MAX_BUF_LEN 128
#define MAX_ARGS 128
#define MAX_STR_LEN 128

// stores all input information for command
struct parsed_cmd_node {
    struct list_elem elem;
    char *args[MAX_ARGS];
    char *buf;
    int num_args;
    int redirect_pos;
    int cmd_line_number;
};

// initializes a new command node
struct parsed_cmd_node *parsed_cmd_new (int current_line_num) {
    int i;
    struct parsed_cmd_node *pcmd;
    // allocate memory for new command node and user input buffer
    pcmd = (struct parsed_cmd_node *) calloc(1 ,sizeof(struct parsed_cmd_node));
    pcmd->buf = (char *) calloc(MAX_BUF_LEN, sizeof(char));

    for(i = 0; i < MAX_ARGS; i++) {
        pcmd->args[i] = (char *) calloc(MAX_STR_LEN, sizeof(char));
    }
    
    if (pcmd == NULL || pcmd->buf == NULL) {
        printf("parsed_cmd_new(): Cannot malloc. Exiting.\n");
        exit(-1);
    }
    // initialize variables in command node
    pcmd->num_args = 0;
    pcmd->redirect_pos = 0;
    pcmd->cmd_line_number = current_line_num;

    return pcmd;
}

// removes the earliest command in the command history and frees the memory
void remove_earliest_cmd(struct list *cmd_history) {
    int i;
    struct list_elem *e = list_front(cmd_history);
    struct parsed_cmd_node *earliest_node = list_entry(e, struct parsed_cmd_node, elem);

    list_remove(e);
    
    free(earliest_node->buf);
    earliest_node->buf = NULL;
    free(earliest_node);
    earliest_node = NULL;

}

// prints out the prompt to take in userinput
void print_prompt(struct parsed_cmd_node *pcmd)
{
    int rv;
    
    // output line number and prompt
    printf("[%d]", pcmd->cmd_line_number);
    fflush(stdout);
    rv = write(1,"$ ", 2);
    
    if (rv < 0) {
        printf("write() failed.\n");
        exit(-1);
    }
}

// prints out the command history of the last ten commands including the current call
void print_history(struct list *cmd_history, int num_args) {
    int i;
    struct list_elem *e;
    struct parsed_cmd_node *cmd_node;

    if (num_args > 1) {
        // return to shell if history has too many arguments
        printf("history: too many arguments\n");
        return;
    }
    
    // go through linked list starting from the head (earliest command in history)
    for (e = list_begin(cmd_history); e != list_end(cmd_history); e = list_next(e)) {
        cmd_node = list_entry(e, struct parsed_cmd_node, elem);

        printf("%d ", cmd_node->cmd_line_number);
        i = 0;
        while (cmd_node->args[i] != NULL) {
            printf("%s ", cmd_node->args[i]);
            i++;
        }
        printf("\n");
    }
}

// Read in user input
void read_command_line(struct parsed_cmd_node *pcmd)
{
    int rv;
    char *buf = pcmd->buf;
    
    rv = read(0, buf, (MAX_BUF_LEN-1));
    if (rv < 0) {
        printf("read() failed.\n");
        exit(-1);
    }
    buf[rv] = '\0';
}

// strips user input of whitespace and newline so only arguments remain in args
void parse_command_line(struct parsed_cmd_node *pcmd)
{
    char *buf = pcmd->buf;
    int i = 0;

    if ((pcmd->args[i] = strtok(buf, " \n")) != NULL) {
        i = i + 1;
        while ((pcmd->args[i] = strtok(NULL, " \n")) != NULL) {
            i = i + 1;
        }
    }
    pcmd->num_args = i;
}

// copies part of an array to another
void slice_array(char **to_array, char **from_array, int start, int end) {
    memcpy(to_array, from_array + start, (end - start) * sizeof(char*));
}

// does the user want to change the directory?
bool is_change_directory(char **args) {    
    return strcmp(args[0], "cd") == 0;
}

// does the user want to show the command history?
bool is_history(char **args) {
    return strcmp(args[0], "history") == 0;
}

// does the user want to run the last specified command?
bool is_bang(char **args) {
    return args[0][0] == '!';
}

// does the user want to exit the shell?
bool is_exit(char **args) {
    return strcmp(args[0], "exit") == 0;
}

// does the user want to redirect a command's output to a file?
bool is_file_redirection(char **args, int *redirect_pos)
{
    int i;
    bool rv = false;
    
    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            rv = true;
            *redirect_pos = i + 1;
        }
    }

    return rv;
}

// does the user want to pipe the output of one program to another?
bool is_pipe_redirection(char **args, int *redirect_pos) {
    int i;
    bool rv = false;

    for (i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            rv = true;
            *redirect_pos = i + 1;
        }
    }

    return rv;
}

// executes the output of one program to an output file
int exec_file_redirection(char **args, int redirect_pos) {
	pid_t id;
	int fd;
    // allocate space for the program the user wants to run
    char **command = calloc((redirect_pos), sizeof(char*));

    if (command == NULL) {
        printf("exec_file_redirection(): Cannot malloc. Exiting.\n");
        exit(-1);
    }
    // copy the command and arguments before the redirect character to command
    slice_array(command, args, 0, redirect_pos);
    command[redirect_pos - 1] = NULL;
    
    remove(args[redirect_pos]);
    id = fork();

	if (id == 0) {
        //we are in the child
        // close stdout in child
        close(1);
        
        // opens the file (stored in the last argument in args) to redirect output to
        if ((fd = open(args[redirect_pos], O_CREAT | O_WRONLY, 0644)) < 0) {
            printf("cannot open %s\n", args[redirect_pos]);
            exit(1);
        }
        // execute the command and command arguments
        execvp(command[0], command);
        printf("%s: command not found\n", args[0]);
        exit(-1);
	} 

	// we are in the parent
    id = wait(NULL);

    free(command);
    command = NULL;
}

// creates the children processes for pipe
void create_pipe_children(char **process1, char **process2) {
    pid_t id; 
    int fildes[2];
    
    pipe(fildes);
    
    id = fork();

    if (id == 0) {
        // First child generates output to pipe
        // Close stdout
        close(1);

        //Put the write end of the pipe into stdout (1) file descriptor slot.
        dup(fildes[1]);

        // Close write end of pipe.
        close(fildes[1]);        

        // Close read end of pipe.
        close(fildes[0]);

        if (execvp(process1[0], process1) < 0) {
            printf("%s: command not found\n", process1[0]);
            exit(-1);
        }
    }

    id = fork();

    if (id == 0) {
        // Second child gets input from pipe
        // Close stdin.
        close(0);

        // Put the read end of the pipe into stdin (0) file descriptor slot.
        dup(fildes[0]);

        // Close read end of pipe.
        close(fildes[0]);        

        // Close write end of pipe.
        close(fildes[1]);

        if (execvp(process2[0], process2) < 0) {
            printf("%s: command not found\n", process2[0]);
            exit(-1);
        }
    }

    // close file descriptors
    close(fildes[0]);
    close(fildes[1]);
    
    id = wait(NULL);
    id = wait(NULL);
}

// Executes redirection of output from one program as input to another
int exec_pipe_redirection(char **args, int num_args,int redirect_pos) {
    // allocate space for commands to be saved to
    char **process1 = calloc((redirect_pos), sizeof(char*));
    char **process2 = calloc((num_args - redirect_pos + 1), sizeof(char*));

    if (process1 == NULL || process2 == NULL) {
        printf("exec_pipe_redirection: Unable to malloc. Exiting\n");
        exit(-1);
    }

    // take the commands from args and save them in their respective arrays
    slice_array(process1, args, 0, redirect_pos);
    slice_array(process2, args, redirect_pos, num_args);
    process1[redirect_pos - 1] = NULL;
    process2[num_args - redirect_pos] = NULL;

    // pipe output from process1 as input to process2
    create_pipe_children(process1, process2);

    free(process1);
    free(process2);
    process1 = NULL;
    process2 = NULL;
}

// Executes a simple command and command arguments from user input
void exec_simple_command(char **args)
{
    pid_t id;

    id = fork();

    if (id == 0) {
        // assumes path is in args[0] and arguments follow
        execvp(args[0], args);

        // print error message and exit if execlp fails 
        printf("%s: command not found\n",args[0]);
        exit(-1);
    } else {
        // in parent
        id = wait(NULL);
    }
}

// Executes a change in working directory
void exec_change_directory(char **args, int num_args) {
    int rv;
    
    if (num_args == 2) {
        // if a destination is provided
        if ((rv = chdir(args[1])) < 0) {
            // if chdir returns -1 then the directory doesn't exist
            printf("cd: %s: no such file or directory\n", args[1]);
        }
    } else if (num_args == 1) {
        // if no arguments are given
        if ((rv = chdir(getenv("HOME"))) < 0) {
            // if there's no home directory for some odd reason
            printf("cd: %s: no such file or directory\n", args[1]);
        }
    } else {
        printf("cd: too many arguments\n");
    }
}

// checks if bang is a command line number and if there is a match
bool is_line_number(int cmd_history_num, char *bang_command) {
    if (bang_command[0] >= '0' && bang_command[0] <= '9') {
        return cmd_history_num == atoi(bang_command);
    }
    return false;
}

// Execute a previous statement in command history
struct parsed_cmd_node *exec_bang(struct parsed_cmd_node *pcmd, struct list *cmd_history) {
    struct list_elem *e;
    struct parsed_cmd_node *new_node;
    
    int i;
    char **args = pcmd->args;
    // allocate memory for the user requested command
    char *bang_command = calloc(strlen(args[0]) + 1,sizeof(char)); 
    // copy argument to bang_command without ! character
    strncpy(bang_command, &args[0][1], strlen(args[0]) + 1);
    
    if (args[0][1] == '\0') {
        // if there is only one argument return to main    
        return NULL;
    }

    // go through command history from latest commands to earliest (tail to head)
    for (e = list_end(cmd_history); e != list_head(cmd_history); e = list_prev(e)) {
        struct parsed_cmd_node *s = list_entry(e, struct parsed_cmd_node, elem);
        
        // if the command line number or command is found save it to new_node and return
        if (strcmp(s->args[0], bang_command) == 0 || is_line_number(s->cmd_line_number, bang_command)) {
            // create a new command node to save to history
            new_node = parsed_cmd_new(pcmd->cmd_line_number);
            
            for (i = 0; s->args[i] != '\0'; i++) {
                //memcpy(new_node->args[i], s->args[i], sizeof(s->args[i]));
                strcpy(new_node->args[i], s->args[i]);
                //new_node->args[i] = s->args[i];
            }
            new_node->args[i] = '\0';
            new_node->num_args = s->num_args;
            return new_node;
        }
    }

    free(bang_command);
    // if event is not found then return NULL
    printf("%s: event not found\n", args[0]);
    return NULL;
}

/* Takes the parsed command struct and checks for a valid command
 * returns true if the user wants to exit the shell*/
bool validate_command(struct parsed_cmd_node *pcmd, struct list *cmd_history) {
    char **args = pcmd->args;
    struct parsed_cmd_node *bang_node;
    
    // returns to main if there are no args present
    if (pcmd->num_args > 0) {
        // user wants to exit
        if (is_exit(args)) {
            return true;
        }

        if (is_bang(args)) {
            // execute bang
            if ((bang_node = exec_bang(pcmd, cmd_history)) != NULL ) {
                validate_command(bang_node, cmd_history);
            }

        } else if (is_change_directory(args)) {
            // execute change directory
            exec_change_directory(args, pcmd->num_args);

        } else if (is_file_redirection(args, &pcmd->redirect_pos)) {
            // execute file redirection
            exec_file_redirection(args, pcmd->redirect_pos);
        
        } else if (is_pipe_redirection(args, &pcmd->redirect_pos)) {
            // execute pipe redirection
            exec_pipe_redirection(args, pcmd->num_args, pcmd->redirect_pos);
        
        } else if (!is_history(args)) {
            /* execute a simple command */
            exec_simple_command(args);
        }
        
        if (!is_bang(args)) {
            list_push_back(cmd_history, &pcmd->elem);
            if (list_size(cmd_history) > 10) {
                // removes the earliest command in history from list
                remove_earliest_cmd(cmd_history);
            } 
        }

        if (is_history(args)) {
            // prints 10 previous calls inclusive
            print_history(cmd_history, pcmd->num_args); 
        }
        
    }

    return false;
}

int main(int argc, char **argv)
{
    int i;
    bool done = false;
    
    struct list cmd_history;
    struct parsed_cmd_node *pcmd;

    // initialize history and command node
    list_init(&cmd_history);
    pcmd = parsed_cmd_new(0);

    while (!done) {
        
        print_prompt(pcmd);
        read_command_line(pcmd);
        parse_command_line(pcmd);
        
        // validates the user input and executes commands
        done = validate_command(pcmd, &cmd_history);
        // creates a new command node for a new user command
        pcmd = parsed_cmd_new(pcmd->cmd_line_number + 1);
    }

    return 0;
}
