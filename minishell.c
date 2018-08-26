/* Author: Joel Herrick
 * Class: CS344 Operating Systems I
 * Date: 8/6/2018
 * Description: smallsh mimics ability of bash shell
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

//void exitShell();
//void changeDir();
//void getStatus();

//global flag for foreground-only mode
int globalSigFlag;

//catch sigtstp to enter/exit fg-only mode
void catchSIGTSTP(int signo) {
	if(globalSigFlag) {
		globalSigFlag = 0;
		printf("\nExiting foreground-only mode.\n");
		fflush(stdout);
	}

	else {
		globalSigFlag = 1;
		printf("\nEntering foreground-only mode (& is now ignored)\n");
		fflush(stdout);
	}
}

//replace all instances of $$ with pid, taken from 
//https://www.linuxquestions.org/questions/programming-9/replace-a-substring-with-another-string-in-c-170076/
char *replace_str(char *str, char *orig, char *rep) {
	static char buffer[4096];
	char *p;

	if(!(p = strstr(str,orig)))
		return str;

	strncpy(buffer, str, p-str);
	buffer[p-str] = '\0';

	sprintf(buffer+(p-str), "%s%s", rep, p+strlen(orig));

	return buffer;
}

//main
void main() {
	//sigint sigaction struct setup
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = SIG_IGN;
	//sigfillset(&SIGINT_action.sa_mask);
	//SIGINT_action.sa_flags = SA_RESTART;
	sigaction(SIGINT, &SIGINT_action, NULL);
	
	//sigtstp sigaction struct setup
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	//default not in fg-only mode
	globalSigFlag = 0;

	//commence the vars...
	int status = 0;
	int numCharsEntered = -5;
	size_t bufferSize = 0;
	char * inputLine = NULL;
	char * lineEntered;
	char * args[512];
	int argsCount;
	char * token;
	char * inputFile;
	char * outputFile;
	char * tempStr = NULL;
	int bgProcess = 0;
	int exiting = 0;
	pid_t cpid, wpid;
	int fd, fd2, fd3; 
	int inF = 0;
	int outF = 0;

	//advanced user input adapted from 
	//https://oregonstate.instructure.com/courses/1683586/pages/3-dot-3-advanced-user-input-with-getline
	while(!(exiting)) {
		while(!(exiting)) {
			printf(": ");
			fflush(stdout);

			numCharsEntered = getline(&inputLine, &bufferSize, stdin);
			if (numCharsEntered == -1)
				clearerr(stdin);
			else
				break;
		}
		//while loop vars
		bgProcess = 0;
		argsCount = 0;
		int parser = 1;

		//convert pid to string to use in replace_str
		int dpid = getpid();
		int length = snprintf(NULL, 0, "%d", dpid);
		tempStr = malloc(length+1);
		snprintf(tempStr, length+1, "%d", dpid);

		//replace \n with \0 -- taken from url above
		inputLine[strcspn(inputLine, "\n")] = '\0';

		//replace all instances of $$ with pid, taken from 
		//https://www.linuxquestions.org/questions/programming-9/replace-a-substring-with-another-string-in-c-170076/
		char * lineEntered = replace_str(inputLine, "$$", tempStr);

		//strtok parse text code adapted from http://www.cs.ucf.edu/~lboloni/Teaching/COP4600_Fall2013/homework/strtokeg.c
		//and https://stackoverflow.com/questions/30415663/c-using-strtok-to-parse-command-line-inputs
		token = strtok(lineEntered, " ");

		while (token != NULL && parser != 0) {
			//if we find < set flag to get filename
			if(strcmp(token, "<") == 0) {
				parser = 5;
			}

			//if we find > set flag to get filename
			else if(strcmp(token, ">") == 0) {
				parser = 6;
			}

			//if we find & set flag to make background process and end reading
			else if(strcmp(token, "&") == 0) {
				if(!globalSigFlag) {
					bgProcess = 1;
				}
				parser = 0;
			}

			/*else if(strcmp(token, "$$") == 0) {
	
				args[argsCount] = strdup(tempStr);
				argsCount++;
			}*/

			else {
				//grab input file argument
				if(parser == 5) {
					inF = 1;
					inputFile = strdup(token);
				}

				//grab output file argument
				else if(parser == 6) {
					outF = 1;
					outputFile = strdup(token);
				}

				else {
					//add to our arguments array
					args[argsCount] = strdup(token);
					argsCount++;		
				}			
			}
			//get next token
			token = strtok(NULL, " ");
		}

		//makes next check easier
		args[argsCount] = NULL;		

		// checking for int/ext command taken from 
		//http://www.cs.ucf.edu/~lboloni/Teaching/COP4600_Fall2013/homework/strtokeg.c
		if (args[0] == NULL || *(args[0]) == '#') { 
			//do nothing
		}	

		//do exit
		else if (strcmp(args[0], "exit") == 0) {
			exit(0);
			exiting = 1;
		}

		//do changedir
		else if (strcmp(args[0], "cd")==0) {
			//no args == getenv for HOME
			if(args[1] == NULL) {
				chdir(getenv("HOME"));
			}

			//otherwise cd to arg provided
			else {
				chdir(args[1]);
			}
		}
		
		//do status
		else if (strcmp(args[0], "status") == 0) {
			if(WIFEXITED(status)) {
				printf("exit value %d\n", WEXITSTATUS(status));
				fflush(stdout);
			}

			else {
				printf("terminated by signal %d\n", status);
				fflush(stdout);
			}
		}
		
		//do exec
		else { 		
			//create fork
			cpid = fork();

			//switch idea taken from lecture 3.2 process management & zombies
			switch (cpid) {
				//if it's the child
				case 0:
					//foreground?
					if(!bgProcess) {
						//set up SIGINT_action handlers to default if foreground
						SIGINT_action.sa_handler = SIG_DFL;
						SIGINT_action.sa_flags = 0;
						sigaction(SIGINT, &SIGINT_action, NULL);
					}

					//if there's an input file
					if (inF) {
						//open it read
						fd = open(inputFile, O_RDONLY);

						//error if can't open it
						if(fd == -1) {
							fprintf(stderr, "Invalid file, could not open %s for input.\n", inputFile);
							fflush(stdout);
							exit(1);
						}

						//error if can't redirect
						else { 
							if (dup2(fd, 0) == -1) {
								fprintf(stderr, "Error: could not redirect stdin.\n");
								fflush(stdout);
								exit(1);
							}

							fcntl(fd, F_SETFD, FD_CLOEXEC);
						}	
					}

					//output file?
					if (outF) {
						//open file write
						fd2 = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

						//error if invalid file
						if(fd2 == -1) {
							fprintf(stderr, "Invalid file, could not open %s for output.\n", outputFile);
							fflush(stdout);
							exit(1);
						}

						//error if can't redirect
						else {
							if (dup2(fd2, 1) == -1) {
								fprintf(stderr, "Error: could not redirect stdout.\n");
								fflush(stdout);
								exit(1);
							}

							//fcntl(fd2, F_SETFD, FD_CLOEXEC);
							close(fd2);
						}
					}

					//background route to dev/null?
					else if (bgProcess && inF != 1 && outF != 1) {
						//open file read
						fd3 = open("/dev/null", O_RDONLY);

						//error if invalid file
						if(fd3 == -1) {
							fprintf(stderr, "Invalid file, could not open.\n");
							fflush(stdout);
							exit(1);
						}

						//error if can't redirect
						else {
							if (dup2(fd3, 0) == -1) {
								fprintf(stderr, "Error: could not redirect.\n");
								fflush(stdout);
								exit(1);
							}
							fcntl(fd3, F_SETFD, FD_CLOEXEC);
						}
					}

					//execute command
					if (execvp(args[0], args) == -1) {
						printf("%s: no such file or directory.\n", args[0]);
						fflush(stdout);
						exit(1);
					}
					break;

				//problem forking?
				case -1: 
					printf("Error Forking.\n");
					fflush(stdout);
					status = 1;
					break;

				//parent
				default:
					//background
					if (bgProcess && !globalSigFlag) {
						printf("background pid is %d\n", cpid);
						fflush(stdout);
					}
					
					//foreground
					else {
						//keep eye on signals coming in
						do {
							wpid = waitpid(cpid, &status, WUNTRACED);
						} while (!WIFEXITED(status) && !WIFSIGNALED(status));		

						//print out the terminated signal immediately
						if (!(WIFEXITED(status))) { 
						printf("terminated by signal %d\n", status);
						fflush(stdout);
	    				}
					}
					break;
				}

		}		

		//printf("%s", lineEntered);

		//clean up args array
		int i;
		for (i = 0; i < argsCount; i++) {
			args[i] = NULL;
		}

		//clean up input buffer
		free(inputLine);
		inputLine = NULL;

		//clean up i/o files
		inputFile = NULL;
		outputFile = NULL;
		inF = 0;
		outF = 0;

		//clean up pid replacer str
		free(tempStr);

		//check for zombos and that bg procs are done
		pid_t nwpid = waitpid(-1, &status, WNOHANG);
        while (nwpid > 0) {
        	//check we're not in fg-only mode to ignore any stray printfs...
        	if (!globalSigFlag) {
	        	//background process finished
	            printf("background pid %d is done: ", nwpid);
	            fflush(stdout);
		   
		   		//normal exit
	 	    	if (WIFEXITED(status)) { 
		    		printf("exit value %d\n", WEXITSTATUS(status));
		    		fflush(stdout);
		    	}

		    	//terminated exit
		    	else { 
					printf("terminated by signal %d\n", status);
					fflush(stdout);
		    	}
		    }

	    	//continue checking
	    	nwpid = waitpid(-1, &status, WNOHANG);
        }

	}

}
