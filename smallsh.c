/* Max Goldstein
* CS 374: Operating Systems 1
* Assignment 3: Smallsh
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

// globals
int statusValue;
int signalValue = 0;
int foregroundMode = 0;
struct sigaction SIGINT_action;
struct sigaction SIGTSTP_action;

/* SIGTSTP handler
 * Prints message (not in requirements, but helpful for testing)
*/
void handle_SIGINT(int signo){
	char* message = " Caught SIGINT\n";
	write(STDOUT_FILENO, message, 15);
}

/* SIGTSTP handler
 * Toggles foreground-only mode and prints message
*/
void handle_SIGTSTP(int signo){
    if(foregroundMode == 0)
    {
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
	    write(STDOUT_FILENO, message, 50);
        foregroundMode = 1;
    }
    else
    {
        char* message = "\nExiting foreground-only mode\n";
	    write(STDOUT_FILENO, message, 30);
        foregroundMode = 0;
    }
    
}

/* Expansion of varaible $$ function
 * Uses strstr to split string containing $$ into halves
 * Uses sprintf to write formatted output with two string halves surrounding PID replacing $$
*/
char* expandify(char* command)
{
    pid_t getpid(void);

    char buffer[sizeof(command)+sizeof(getpid())];

    char* beforePID = strdup(command);

    char* afterPID = strstr(beforePID,"$$");
    *afterPID = '\0';

    // afterPID+2 cuts off $$ from string
    sprintf(buffer, "%s%d%s", beforePID, getpid(), afterPID+2);
    char* returnBuffer = strdup(buffer);
    free(beforePID);

    return returnBuffer;
}

/* Built-in exit command function
 * Exits
*/
void exitCommand()
{
    exit(0);
}

/* Built-in status command function
 * Prints last status saved in global
 * If last command was terminated with a signal it prints that too
*/
void statusCommand()
{
    printf("exit value %d\n", statusValue);
    if(signalValue != 0)
    {
        printf("exit signal %d\n", signalValue);
    }
    fflush(stdout);
}

/* Built-in cd command function
 * If only one argument (cd) passed, cd to home directory
 * Else cd to path, and print error if invalid
*/
void cdCommand(int arguments, char* path)
{
    if(arguments == 1)
    {
        chdir(getenv("HOME"));
    }
    else
    {        
        if(chdir(path) != 0)
        {
            printf("chdir to %s failed, try again\n", path);
            fflush(stdout);
        }
    }
}

/* Function to handle forking, exec, and foreground/background commands
 * First scans command for &, <, and >, and sets appropriate varaibles
 * Then forks child process, and redirects IO if needed and runs exec
 * The parent waits for completion if child is a foregound process and sets status
 * Then checks for prior backround process completion
 * If the child is a background process it prints the PID
*/
void forkCommand(char* commandList[2048], int counter)
{
    pid_t spawnpid = -5;
	int childStatus;
    char* outPath = NULL;
    char* inPath = NULL;
    int targetFD = -5;
    int sourceFD = -5;
    int background = 0;

    // check if last argument in command is & and nullify it
    if(commandList[counter-1] != NULL)
    {
        if(strcmp(commandList[counter-1], "&") == 0)
        {
            // if foreground-only mode is on, ignore
            if(foregroundMode == 1)
            {
                background = 0;
                commandList[counter-1] = NULL;
            }
            else
            {
                background = 1;
                commandList[counter-1] = NULL;
            }
        }
    }

    // two loops to scan command for < and >
    // if found, set path to argument after < or > and nullify those arguments
    for(int i = 0; i < counter; i++)
    {
        if(commandList[i] != NULL)
        {
            if(strcmp(commandList[i], "<") == 0)
            {
                inPath = commandList[i+1];
                commandList[i] = NULL;
                commandList[i+1] = NULL;
                break;
            }
        }
    }
    for(int i = 0; i < counter; i++)
    {
        if(commandList[i] != NULL)
        {
            if(strcmp(commandList[i], ">") == 0)
            {
                outPath = commandList[i+1];
                commandList[i] = NULL;
                commandList[i+1] = NULL;
                break;
            }
        } 
    }

    spawnpid = fork();
    switch (spawnpid)
    {

    // fork failure, should never happen
    case -1:
        perror("fork() failed");
        fflush(stdout);
        exit(1);
        break;

    // child process
    case 0:
        // if background command IO is not redirected, it should go to /dev/null
        if(background == 1)
        {
            targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0640);
            sourceFD = open("/dev/null", O_RDONLY);
            dup2(targetFD, 1);
            dup2(sourceFD, 0);
        }

        // redirect output if set
        if(outPath != NULL)
        {
            targetFD = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if(targetFD == -1){
                printf("%s: No such file or directory\n", outPath);
                exit(1);
            }
            int result = dup2(targetFD, 1);
            if (result == -1) {
                printf("%s: No such file or directory\n", outPath);
                exit(1); 
            }
        }

        // redirect input if set
        if(inPath != NULL)
        {
            sourceFD = open(inPath, O_RDONLY);
            if (sourceFD == -1) { 
                printf("%s: No such file or directory\n", inPath);
                exit(1); 
            }
            int result = dup2(sourceFD, 0);
            if (result == -1) { 
                printf("%s: No such file or directory\n", inPath);
                exit(1); 
            }

        }

        // if foreground command, set default SIGINT action
        if(background != 1)
        {
            SIGINT_action.sa_handler = SIG_DFL;
            sigaction(SIGINT, &SIGINT_action, NULL);
        }

        // run command
        execvp(commandList[0], commandList);
        printf("%s: execv command not found\n", commandList[0]);
        fflush(stdout);
        exit(EXIT_FAILURE);
        break;

    // parent process
    default:
        if(background == 0)
        {
            // if child is not a background process, wait for it to return
            waitpid(spawnpid, &childStatus, 0);
            
            // set child process status
            if(WIFEXITED(childStatus))
            {
                statusValue = WEXITSTATUS(childStatus);
                signalValue = 0; 
            }
            if(WIFSIGNALED(childStatus))
            {
                statusValue = WEXITSTATUS(childStatus);
                signalValue = WTERMSIG(childStatus); 
            }

            // check for prior background process completion
            int backgroundPid = waitpid(-1, &childStatus, WNOHANG);
            if(backgroundPid > 0)
            {
                // process completed normally
                if(WIFEXITED(childStatus) && foregroundMode != 1)
                {
                    printf("background pid %d is done: exit value %d\n", backgroundPid, WEXITSTATUS(childStatus));
                }
                
                // process terminated by signal
                if(WIFSIGNALED(childStatus) && foregroundMode != 1)
                {
                    printf("background pid %d is done: terminated by signal %d\n", backgroundPid, WTERMSIG(childStatus));
                }
            }
        }

        // if child is background process, print the PID
        else
        {
            printf("background pid is %d\n", spawnpid);
        }
        break;
    }
}

/* Get command function
 * Prints command prompt : and handles comments and blank lines
 * Command arguments are stored in char* array and filled with strtok_r
 * For each argument, it is scanned character by character for $$ to expand
 * Then appropriate functions are called based on the first element of command list 
*/
void getCommand()
{
    char commandString[2048] = "";
    char* saveptr;
    char* commandList[2048];
    int counter = 0;    
    char* comment = "#";

    // command prompt
    printf(": ");
    fflush(stdout);
    fgets(commandString, 2048, stdin);
    commandString[strcspn(commandString, "\n")] = 0;        // remove newline from fgets

    // handle comments and blank lines
    while(strncmp(commandString, comment, 1) == 0 || strlen(commandString) == 0)
    {
        printf(": ");
        fflush(stdout);
        fgets(commandString, 2048, stdin);
        commandString[strcspn(commandString, "\n")] = 0;
    }

    // use tokens to break up command string delimited by spaces
    char* token = strtok_r(commandString, " ", &saveptr);
    if(token == NULL)
    {
        commandList[counter] = token;
    }

    // loop to fill command array and find $$
    while(token != NULL)
    {
        commandList[counter] = token;

        // loop through each character of command argument to find $$
        for(int i = 0; i <= strlen(commandList[counter]); i++)
        {
            if(commandList[counter][i] == '$' && commandList[counter][i+1] == '$')
            {
                // call varaible expansion function
                commandList[counter] = expandify(commandList[counter]);
            }
        }

        token = strtok_r(NULL, " ", &saveptr);
        counter++;
    }

    commandList[counter] = NULL;

    // call built in commands and fork function if the first command argument is ...
    if(strncmp(commandList[0], "exit", strlen("exit")) == 0)
    {
        exitCommand();
    }
    else if(strncmp(commandList[0], "status", strlen("status")) == 0)
    {
        statusCommand();
    }
    else if(strncmp(commandList[0], "cd", strlen("cd")) == 0)
    {
        cdCommand(counter, commandList[1]);
    }
    else if(strlen(commandString) != 0)
    {
        forkCommand(commandList, counter);
    }
}

/* Main
 * Sets up signal handlers
 * Infinitley loops program
*/
int main(int argc, char* argv[])
{
    struct sigaction SIGINT_action = {0};
    struct sigaction SIGTSTP_action = {0};

    SIGINT_action.sa_handler = handle_SIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    
    while(1 > 0)
    {
        getCommand();
    }
    
    return EXIT_SUCCESS;
}
