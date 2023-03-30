#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include<stdbool.h>
#include <fcntl.h>

#define RESET   "\033[0m"
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */

#define BUFSIZE 512
#ifndef DEBUG
    #define DEBUG 0
#endif

void changeDir(char *path){
    int res = chdir(path);
    if (res==-1) perror("cd failed");
    return;
}

// sucess: return 0 and info to buf
// failed: return -1
int getCurDir(char* buf){
    if (getcwd(buf, BUFSIZE) == NULL) {
        // if getcwd gets error
        perror("getcwd() error");
        return -1;
    }
    return EXIT_SUCCESS;
};

char* findPath(char* prog_name) {
    bool isPathGiven = false;
    if (prog_name[0] == '/') isPathGiven = true;

    // Infos about the filePath, and stat_buf (using stat())
    struct stat stat_buf;
    char* filePath = NULL;

    if (isPathGiven){
        int pathSize = strlen(prog_name) + 1;
        filePath = malloc(pathSize);
        memcpy(filePath, prog_name, pathSize); // take program path from command
    }
    else
    {
        // look for program based on bare names from command
        char *PATH[6] = {
            "/usr/local/sbin/", 
            "/usr/local/bin/",
            "/usr/sbin/",
            "/usr/bin/",
            "/sbin/",
            "/bin/"
        };
        int path_count = 6;
        char* tryPath;
        int pathSize;

        for (int i = 0; i < path_count; i++)
        {   
            // concatenate PATH[i] and prog_name
            pathSize = strlen(PATH[i]) + strlen(prog_name) + 1;
            pathSize = ((pathSize + 7)/8)  * 8;

            //assign and try paths
            
            tryPath = (char*) malloc(pathSize * sizeof(char));
            strcpy(tryPath, PATH[i]);
            strcat(tryPath, prog_name); 

            if (stat(tryPath, &stat_buf) == 0) // found program
            {
                if (DEBUG) {
                    printf(YELLOW "stat() found file %s at dir: %s" RESET "\n", prog_name, tryPath);
                }
                filePath = (char*) malloc(pathSize);
                memcpy(filePath, tryPath, pathSize);
                break; // stop searching
            }
            free(tryPath);
        }
    }
    return filePath;
}

pid_t execProgram(char*** commands, int* idx, int len, int* pd, int lastPipe){
    // Create a child process
    char** prog_args = commands[*idx];
    int nfd;

    // Implemented for single command so far
    // TODO: split arguments corresponding each command and execute
    if (strcmp(*prog_args, "pwd") == 0){
        char buffer[BUFSIZE];
        int res = getCurDir(buffer);
        if (res == 0){  
            nfd = dup2(pd[1], STDOUT_FILENO);
            if (nfd == -1) {
                perror(RED "ERROR: write to pipefailed" RESET "\n");
            }
            int writeBytes = write(STDOUT_FILENO, buffer, BUFSIZE);
            if (writeBytes == -1) printf(RED "Didn't write any byte!!!" RESET "\n");
            else if (DEBUG) printf(YELLOW "Wrote %d bytes!!" RESET "\n", writeBytes);
            close(pd[0]);
            close(pd[1]);
        }
        else {
            printf("pwd failed\n");
        }
        return 0;
    }
    if (strcmp(*prog_args, "cd") == 0){
        changeDir(*(*commands+1));
        return 0;
    }

    char* filePath = findPath(*prog_args);
    int pid = 0;
    // printf("len is %d and idx is %d \n", len, *idx);
    if (filePath == NULL) {
        // program not found
        printf("Program %s not found!\n", *prog_args);
    }
    else
    {
        // Run program in filePath

        pid = fork();
        if (pid == -1) { 
            perror("fork failed");
        }
        if (pid == 0) {// we are in the child process
        
            //Get input from pipeline
            //if not first command.
            if (*idx != 0 && lastPipe >= 0){
                if (DEBUG) printf(YELLOW "reading from pipeline[%d]" RESET "\n", (lastPipe));
                nfd = dup2(pd[lastPipe], STDIN_FILENO);
                if (nfd == -1){
                    perror(RED "ERROR: Read from pipeline failed" RESET "\n");
                }
            }

            //write output to pipeline
            //if not last command
            if (*idx < (len-1))
            {
                if (DEBUG) printf(YELLOW "writing to pipeline[%d]" RESET "\n", (*idx)*2 + 1);
                nfd = dup2(pd[(*idx)*2 + 1], STDOUT_FILENO);
                if (nfd == -1){
                    perror(RED "ERROR: Write to pipeline failed" RESET "\n");
                }
            }

            //close all pipes;
            for (int i = 0; i < 2*len; i++) close(pd[i]);

            // Look for redirection
            while (((*idx)+1 < len) && commands[(*idx)+1][0][0] != '|') {
                if (commands[*(idx)+1][0] != NULL)
                {
                    *idx = (*idx) + 1;
                    if (commands[*idx][0][0] == '<')
                    {
                        if (commands[*idx][1] == NULL) {
                            printf(RED "Invalid Redirection Syntax" RESET "\n");
                            exit(EXIT_FAILURE);
                        }
                        // Redirect input from a file
                        int fd = open(commands[*idx][1], O_RDONLY);
                        int nfd = dup2(fd, STDIN_FILENO);
                        if (nfd == -1) printf( RED "ERROR: Redirect input failed" RESET "\n");
                        close(fd);    
                    }


                    if (commands[*idx][0][0] == '>')
                    {
                        if (commands[*idx][1] == NULL) {
                            printf(RED "Invalid Redirection Syntax" RESET "\n");
                            exit(EXIT_FAILURE);
                        }

                        // Redirect output to a file
                        int fd = open(commands[*idx][1], O_WRONLY|O_CREAT|O_TRUNC, 0640);
                        int nfd = dup2(fd, STDOUT_FILENO);
                        if (nfd == -1) printf( RED "ERROR: Redirect output failed" RESET "\n");
                        close(fd);
                    }   
                }
            }
            // Execute program
            if (execv(filePath, prog_args) < 0){
                perror("execv failed");
                exit(0);
            }

            // if we reach here, something went wrong
            exit(EXIT_FAILURE);
        }
    }
    free(filePath);
    return pid;
}

void execute(char ***commands, int len) {
    if (commands == NULL) return;

    int* pids = malloc(sizeof(int) * 4);
    int pids_count = 0, pids_size = 4;
    int command_iterator = 0;
    int pd[2*len]; 
    for (int i = 0; i < len;i++){ 
        if (pipe(pd + i*2) == -1){
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    int pid;
    int lastWritePipe = -1;
    while (command_iterator < len){
        if (commands[command_iterator][0][0] != '<' && \
            commands[command_iterator][0][0] != '>' && \
            commands[command_iterator][0][0] != '|')
        {
            pid = execProgram(commands, &command_iterator, len, pd, lastWritePipe);                        
            if (pid >= 0){
                if (pids_count + 1> pids_size){
                    pids_size *= 2;
                    pids = realloc(pids, pids_size);
                }
                lastWritePipe = command_iterator * 2;
                printf("lastWritePipe value %d\n", lastWritePipe);
                pids[pids_count++] = pid;
            } 
        }  
        command_iterator++;
    }
    
    //close all pipes;
    for (int i = 0; i < 2 * len; i++)
        close(pd[i]);

    if (DEBUG) printf(YELLOW "### Number of process is %d" RESET "\n", pids_count);

    int clid, wstatus;
    // wait all processes
    for (int i = 0; i < pids_count; i++)
    {    
        clid = wait(&wstatus); // wait for child to finish
        if (clid == -1) // wait failed
        {
            perror("wait failed");
        }
        if (WIFEXITED(wstatus)){
            // child exited normally
            printf("\nchild exited with status %d\n", WEXITSTATUS(wstatus));
        }else{
            printf("\nexited abnormally\n");
        }
    }
}
