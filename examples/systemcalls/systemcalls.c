#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int ret; 
    bool result = false;

    /* Calling the system() system call */
    ret = system(cmd);
    /* If a child process could not be created, or its status could not be retrieved, the return value is -1 
       and errno is set to indicate the  error*/
    if (ret == -1)
    {
        perror("Child process could not be created");
    }
    else
    {
        /*
            If  a  shell  could not be executed in the child process, then the return value is as though 
            the child shell terminated by calling _exit(2) with the status 127.
         */
        if (ret == 127)
        {
            perror("Can't execute the Shell");
        }
        /* 
            1. If  a  shell  could not be executed in the child process, then the return value is as though 
            the child shell terminated by calling _exit(2) with the status 127.

            2. If all system calls succeed, then the return value is the termination status of the child shell used to execute command.  
            (The  termination status of a shell is the termination status of the last command it executes.)
        */
        if (WIFEXITED(ret)) 
        {
            printf("Child process exited with status: %d\n", WEXITSTATUS(ret));
            result = true;
        }
        else
        {
            perror("Child process didn't exit the command normally\n");
        }
    }
    printf("Final Status is %d\n", result);
    return result;
}
/**
 * @param command the parsed list of argument
 * @param fd the file descriptor if exec to be redirected 
 * @return boolean true in case of successful execution / false otherwise
*/
static bool common_do_exec(char * command[], int fd)
{
    bool result = false;
    /* fork locals */
    pid_t fork_pid;
    /* execv locals */
    /* wait locals */
    pid_t wait_pid;
    int status;
    int saved_stderr, saved_stdout;
    

    if (fd > 2)
    {
        /* Save the stdout and stderr file descriptor to make it easy to return back */
        saved_stdout = dup(1);
        saved_stderr = dup(2);
        if ((saved_stdout == -1) ||
            (saved_stderr == -1)) 
        {
            perror("dup: failed to store stdout or stderr");
            goto func_exit;
        }
        /* redirect the stdout and std error*/
        if (dup2(fd, 1) < 0) 
        { 
            perror("dup2: Fatal Error STDOUT"); 
            goto func_exit;
        }
        if (dup2(fd, 2) < 0) 
        {
            perror("dup2: Fatal Error STDERR"); 
            goto func_exit;
        }
    }

    /* Forking the child process */
    fork_pid = fork();
    if (fork_pid == -1)
    {
        perror("Fork: Error in forking the child");
        goto func_exit;
    }
    /* Successfully forked */
    if (fork_pid == 0)
    {
        /* Child code */
        if(-1 == execv(command[0],&command[0]))
        {
            perror("Execv: Error in execv");
            exit(EXIT_FAILURE);
        }
        else
        {
            exit(EXIT_SUCCESS);
        }
    }
    else
    {
        /* Parent code */
        wait_pid = waitpid (fork_pid, &status, 0);
        if (wait_pid == -1)
        {
            perror ("wait");
            goto func_exit;
        }
                
        if (WIFEXITED (status))
        {
            printf ("Normal termination with exit status=%d\n",WEXITSTATUS (status));
            if (WEXITSTATUS (status) == 0)
            {
                printf("SUCCESSFUL Status from Child\n");
                result = true;
            }
            else
            {
                printf("Failure Status from Child\n");
            }
        }
        else if (WIFSIGNALED (status))
        {
            /* Not necessary but added here for future use  */
            printf ("Killed by signal=%d%s\n",WTERMSIG (status), WCOREDUMP (status) ? " (dumped core)" : "");
        }
        else
        {
            perror("Error Waiting Status");
            goto func_exit;
        }
    }

func_exit:
    /* retore the direction of stdout/stderr */
    if (fd > 2)
    {
        if (dup2(saved_stdout, 1) < 0) 
        { 
            perror("dup2: Fatal Error STDOUT"); 
            goto func_exit;
        }
        if (dup2(saved_stderr, 2) < 0) 
        {
            perror("dup2: Fatal Error STDERR"); 
            goto func_exit;
        }
    }
    return result;
}
/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    bool result = false;

    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];
    
    result  = common_do_exec(command, 0);

    va_end(args);
    printf("Final Status is %d\n", result);
    return result;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    bool result = false;
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;

    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];
    
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, S_IRUSR| S_IWUSR | S_IRGRP| S_IROTH);
    if (fd == -1) 
    { 
        perror("open"); 
        goto func_exit;
    }    
    /* call the main functionality */
    result = common_do_exec(command, fd);
    
    if (-1 == close(fd))
    {
        perror("close: failed to close the file\n");
        goto func_exit;
    }

func_exit:
    va_end(args);
    printf("Final Status is %d\n", result);
    return result;
}

#if 0
/* local driver */
int main()
{
    bool result;
    char* test = "ls";
    char * args[] = { "ls", "-l", NULL };
    // result = do_system(test);
    // if (result == false){printf("failure in do_system\n"); return -1;}
    // else {printf("SUCCESS in do_system\n");}
    result = do_exec(3, "/usr/bin/test","-f","echo");//do_exec(3, test, args[0], args[1], args[2]);
    if (result == false){printf("failure in do_exec\n");return -1;}
    else {printf("SUCCESS in do_exec\n");}
    // result = do_exec_redirect("/home/khaled/Desktop/file_test",3, test, args[0], args[1], args[2]);
    // if (result == false){printf("failure in do_exec_redirect\n");return -1;}
    // else {printf("SUCCESS in do_exec_redirect\n");}
    printf("PASSED\n");
    return 0;
}
#endif /* DEBUGGING */