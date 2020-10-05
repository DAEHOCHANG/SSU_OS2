#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 64
#define MAX_NUM_TOKENS 64

int pipe_num=0; // 한 명령어 라인에서의 파이프 개수
int order_arr[MAX_NUM_TOKENS / 2 + 2][2];
char env[1024];
char curp[1024];
// 파이프 존재때문에 한 라인에 여러 명령어가 올수 있음
// 이 때 [n][0]에 n번째 명령어의 시작지점을 저장하고
//       [n][1]에 n번째 명령어의 종료지점을 저장한다.

/*
   ssu_shell.c
   ./ssu_shell, ./ssu_shell file 두종류의 실행이 가능합니다.

   */




/* Splits the string by space and returns the array of tokens
 *
 */
char **tokenize(char *line)
{
	char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
	char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
	int i, tokenIndex = 0, tokenNo = 0;

	for(i =0; i < strlen(line); i++){

		char readChar = line[i];

		if (readChar == ' ' || readChar == '\n' || readChar == '\t'){
			token[tokenIndex] = '\0';
			if (tokenIndex != 0){
				tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
				strcpy(tokens[tokenNo++], token);
				tokenIndex = 0; 
			}
		} else {
			token[tokenIndex++] = readChar;
		}
	}

	free(token);
	tokens[tokenNo] = NULL ;
	return tokens;
}

void Daeho_run(char **arr) {
	int fd[2];
	pid_t pid = 0;
	int fd_in = 0;
	int i=0;
	char *tmp ;
	int status;
	for(i=0; i<=pipe_num; i++) {
		pipe(fd);
		if (pid = fork() == 0) {

			dup2(fd_in, 0); //표준 입력 변경 초기에는 0이기때문에 변화 없음
			if (i < pipe_num){
				dup2(fd[1],1); // 표준 출력 변경 마지막에는 출력은 stdout이야 하기에 조건문
			}
			close(fd[0]);
			
			int s = order_arr[i][0];
			int e = order_arr[i][1];
			char order[64];
			strcpy(order,arr[s]);
			free(arr[e+1]);
			arr[e+1] = NULL;
			//order에 파일 절대경로 표시
			//arr[e+1]을 NULL로 만들어 줌으로서 arr + s ~ arr+ e를 넘겨줄 수 있음
			if (execvp(order, arr + s) < 0) {
				fprintf(stderr, "SSUShell : Incorrect command\n");
				exit(1);
			}
		}
		else {
			//자식노드에서 프로그램을 실행시킬동안 기다린다.
			wait(&status);
			if (status != 0)
				break;
			//만약 잘못되어 execvp가 실패할 경우 exit(1)이 호출되며
			//정상 종료시 0이 리턴되기에 0이 아닌경우 에러로 판단해 break해준다.
			close(fd[1]);
			//출력 파이프 닫아준후 fd_in을 입력파이프 디스크립터로 바꿔준다.
			fd_in = fd[0];
		}
	}
}

int main(int argc, char* argv[]) {
	char  line[MAX_INPUT_SIZE];            
	char  **tokens;              
	int i;
	FILE* fp;
	strcpy(env, getenv("PATH"));
	strcat(env,":");
	realpath(".",curp);
	strcat(env, curp);
	setenv("PATH",env,1);
	
	if(argc == 2) {
		fp = fopen(argv[1],"r");
		if(fp < 0) {
			printf("File doesn't exists.");
			return -1;
		}
	}
	while(1) {			
		/* BEGIN: TAKING INPUT */
		bzero(line, sizeof(line));
		if(argc == 2) { // batch mode
			if(fgets(line, sizeof(line), fp) == NULL) { // file reading finished
				break;	
			}
			line[strlen(line) - 1] = '\0';
		} else { // interactive mode
			printf("$ ");
			scanf("%[^\n]", line);
			getchar();
		}

		line[strlen(line)] = '\n'; //terminate with new line
		tokens = tokenize(line);

		int prev = 0;
		pipe_num = 0;
		order_arr[0][0] = 0;
		for(i=0;tokens[i]!=NULL;i++){
			if (strcmp(tokens[i], "|")==0) {
				order_arr[pipe_num][1] = i-1;
				order_arr[pipe_num+1][0] = i+1;
				pipe_num++;
			}
		}
		order_arr[pipe_num][1] = i-1;
		//명령어들의 시작지점과 끝지점을 저장해 둔다.

		int status;
		Daeho_run(tokens);
		// 실행시키는 부분

		// Freeing the allocated memory	
		int cnt = pipe_num + pipe_num + 1;
		for(i=0;i<cnt;i++){
			if (tokens[i] != NULL)
				free(tokens[i]);
		}
		free(tokens);
	}
	return 0;
}

