#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include "tokenize.h"
#include "execution.h"

#define BUFSIZE 512
#define RESET   "\033[0m"
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */

#ifndef DEBUG
#define DEBUG 0
#endif

char *lineBuffer;
int linePos = 0, lineSize = 1;

void append(char *, int);
void print();
void printCommands();

//TODO: implement free() for all malloc'ed space

void ReadThenWrite(){
    int pos;
    char buffer[BUFSIZE];

    int readBytes, lstart = 0; // lstart to calculate len of each line
    if (isatty(STDIN_FILENO)){
	printf(GREEN "THeshell> " RESET);
	fflush(stdout);
    }
    while ((readBytes = read(STDIN_FILENO, buffer, BUFSIZE)) > 0){
	if (DEBUG) printf(YELLOW "Read %d bytes!!" RESET "\n", readBytes);
	// If a new line, process the line buffer
	lstart = 0;
	bool tokenizeFailed = false;
	bool lastCommandFailed = false;
	for (pos = 0; pos < readBytes; pos++){
	    if (buffer[pos] == '\n'){
		if (pos == 0) break;
		int thislen = pos - lstart + 1;
		if (DEBUG) fprintf(stderr,GREEN "stream pos: %d | lstart: %d | finished line %d+%d bytes\n" RESET, pos, lstart, linePos, thislen);
		append(buffer + lstart, thislen);

		/* Tokenize each line */
		int scale = 1;
		char ***commands = malloc(BUFSIZE);
		int len = 0;
		int initCommCapacity = (int) (BUFSIZE/sizeof(char***));
		while (1) { // need loop to make sure commands is large enough
		    len = tokenize(commands, scale*initCommCapacity, lineBuffer, linePos);
		    if (len == -1) {
			tokenizeFailed = true;
		    } else if (len == -2) { // need to resize commands
			free(commands);
			scale *= 2;
			commands = malloc(scale*BUFSIZE);
			if (DEBUG) printf("resizing->commCapacity = %d\n", scale*initCommCapacity);
		    }
		    break;
		}

		// write words in commands
		if (!tokenizeFailed) {
		    if (DEBUG) printf("printing commands\n");
		    if (DEBUG) printCommands(commands, len); 

		    if (**commands != NULL && strcmp(**commands, "exit") == 0){
			printf(RED "Terminating THeShell!" RESET "\n");
			fflush(stdout);
			exit(EXIT_SUCCESS);
		    }
		    // execute commands
		    lastCommandFailed = execute(commands, len);
		}

		// free up pointers
		for (int i = 0; i<len; i++) {
		    free(commands[i]);
		}
		free(commands);
		// reset line buffer
		lstart = pos + 1;
		linePos = 0;
	    }
	}
	if (isatty(STDOUT_FILENO)){
	    if (lastCommandFailed || tokenizeFailed) printf(RED "!" RESET);
	    printf(GREEN "THeshell> " RESET);
	    fflush(stdout);
	    continue;
	}
	if (lstart < readBytes){ // if partial line at the end of readBytes
	    int thisLen = readBytes - lstart;
	    if (DEBUG) fprintf(stderr, YELLOW "partial line %d+%d bytes" RESET "\n", linePos, thisLen);
	    append(buffer + lstart, thisLen);
	}
    }
    if (linePos > 0){
	// file ended with partial line
	append("\n",1);
	linePos = 0;
    }
}

// add specified text the line buffer, expanding as necessary
// assumes we are adding at least one byte
void append(char *buf, int len){

    int newPos = linePos + len;
    int isExpanded = 0;
    while (newPos > lineSize){
	isExpanded = 1;
	lineSize *= 2;
	if (DEBUG) fprintf(stderr, YELLOW "expanding line buffer to %d" RESET "\n", lineSize);
    }   
    if (isExpanded) {
	// realloc buffer if resized
	lineBuffer = realloc(lineBuffer, lineSize);
	if (lineBuffer == NULL){
	    perror("line buffer");
	    exit(EXIT_FAILURE);
	}
    }
    memcpy(lineBuffer + linePos, buf, len);
    linePos = newPos; // linePos will be reset after print() in ReadThenWrite()
}

// Print each command from buffer to file fd
void print() {
    int writeBytes = write(STDOUT_FILENO, lineBuffer, linePos);
    if (writeBytes == -1) printf(RED "Didn't write any byte!!!" RESET "\n");
    else if (DEBUG) printf(YELLOW "Wrote %d bytes!!" RESET "\n", writeBytes);
}

// Print each command from buffer to file fd
void printCommands(char*** commands, int len) {
    int i = 0;
    int j = 0;
    printf("Commands: ");
    printf("[");
    while (i < len){
	printf("["); 
	while ( commands[i][j] != NULL ){
	    printf("%s", commands[i][j]);
	    j++;
	    printf(", ");
	}
	printf("NULL]");
	i++;
	j = 0;
	if (i != len) printf(", "); }
    printf("]\n");
}

void setupInputOutput(int argc, char** argv){
    if (argc <= 1)
    {
	//stdin stdout to terminal - using STDIN_FILENO and STDOUT_FILENO
	if (isatty(STDIN_FILENO)) // if input_fd is pointing to terminal
	    printf(RED "Welcome to THeShell! (a Tran / Herman collab)" RESET "\n"); 
    }
    else
    {
	//set stdin stdout to argv[1] and "output.txt"
	char* dname = argv[1];
	// open dir
	int fd, nfd;
	fd = open(dname, O_RDONLY);
	if (fd == -1){
	    perror(dname);
	    exit(EXIT_FAILURE);
	} else {
        nfd = dup2(fd, STDIN_FILENO);
        if (nfd == -1) printf(RED "ERROR: assigning STDOUT_FILENO failed" RESET "\n");
	    if (DEBUG) printf(YELLOW "Successfully opened %s" RESET "\n", dname); 
	}
    close(fd);
	// open output
	fd = open("output.txt", O_RDWR | O_CREAT | O_TRUNC, 0000700);
	if (fd == -1){
	    perror("output.txt");
	    exit(EXIT_FAILURE);
	} else {
	    nfd = dup2(fd, STDOUT_FILENO);
        if (nfd == -1) printf(RED "ERROR: assigning STDOUT_FILENO failed" RESET "\n");
	    if (DEBUG) printf(YELLOW "Successfully opened output.txt" RESET "\n");
	}
    close(fd);
    }
}

int main(int argc, char** argv){

    setupInputOutput(argc, argv);
    ReadThenWrite();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);   
    return EXIT_SUCCESS;
}
