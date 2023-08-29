//*********************************************************
//
// Francisco Alarcon
// Operating Systems
// Programming Project #2 Writing Your Own Shell: hfsh2
// February 20th, 2023
// Instructor: Michael Scherger
//
//*********************************************************


//*********************************************************
//
// Includes and Defines
//
//*********************************************************
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "csapp.h"
#include <unistd.h>


#define STR_EXIT "exit"

//*********************************************************
// Extern Declarations
//*********************************************************
// Buffer state. This is used to parse string in memory...
// Leave this alone.

extern "C"{
    extern char **gettoks();
    typedef struct yy_buffer_state * YY_BUFFER_STATE;
    extern YY_BUFFER_STATE yy_scan_string(const char * str);
    extern YY_BUFFER_STATE yy_scan_buffer(char *, size_t);
    extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
    extern void yy_switch_to_buffer(YY_BUFFER_STATE buffer);
}


//*********************************************************
//
// Global Variables
//
//*********************************************************
using namespace std;
char error_message[30] = "An error has occurred\n";
int toks_size;
char **paths = NULL;
char finalpath[256];
int fd, stdout_fd, stderr_fd;
pid_t parallel_pid = 1;

//*********************************************************
// Type definitions
//*********************************************************
struct job_t {
    pid_t pid;
};

//*********************************************************
//
// Function Prototypes
//
//*********************************************************
void cmdExecute(char **toks, int toks_size);
int builtin_cmd(char **toks, int toks_size);
void sendError();
void do_cd(char **toks, int toks_size);
void init_path(char **toks);
void set_path(char **toks);
int redirect(char **toks);
char **run_parallel(char **toks);


//*********************************************************
//
// Main Function
//
//*********************************************************
int main(int argc, char *argv[])
{
    /* local variables */
    int ii, redirect_flag, parallel_flag;
    char inFile[50];
    char **toks;
    int retval;
    char linestr[256];
    YY_BUFFER_STATE buffer;
    FILE *fp;

    /* initialize local variables */
    ii = 0;
    toks = NULL;
    retval = 0;
    redirect_flag = 0;
    parallel_flag = 0;
    
    /* determine whether interactive mode or batch mode */
    // if 2 arguments, then batch mode, open specified file and read it 
    if (argc == 2)
    {
        strcpy(inFile, argv[1]);

        if((fp = fopen(inFile, "r")) == NULL)
        {
            sendError();
            exit(1);
        }
    }
    /* if there is only one argument, then it is interactive mode: print first prompt*/
    else if (argc == 1)
    {
        fprintf(stdout, "hfsh2> ");
    }
    else
    {
        sendError();
        exit(1);
    }

    // initialize paths array and set default path to /bin
    init_path(toks);
    char default_path[5] = "/bin";
    paths[0] = default_path;

    /*if there is one argument (user inputs "./hfsh2") then take input from stdin, else take input from fp */
    #define STREAM (argc==1)?stdin:fp  


    /**************  MAIN LOOP ****************/
    while(fgets(linestr, 256, STREAM))
    {
        // parallel flag helps identify whether there is commands to run in parallel, it is initialized as 0
        parallel_flag = 0;
        
        // make sure line has a '\n' at the end of it
        if(!strstr(linestr, "\n"))
            strcat(linestr, "\n");
  
        /* get arguments */
        buffer = yy_scan_string(linestr);
        yy_switch_to_buffer(buffer);
        toks = gettoks();
        yy_delete_buffer(buffer);

        if (feof(stdin)) { /* End of file (ctrl-d) */
	        // fflush(stdout);
	        exit(0);
	    }
        if(toks[0] != NULL)
        {
            /* simple loop to to count how many elements */
            for(ii=0; toks[ii] != NULL; ii++){
                if (!strcmp(toks[ii], ">"))
                    redirect_flag = 1;
                if (!strcmp(toks[ii], "&"))
                    parallel_flag = 1;
            }
            toks_size = ii;
            
        /* parse by parallel commands */
        /* if only input is & then ignore */
            if(parallel_flag && toks_size == 1)
                continue;
            else if(parallel_flag)
                toks = run_parallel(toks);
                
            // begin command execution
            if(redirect_flag)
            {
                // if 1, then input is an error
                if (redirect(toks))
                    sendError();
                else
                {
                    /* execute with redirection, and reset stderr and stdout to regular redirection */
                    cmdExecute(toks, toks_size);
                    dup2(stdout_fd, STDOUT_FILENO);
                    dup2(stderr_fd, STDERR_FILENO);
                }
            }
            else
            {
                cmdExecute(toks, toks_size);
            }
        }
        if(!parallel_pid)
            exit(0);
        // print prompt in a loop every time a command is run
        if(argc == 1)
            fprintf(stdout, "hfsh2> ");
    }
    /* return to calling environment */
    return( retval );
}

//*********************************************************

// Execute Commands function

// This function uses the toks array to execute all non-built-in
// commands. It does this by forking a new process in which the 
// child process loops through the paths array trying to find
// for a valid path to execute. if valid path is found, it is 
// executed
// 
// Return Value: Void
// -------------------
// 
// Function Parameters
// -------------------
// toks         char**         Array of user command   
// toks_size    int            size of toks array

// Local Variables
// ----------------
// pid      pid_t   Pid of forked processes
// status   int     holds the status of child processes during wait() system call
// ii       int     counter for the loop
// inPath   int     flag: 1 if path is valid, 0 if path is not valid
// 
//*********************************************************
void cmdExecute(char **toks, int toks_size)
{
    pid_t pid;
    int status;
    int ii;
    int inPath = 0;

    // if not builtin command, fork process
    if(!builtin_cmd(toks, toks_size))
    {
        if((pid = fork()) < 0)
        {
            sendError();
            return;
        } 
        else if (pid) // if parent process, wait until child terminates
        {
            waitpid(pid, &status, 0);
        }
        else if (pid == 0) // if child process, find path and execute if valid
        {
            // iterate through paths array looking for valid path
            for (ii=0; paths[ii] != NULL; ii++)
            {
                strcpy(finalpath, paths[ii]);
                strcat(finalpath, "/");
                strcat(finalpath, toks[0]);

                // access function checks for a valid path, returns 0 if valid
                if(access(finalpath, X_OK) == 0)
                {
                    inPath = 1;
                    break;
                }
            }

            // if valid path, execute the command
            if (inPath)
            {
                if(execvp(finalpath, toks) < 0)
                {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    exit(1);
                }
            }
            else
            {
                // if path  is not valid then give error and exit child process
                sendError();
                exit(0);
            }
        }
    }
}

//*********************************************************

// Built-in commands function

// This function checks whether he first element of the user's input
// is a built-in command or not. If it is, it calls the appropriate
// function to execute the command and returns 1. Otherwise,
// it returns 0.
// 
// Return Value:
// -----------------
// int      0 / 1 if built-in command
// 
// Function Parameters
// -------------------
// toks         char**         Array of user command   
// toks_size    int            size of toks array
// 
//*********************************************************
int builtin_cmd(char **toks, int toks_size)
{
    if(!strcmp(toks[0], "cd"))
    {
        do_cd(toks, toks_size);
        return 1;
    }
    else if (!strcmp(toks[0], "path"))
    {
        init_path(toks);
        set_path(toks);
        return 1;
    }
    else if (!strcmp(toks[0], "exit"))
    {
        if (toks_size == 1)
            exit(0);
        else 
        {
            sendError();
            return 1;
        }
    }
    return 0;
}

//*********************************************************
// 
// do_cd function

// This function takes care of the "cd" command. 
// it checks for the input, if it has no arguments
// or more than 1 argument, then it sends error and
// returns to the execution of the shell.
// If it input is correct, then it changes directory
//
// 
// Return Value: Void
// -------------------
// 
// Function Parameters
// -------------------
// toks         char**         Array of user command   
// toks_size    int            size of toks array
// 
//*********************************************************
void do_cd(char **toks, int toks_size)
{
    if (toks_size != 2)
        sendError();
    else if(chdir(toks[1]) < 0)
        sendError();
}

//*********************************************************
// 
// Send Error Message Function
// 
// This function sends the error message to stderr
//
// Return Value: Void
// -------------------
// 
//*********************************************************
void sendError()
{
    write(STDERR_FILENO, error_message, strlen(error_message));
    return;
}

//*********************************************************
// 
// Initialize Path function
// 
// This function initializes the paths array, which is an array 
// of strings (pointer to an array of char pointers). Allocates
// memory for said array.
//
// Return Value: Void
// -------------------
// 
// Function Parameters
// -------------------
// toks         char**      Array of user command   
// 
// Local Variables
// ----------------
// ii       int     loop iteration variable
// jj       int     loop iteration variable
//*********************************************************
void init_path(char **toks)
{
    int ii, jj;
    // allocate memory for array
    paths = (char**)malloc(sizeof(char*) * 30);
    for (jj = 1; jj < 30; ++jj)
        paths[jj-1] = (char *)malloc(sizeof(toks[jj]));
}

//*********************************************************
// 
// Set Path function
// 
// This function is called when the user inputs 
// the "path" built-in command. It adds the paths
// specified by user to the paths array.
//
// Return Value: Void
// -------------------
// 
// Function Parameters
// -------------------
// toks         char**       Array of user command   
// 
// Local Variables
// ----------------
// ii       int     loop iteration variable
//*********************************************************
void set_path(char **toks)
{
    int ii;
    for(ii=1; ii < toks_size; ii++)
    {
        strcpy(paths[ii-1], toks[ii]);
        // printf("%s\n", paths[ii-1]);
    }
}

//*********************************************************
// 
// Redirect output/error function
// 
// This function deals with redirection. If there is a >
// character in the input, then the output of the 
// preceding commands and arguments are redirected to the specified 
// file. If there is a 2> then standard error is redirected to 
// specified file.
//
// Return Value: 
// -------------------
// int      0 if correct input, 1 if invalid input.
// 
// Function Parameters
// -------------------
// toks         char**       Array of user command   
// toks_size    int          size of toks array
// 
// Local Variables
// ----------------
// ii          int       loop iteration variable
// file_name   char*     the filename specified after the redirection symbol
// mode         mode_t   modes used in creat() system call, sets file descriptor
//*********************************************************
int redirect(char **toks)
{
    int ii;
    char *file_name;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    
    // if first element is ">", return 1 = invalid input
    if (!strcmp(toks[0], ">"))
        return 1;

    for(ii=0; toks[ii] != NULL; ii++)
    {
        /* if > is found */
        if (!strcmp(toks[ii], ">"))
        {
            /* if element after ii is not null, valid input, get file_name */
            if(toks[ii+1] != NULL)
                file_name = toks[ii+1]; 
            else //if there is no argument after ">", then it's an error.
                return 1;
            // if there is more arguments after file name, it's an error. 
            if(toks[ii+2] != NULL)
                return 1;

            /* stdout and stderr will be sent to respective file descriptors*/
            stdout_fd = dup(STDOUT_FILENO);
            stderr_fd = dup(STDERR_FILENO);
            
            // creat() system call opens (creates if not exist) a fd.
            fd = creat(file_name, mode);

            /* if 2>, redirect stderr, else redirect stdout */
            if (!strcmp(toks[ii-1], "2"))
            {
                dup2(fd, STDERR_FILENO);
                toks[ii-1] = NULL;
            }
            else
            {
                dup2(fd, STDOUT_FILENO);
                toks[ii] = NULL; 
            }           
            break;
        }
    }
    return 0;
}

//*********************************************************

// Parallel Commands function

// This function deals with parallel commands if the "&"" character is found.
// It iterates through a loop checking every token. if the token is not a "&", then
// it adds it to the "newtoks" array, which will execute once we hit a "&".
// when we hit a "&", a process is forked and the newtoks array is returned 
// back to where this function was called to continue with the execution of 
// the command.
// 
// 
// Return Value: 
// -------------------
// toks     char**      array with command and arguments to be executed
// 
// Function Parameters
// -------------------
// toks         char**         Array of user command   
// 
// Local Variables
// ----------------
// newtoks  char**  array that holds each command and its arguments
// status   int     holds the status of child processes during wait() system call
// ii, jj   int     counter for the loop
// size     int     size of toks array
// index    int     keeps track of the index of newtoks array
//*********************************************************
char **run_parallel(char **toks)
{
    int ii, jj, status, index;
    char **newtoks = NULL;
    int size = toks_size;

// allocates memory for newtoks array
    newtoks = (char**)malloc(sizeof(char*) * 30);
    for (jj = 1; jj < 30; ++jj)
    {
        newtoks[jj-1] = (char *)malloc(sizeof(toks[jj]));
    }

// index starts at 0 and is reset to 0 everytime a new command is found
    index = 0;

// if last token is "&", ignore it
    if (!strcmp(toks[size-1], "&"))
    {
        toks[size-1] = NULL;
        size--;
    }

    for(ii = 0; ii <= size; ii++)
    {
        // if & or NULL
        if (toks[ii] == NULL || !strcmp(toks[ii], "&"))
        {
            // if forking error, send error
            if((parallel_pid = fork()) < 0)
            {
                sendError();
            }
            // if child, break out of loop and execute newtoks
            if (parallel_pid == 0)
            {
                if (toks[ii] == NULL)
                {
                    exit(0);
                }
                
                newtoks[index] = NULL;
                toks = newtoks;
                break;
            }
            // if parent
            else if (parallel_pid)
            {
                if (toks[ii] == NULL)
                {
                    newtoks[index] = NULL;
                    toks = newtoks;

                    return toks;
                }
                // if &, then reset index
                else if(!strcmp(toks[ii], "&"))
                {
                    newtoks[0] = NULL;
                    index = -1;
                }
                waitpid(parallel_pid, &status, 0);
            }
        }
        //  if not &
        else if (strcmp(toks[ii], "&"))
        {
            newtoks[index] = toks[ii];
        }
        index++;
    }
    return toks;
}