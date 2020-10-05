#include <stdio.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>
#include "pps.h"

#define uoption 0
#define aoption 1
#define xoption 2
int option[3] = {0,0,0};
int pnum=0;
char printbuf[1024];
/*
   struct data
   ps시 출력할 데이터를 모아둔 구조체
 */
struct data { 
	char user[64];
	int pid;
	double pcpu;
	double mem;
	unsigned int vsz;
	unsigned int rss;
	char tty[64];
	char stat[10];
	time_t stime;
	time_t rtime;
	char command[128];
};
struct data arr[5000];
struct winsize ws;


/*
   void get_usr(char *path);
   USER얻는 함수
   path는 /proc/pid까지 완성된 경로를 받는다.
   status파일로부터 uid를 얻어 getpwuid함수를 이용해
   USER이름을 얻는다.
 */
void get_usr(char *path) {
	FILE *fp;
	char tmp[64];
	char tbuf[64];
	unsigned int luid;
	struct passwd *pw;
	strcpy(tmp, path);
	strcat(tmp, "/status");
	fp = fopen(tmp, "r");
	for(int i=0; i<8; i++) {
		while(fgetc(fp) != '\n') ;		
	}
	fgets(tbuf,64,fp);
	for(int i=0; i<64; i++) {
		if (tbuf[i] >= '0' && tbuf[i] <= '9') {
			luid = atoi(tbuf + i);
			break;
		}
	}

	pw = getpwuid(luid);
	strcpy(arr[pnum].user,pw-> pw_name);
	if (strlen(arr[pnum].user) > 8) {
		arr[pnum].user[7] = '+';
		arr[pnum].user[8] = '\0';
	}
	fclose(fp);
}

/*
   void get_command(char *path);
   COMMAND 얻는 함수
   path 는 /proc/pid까지 완성된 경로를 받는다.
   cmdline파일을 먼저 확인해 있으면 그대로 써준다.
   없는경우 comm파일을 열어 파일이름을 받아 "[파일이름]"을 써준다.
 */
void get_command(char *path) {
	FILE *fp;
	char tmp1[64];
	char tmp2[64];
	struct stat statbuf;
	strcpy(tmp1,path);
	strcat(tmp1,"/cmdline");
	strcpy(tmp2,path);
	strcat(tmp2,"/comm");

	fp = fopen(tmp1, "r");
	if(fgets(arr[pnum].command, 128,fp) > 0){
		for(int i=1; i<127; i++)
			if (arr[pnum].command[i]== '\0' && arr[pnum].command[i+1] != '\0')
				arr[pnum].command[i] = ' ';
		fclose(fp);
	}
	else  {
		char tmp3[128];
		fclose(fp);
		fp = fopen(tmp2, "r");
		fgets(tmp3, 128,fp);
		tmp3[strlen(tmp3)-1]='\0';
		strcpy(arr[pnum].command, "[");
		strcat(arr[pnum].command, tmp3);
		strcat(arr[pnum].command, "]");
		fclose(fp);
	}
}

/*
   void get_cpu();
   cpu %를 구하는 함수
   이미 TIME이 먼저 구해져야 한다.
   TIME / uptime();
 */
void get_cpu() {
	arr[pnum].pcpu = (arr[pnum].rtime / uptime())*100;
}


/*
   void get_mem(char *path);
   %MEM 와 VSZ, RSS를 구하는 함수
   /proc/pid/status에 있는 RSS, VSZ, RSS를 구하고
   /proc/meminfo에서 전체 메모리 양을 구해 %MEM을 계산한다.
   %MEM = total mem / RSS
 */
void get_mem(char *path) {
	FILE *fp;
	char tmp1[64];
	char tmp2[64] = "/proc/meminfo";
	char btmp[50];
	int total;
	int used;
	int vsz;
	int count=0;

	strcpy(tmp1, path);
	strcat(tmp1, "/status");

	fp = fopen(tmp1, "r");
	while(count < 17) {
		if (fgetc(fp) == '\n')
			count++;
	}
	fgets(btmp, 50, fp);
	if (strncmp(btmp, "VmSize",6) != 0) {
		arr[pnum].rss=0;
		arr[pnum].mem=0;
		arr[pnum].vsz=0;
		fclose(fp);
		return;
	}
	for(int i=0; i<50; i++) {
		if (btmp[i] >= '0' && btmp[i] <= '9') {
			vsz = atoi(btmp + i);
			break;
		}
	}
	arr[pnum].vsz = vsz;

	while(count < 20) {
		if (fgetc(fp) == '\n')
			count++;
	}
	fgets(btmp, 50, fp);
	for(int i=0; i<50; i++) {
		if (btmp[i] >= '0' && btmp[i] <= '9') {
			used = atoi(btmp + i);
			break;
		}
	}
	fclose(fp);

	fp = fopen(tmp2, "r");
	fgets(btmp, 50,fp);
	for(int i=0; i<50; i++) {
		if (btmp[i] >= '0' && btmp[i] <= '9') {
			total = atoi(btmp+i);
			break;
		}
	}
	fclose(fp);

	arr[pnum].rss = used;
	arr[pnum].mem = ((double)used/total)*100; 
}

/*
   double uptime();
   /proc/uptime의 첫번째값을 반환해줍니다.
 */
double uptime() {
	char path[64] = "/proc/uptime";
	FILE *fp = fopen(path,"r");
	double ret;
	fscanf(fp,"%lf", &ret);
	fclose(fp);
	return ret;
}

/*
   void get_time(char *path);
   START와 TIME을 구하는 함수
   proc/pid/stat 의 14,15,22번째 인자를 받아 가공한다.
   START = curtime - uptime + (22인자 / sysconf(_SK_CLK_TCK))
   TIME = (14 + 15) / sysconf(_SC_CLK_TCK);
 */
void get_time(char *path) {
	FILE *fp;
	char tmp[64];
	long long unsigned int stime;
	long unsigned int systime;
	long unsigned int utime;

	strcpy(tmp, path);
	strcat(tmp, "/stat");
	fp = fopen(tmp,"r");
	for(int i=1; i<14; i++) {
		while(fgetc(fp) !=' ');
	}
	fscanf(fp,"%lu%lu",&utime, &systime);
	for(int i=0; i<7;i++) {
		while(fgetc(fp) !=' ');
	}
	fscanf(fp, "%llu",&stime);
	arr[pnum].rtime = (systime + utime) / sysconf(_SC_CLK_TCK);
	arr[pnum].stime = time(NULL) - uptime() + (stime / sysconf(_SC_CLK_TCK));
	fclose(fp);
}

/*
   void get_pid (char *path);
   /proc/pid/stat파일을 열어
   pid정보를 저장한다.
   */
void get_pid(char *path) {
	char tmp[64];
	FILE *fp;
	strcpy(tmp,path);
	strcat(tmp, "/stat");
	fp = fopen(tmp, "r");
	fscanf(fp,"%d", &arr[pnum].pid);
	fclose(fp);
}

/*
   void get_tty(char *path);
   tty를 읽습니다.
   15~8비트는 메이저번호
   0~7비트는 마이너 번호
   이를통해 /dev에서 tty를 찾는다.
 */
void get_tty(char *path) {
	FILE *fp;
	char tmp[64];
	char ppath[64];
	char ppp[64];
	char pppp[64];
	int ttty;
	int mask = 0xff;
	int minor;
	int major;
	strcpy(tmp, path);
	strcat(tmp, "/stat");
	fp = fopen(tmp, "r");

	for(int i=0; i<6; i++) {
		while(fgetc(fp) != ' ');
	}
	fscanf(fp, "%d", &ttty);
	if(ttty == 0) {
		strcpy(arr[pnum].tty, "?");
		fclose(fp);
		return;
	}
	minor = ttty & mask;
	major = ttty >> 8;
	memset(ppp,0,sizeof(ppp));
	sprintf(ppath, "/dev/char/%d:%d",major, minor);
	if (readlink(ppath,ppp,64) >0) {
		strcpy(arr[pnum].tty, ppp + 3);
	}
	else {
		sprintf(pppp, "/dev/pts/%d",minor);
		if(access(pppp, F_OK) == 0)
			sprintf(arr[pnum].tty,"pts/%d",minor);
		else
			strcpy(arr[pnum].tty, "?");
	}
	fclose(fp);
}

/*
   아래 두 함수는 scandir에 필요한 함수로써
   숫자가 아닌 디렉토리는 filter함수로 걸러주고
   mysort를 통해 숫자 크기순으로 정렬해 준다.
 */
int mysort(const struct dirent **a, const struct dirent **b )
{
	int aa = atoi((*a) -> d_name);
	int bb = atoi((*b) -> d_name);
	return aa > bb;
}
int myfilter(const struct dirent * a) {
	int tmp = atoi(a -> d_name);
	if (tmp == 0)
		return 0;
	else
		return 1;
}
/*
   void get_stat(char *path);
   stat에 필요한 모든 정보를 얻은후
   stat를 가공하는 함수.
   /proc/pid/stat에 있는 stat 와
   session id, pr, ni, number of thread 들로 데이터 가공
 */

void get_stat(char *path) {
	//6,8, 18,19,20 session id,pr, ni, threadN
	//status VmLck
	char tmp[64];
	char bbuf[64];
	long int pr;
	long int ni;
	long int threadN;
	FILE *fp;
	pid_t sspid;
	pid_t spid;


	strcpy(tmp, path);
	strcat(tmp, "/stat");

	fp = fopen(tmp, "r");
	for(int i=0; i<2; i++) {
		while(fgetc(fp) != ' ');
	}
	fscanf(fp,"%s", arr[pnum].stat);
	for(int i=0; i<3; i++) {
		while(fgetc(fp) != ' ');
	}
	fscanf(fp, "%d", &sspid);
	while(fgetc(fp) != ' ');
	while(fgetc(fp) != ' ');
	fscanf(fp, "%d", &spid);

	for(int i=0; i<10; i++) {
		while(fgetc(fp) != ' ');
	}
	fscanf(fp, "%ld", &pr);
	fscanf(fp, "%ld", &ni);
	fscanf(fp, "%ld", &threadN);
	//우선순위 
	if (ni > 0)
		strcat(arr[pnum].stat,"N");
	else if (ni < 0)
		strcat(arr[pnum].stat,"<");
	fclose(fp);

	strcat(tmp,"us");
	fp = fopen(tmp, "r");
	fseek(fp,0,SEEK_SET);
	for(int i=0; i<18; i++) {
		fgets(bbuf,64,fp);
	}
	fgets(bbuf,64,fp);
	fclose(fp);
	if (strncmp(bbuf,"VmLck",5) == 0) {
		for(int i=0; i<64; i++) {
			if(bbuf[i] >= '0' && bbuf[i] <= '9') {
				if (atoi(bbuf + i) > 0)
					strcat(arr[pnum].stat, "L");
				break;
			}
		}
	}


	if (arr[pnum].pid == sspid)
		strcat(arr[pnum].stat, "s");

	if (threadN > 1)
		strcat(arr[pnum].stat, "l");

	pid_t mpid = getpgrp();
	if (spid == getpgid(arr[pnum].pid))
		strcat(arr[pnum].stat, "+");
}


/*
   scandir에 쓰이는 filter 함수
   .과 .. 은 받지 않는다
   */
int dfilter(const struct dirent * a) {
	if (strcmp(a->d_name, ".") == 0)
		return 0;
	if (strcmp(a->d_name, "..") == 0)
		return 0;
	return 1;
}

/*
   get_data()
   ps 출력을 위해 데이터를 수집하는 함수
 */
void get_data() {   
	char buf[1024];
	char tmp[100];
	int itmp;

	struct dirent **namelist;
	int size = scandir("/proc",&namelist, myfilter, mysort);
	for(int i=0; i< size; i++) {
		char path[266];
		sprintf(path, "/proc/%s",namelist[i]->d_name);
		if (access(path, R_OK | X_OK) == 0) {
			get_usr(path); // USER
			get_command(path); // COMMAND
			get_mem(path);
			get_time(path);
			get_cpu();
			get_pid(path);
			get_stat(path);
			get_tty(path);
			pnum++;
		}
	}
	for(int i=0; i<size; i++)
		free(namelist[i]);
	free(namelist);
}

/*
   main 함수.
   옵션 파싱후 옵션에 맞추어 출력해준다.
 */

int main(int argc, char *argv[]) {
	struct passwd *pwd = getpwuid(getuid());
	char *mytty = ttyname(0);
	if (argc > 2 ) {
		fprintf(stderr, "input error\n");
		exit(0);
	}
	if (argc == 2 && strlen(argv[1]) > 3) {
		fprintf(stderr, "input error\n");
		exit(0);
	}
	if(argc == 2) {
		for(int i=0; i<strlen(argv[1]); i++) {
			if (argv[1][i] == 'u') option[uoption] = 1;
			if (argv[1][i] == 'a') option[aoption] = 1;
			if (argv[1][i] == 'x') option[xoption] = 1;
		}
	}

	get_data();
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);

	if (option[uoption] == 1)
		snprintf(printbuf, ws.ws_col-1 ,"USER         PID %%CPU %%MEM     VSZ    RSS TTY     STAT START   TIME  COMMAND");
	else if (option[aoption]==1 || option[xoption] == 1)
		snprintf(printbuf, ws.ws_col-1 ,"    PID TTY      STAT   TIME COMNNAD");
	else
		snprintf(printbuf, ws.ws_col-1 ,"    PID TTY          TIME CMD");
	printf("%s\n", printbuf);

	for(int i=0; i<pnum; i++) {
		struct tm *st = (struct tm *)localtime(&arr[i].stime);
		memset(printbuf, 0 , sizeof(printbuf));

		if (option[uoption] == 1)
			snprintf(printbuf,ws.ws_col-1,"%-8s%8d  %.1lf  %.1lf %7u %6u %-8s%-4s %02d:%02d   %2ld:%02ld %s" ,arr[i].user, arr[i].pid, arr[i].pcpu, arr[i].mem,arr[i].vsz,arr[i].rss,arr[i].tty, arr[i].stat ,st->tm_hour,st->tm_min, arr[i].rtime/60, arr[i].rtime%60,arr[i].command);
		else if (option[aoption]==1 || option[xoption] == 1)
			snprintf(printbuf,ws.ws_col-1,"%7d %-8s %-5s %02ld:%02ld %s" , arr[i].pid,arr[i].tty, arr[i].stat , arr[i].rtime/60, arr[i].rtime%60,arr[i].command);
		else
			snprintf(printbuf,ws.ws_col-1,"%7d %-8s %02ld:%02ld:%02ld %s" ,arr[i].pid, arr[i].tty,arr[i].rtime/60/60,  arr[i].rtime/60, arr[i].rtime%60,arr[i].command);

		if (option[aoption]==0 && option[uoption]==0 && option[xoption] ==0){
			if (strcmp(arr[i].tty, mytty+5) != 0) continue;
		}
		else if (option[aoption]==0 && option[uoption]==0 && option[xoption] ==1){
			if (strcmp(arr[i].user, pwd->pw_name) != 0) continue;
		}
		else if (option[aoption]==0 && option[uoption]==1 && option[xoption] ==0){
			if (strcmp(arr[i].user, pwd->pw_name) != 0 || strcmp(arr[i].tty,"?")==0) continue;
		}
		else if (option[aoption]==0 && option[uoption]==1 && option[xoption] ==1){
			if (strcmp(arr[i].user, pwd->pw_name) != 0) continue;
		}
		else if (option[aoption]==1 && option[uoption]==0 && option[xoption] ==0){
			if (strcmp(arr[i].tty,"?")==0) continue;
		}
		else if (option[aoption]==1 && option[uoption]==1 && option[xoption] ==0){
			if (strcmp(arr[i].tty,"?")==0) continue;
		}
		else
			;
		printf("%s\n",printbuf);
	}
}
