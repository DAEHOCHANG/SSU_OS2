#include <stdio.h>
#include <utmp.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>
#include "pps.h"

/*
pnum : 프로세스 수
printbuf : 출력 버퍼
x : 방향키 스크롤의 좌표 (0 ~ pnum-1)
   */
int pnum=0;
char printbuf[1024];
int x=0;
/*
   struct data
   ps시 출력할 데이터를 모아둔 구조체
user : 프로세스 유저이름
pcpu : cpu 사용량
mem  : mem사용량
pr   : priority값
ni   : nice값
vsz  : 총 가상메모리 사이즈
rss  : 사용 메모리
shr  : 공유된 메모리
tty  : 
stat : 프로세스 상태
stime: 프로세스 시작시간
rtime: 프로세스 총 cpu 사용시간
command: 명령어
 */
struct data { 
	char user[64];
	int pid;
	double pcpu;
	double mem;
	int pr;
	int ni;
	unsigned int vsz;
	unsigned int rss;
	unsigned int shr;
	char tty[64];
	char stat[10];
	time_t stime;
	time_t rtime;
	time_t rhtime;
	char command[128];
};
/*
   struct tdata
   모든 프로세스들에 대한 데이터.
   */
struct tdata {
	struct tm *curtime; // 현재시간
	time_t sbtime;		//시스템 부팅된지 얼마나 되었는가
	int usern;			// /var/run/user라고 생각중, 아니면 /var/run/utmp
	double loadAvg[3]; // /proc/loadavg파일
	int tasks[5]; // total/running/sleeping/stopped/zombie;
	double cpus[8]; // us/sy/ni/id/wa/hi/si/st 
	long long int cpusi[9]; // us/sy/ni/id/wa/hi/si/st /total file integer
	double mem[4]; // total/free/used/buff/cache
	double swap[4]; //total/free/used/avail
};

/*
struct data arr   : 프로세스들 배열
struct tdata Tdata: top 맨위5줄 정보가 든 구조체
struct winsize wd : 터미널 사이즈 정보 가진 구조체
   */
struct data arr[5000];
struct tdata Tdata;
struct winsize ws;

/*
   void clear()
   화면을 비워주는 함수
   */
void clear() {
	write(1,"\033[2J\033[1;1H",11);
}

/*
   int getch(void)
   사용자로부터 한글자 입력받는 함수
   화면에 쓰이지 않도록 설정해 준다.
   */
int getch(void) {
	int ch;
	struct termios buf;
	struct termios save;
	tcgetattr(0, &save);
	buf = save;
	buf.c_lflag &= ~ICANON;
	buf.c_lflag &= ~ECHO;
	
	tcsetattr(0,TCSAFLUSH,&buf);
	ch = getchar();
	tcsetattr(0,TCSAFLUSH,&save);
	return ch;
}


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
   /proc/pid/stat에서 command를 읽은후
   () 를 빼주어 저장한다.
 */
void get_command(char *path) {
	FILE *fp;
	char tmp[64];
	strcpy(tmp, path);
	strcat(tmp, "/stat");
	fp = fopen(tmp, "r");
	while(fgetc(fp) != ' ');
	fscanf(fp,"(%s",arr[pnum].command);
	arr[pnum].command[strlen(arr[pnum].command) -1] = '\0';
	fclose(fp);
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
   %MEM 와 VIRT, RES를 구하는 함수
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
		arr[pnum].shr=0;
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

	while(count < 21) {
		if (fgetc(fp) == '\n')
			count++;
	}
	fgets(btmp,50,fp);
	for(int i=0; i<50; i++) {
		if (btmp[i] >= '0' && btmp[i] <= '9') {
			arr[pnum].shr = atoi(btmp + i);
			break;
		}
	}
	fgets(btmp,50,fp);
	for(int i=0; i<50; i++) {
		if (btmp[i] >= '0' && btmp[i] <= '9') {
			arr[pnum].shr += atoi(btmp + i);
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
	arr[pnum].rhtime = (systime + utime) % sysconf(_SC_CLK_TCK);
	arr[pnum].stime = time(NULL) - uptime() + (stime / sysconf(_SC_CLK_TCK));
	fclose(fp);
}
/*
   void get_pid(char *path)
   /proc/pid/stat 1번째에 pid값을 추출
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
   void get_stat(char *path)
   프로세스의 stat, pr,ni값을 구한다.
   또한 전체 프로세스 정보를 위해서
   running,stopped, zombie프로세스 수 또한 계산해 준다.
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
	if (strcmp(arr[pnum].stat, "T") == 0)
		Tdata.tasks[3]++;
	else if (strcmp(arr[pnum].stat, "t") == 0)
		Tdata.tasks[3]++;
	else if (strcmp(arr[pnum].stat, "Z") == 0)
		Tdata.tasks[4]++;
	else if (strcmp(arr[pnum].stat, "R") == 0)
		Tdata.tasks[1]++;
	else if (strcmp(arr[pnum].stat, "D") == 0)
		Tdata.tasks[2]++;
	else if (strcmp(arr[pnum].stat, "S") == 0)
		Tdata.tasks[2]++;
	else 
		;
	for(int i=0; i<3; i++) {
		while(fgetc(fp) != ' ');
	}

	for(int i=0; i<12; i++) {
		while(fgetc(fp) != ' ');
	}
	fscanf(fp, "%ld", &pr);
	fscanf(fp, "%ld", &ni);
	arr[pnum].ni = ni;
	arr[pnum].pr = pr;
	fclose(fp);
}

/*
   scandir에 쓰이는 filter 함수
   .과 .. 을 제외하고 받는다.
   */
int dfilter(const struct dirent * a) {
	if (strcmp(a->d_name, ".") == 0)
		return 0;
	if (strcmp(a->d_name, "..") == 0)
		return 0;
	return 1;
}

/*
   get_first_line();
   ttop 첫줄을 위한 데이터를 얻는 함수
   현재시간 , 부팅후 경과시간, 유저수, load average을 구하는 함수
   현재시간은 time함수로
   부팅후 경과시간은 uptime()함수로
   유저수는 /var/run/user안 디렉토리 수로
   load average는 /proc/loadavg파일에서 구한다.
   */

void get_first_line() {
	struct dirent ** list;
	time_t t;
	int size=0;
	FILE *fp;
	time(&t);
	Tdata.curtime = localtime(&t);
	Tdata.sbtime = uptime();
		
	struct utmp* utmpfp;
	setutent();
	Tdata.usern=0;
	while ((utmpfp = getutent()) != NULL) {
		if (utmpfp -> ut_type == USER_PROCESS)
			Tdata.usern++;
	}
	endutent();


	fp = fopen("/proc/loadavg", "r");
	fscanf(fp,"%lf%lf%lf",&Tdata.loadAvg[0],&Tdata.loadAvg[1],&Tdata.loadAvg[2]);
	fclose(fp);
}
/*
   get_second_line();
   ttop 두번째 줄을 위한 데이터를 얻는함수
   대부분 데이터는 get_data내 반복문에 있는 함수들에서 얻어짐으로
   부족한부분만 계산해준다.
   */
void get_second_line() {
	Tdata.tasks[0] = pnum;
}
void get_third_line() {
	FILE *fp = fopen("/proc/stat","r");
	while(fgetc(fp) != ' ');
	long long int total=0;
	long long int tmp[8];
	fscanf(fp, "%lld", &tmp[0]); // us
	fscanf(fp, "%lld", &tmp[2]); // ni
	fscanf(fp, "%lld", &tmp[1]); // sys
	fscanf(fp, "%lld", &tmp[3]); // idle
	fscanf(fp, "%lld", &tmp[4]); // iowait
	fscanf(fp, "%lld", &tmp[5]); // hi
	fscanf(fp, "%lld", &tmp[6]); // si
	fscanf(fp, "%lld", &tmp[7]); // st
	for(int i=0; i<8;i++)
		total+=tmp[i];
	for(int i=0; i<8; i++) {
		Tdata.cpus[i] = ((double)tmp[i] - Tdata.cpusi[i])/(total - Tdata.cpusi[8]) * 100;
	}
	for(int i=0; i<8; i++) {
		Tdata.cpusi[i] = tmp[i];
	}
	Tdata.cpusi[8] = total;

	fclose(fp);
}
void get_fourth_line() {
	FILE *fp;
	char tbuf[64];
	fp = fopen("/proc/meminfo", "r");
	
	fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.mem[0] = atoi(tbuf + i)/1024.0;
			break;
		}
	}
	fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.mem[1] = atoi(tbuf + i)/1024.0;
			break;
		}
	}

	fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.swap[3] = atoi(tbuf + i)/1024.0;
			break;
		}
	}

	fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.mem[3] = atoi(tbuf + i)/1024.0;
			break;
		}
	}
	fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.mem[3] += atoi(tbuf + i)/1024.0;
			break;
		}
	}


	for(int i=0; i<10; i++)
		fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.swap[0] = atoi(tbuf + i)/1024.0;
			break;
		}
	}
	fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.swap[1] = atoi(tbuf + i)/1024.0;
			break;
		}
	}


	for(int i=0; i<8;i++)
		fgets(tbuf,64,fp);
	for (int i=0; i<64; i++) {
		if (tbuf[i] >='0' && tbuf[i] <='9') {
			Tdata.mem[3] += atoi(tbuf + i)/1024.0;
			break;
		}
	}
	Tdata.mem[2] = Tdata.mem[0] - Tdata.mem[1] - Tdata.mem[3];
	Tdata.swap[2] = Tdata.swap[0] - Tdata.swap[1];
	fclose(fp);
}

void set_init() {
	pnum = 0;
	memset(Tdata.tasks, 0,sizeof(Tdata.tasks));
	memset(arr,0,sizeof(arr));
}

/*
   get_data()
   ps 출력을 위해 데이터를 수집하는 함수
 */
void get_data() {   
	set_init();
	get_first_line();
	get_third_line();
	get_fourth_line();
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
		//	get_tty(path);
			pnum++;
		}
	}
	get_second_line();
	for(int i=0; i<size; i++)
		free(namelist[i]);
	free(namelist);
}

/*
   스크린 출력함수
   ioctl함수로 터미널의 크기를 측정 한 후
   snprintf로 터미널 길이만큼을 버퍼에 써준뒤 출력해준다.
   각 프로세스를 출력할 때에는
   i = j+x를 통해서 x번째부터 출력할수 있게 했으며
   i가 pnum보다 크거나 같은경우는 출력하지 못함으로 종료해준다.
   반복문을 통해서 터미널 끝까지만 (넘치지 않게) 출력 해 준다.
 */
void show_screen() {
	struct passwd *pwd = getpwuid(getuid());
	char *mytty = ttyname(0);
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	clear();
	//up
	if (Tdata.sbtime/60/60 >0)
		snprintf(printbuf, ws.ws_col, "top - %02d:%02d:%02d up %2ld:%02ld,%3d user,  load average: %5.2lf,%5.2lf,%5.2lf",Tdata.curtime->tm_hour, Tdata.curtime->tm_min, Tdata.curtime->tm_sec,Tdata.sbtime/60/60, (Tdata.sbtime/60)%60, Tdata.usern, Tdata.loadAvg[0],Tdata.loadAvg[1],Tdata.loadAvg[2]);
	else
		snprintf(printbuf, ws.ws_col, "top - %02d:%02d:%02d up %2ld min,%3d user,  load average: %5.2lf,%5.2lf,%5.2lf",Tdata.curtime->tm_hour, Tdata.curtime->tm_min, Tdata.curtime->tm_sec, (Tdata.sbtime/60)%60, Tdata.usern, Tdata.loadAvg[0],Tdata.loadAvg[1],Tdata.loadAvg[2]);
	printf("%s\n", printbuf);

	snprintf(printbuf, ws.ws_col, "Tasks:%4d total,%4d running,%4d sleeping,%4d stopped,%4d zombie",Tdata.tasks[0],Tdata.tasks[1], Tdata.tasks[2],Tdata.tasks[3],Tdata.tasks[4]);
	printf("%s\n",printbuf);

	snprintf(printbuf,ws.ws_col, "%%Cpu(s):%3.1lf us,%3.1lf sy,%3.1lf ni,%3.1lf id,%3.1lf wa,%3.1f hi,%3.1lf si,%3.1lf st",Tdata.cpus[0],Tdata.cpus[1], Tdata.cpus[2],Tdata.cpus[3],Tdata.cpus[4], Tdata.cpus[5],Tdata.cpus[6], Tdata.cpus[7]);
	printf("%s\n",printbuf);

	snprintf(printbuf,ws.ws_col, "MiB Mem :%9.1lf total,%9.1lf free,%9.1lf used,%9.1lf buff/cache", Tdata.mem[0], Tdata.mem[1], Tdata.mem[2], Tdata.mem[3]);
	printf("%s\n", printbuf);

	snprintf(printbuf,ws.ws_col, "MiB Swap:%9.1lf total,%9.1lf free,%9.1lf used,%9.1lf avail Mem", Tdata.swap[0], Tdata.swap[1], Tdata.swap[2], Tdata.swap[3]);
	printf("%s\n", printbuf);
	printf("\n");
	
	//down
	snprintf(printbuf, ws.ws_col,"    PID USER      PR  NI    VIRT    RES    SHR S  %%CPU  %%MEM     TIME+ COMMAND");
	printf("%s\n", printbuf);

	for(int j=0; j<ws.ws_row-8; j++) {
		int i = j + x;
		if (i >= pnum)
			break;

		struct tm *st = (struct tm *)localtime(&arr[i].stime);
		memset(printbuf, 0 , sizeof(printbuf));


		if (arr[i].pr == -100)
			snprintf(printbuf,ws.ws_col,"%7d %-8s %3s %3d %7d %6d %6d %s %5.1lf %5.1lf %3ld:%02ld.%02ld %s" ,arr[i].pid, arr[i].user, "rt",arr[i].ni,arr[i].vsz ,arr[i].rss,arr[i].shr,arr[i].stat,arr[i].pcpu,arr[i].mem ,arr[i].rtime/60, arr[i].rtime%60,arr[i].rhtime,arr[i].command);
		else
			snprintf(printbuf,ws.ws_col,"%7d %-8s %3d %3d %7d %6d %6d %s %5.1lf %5.1lf %3ld:%02ld.%02ld %s" ,arr[i].pid, arr[i].user, arr[i].pr,arr[i].ni,arr[i].vsz ,arr[i].rss,arr[i].shr,arr[i].stat,arr[i].pcpu,arr[i].mem ,arr[i].rtime/60, arr[i].rtime%60,arr[i].rhtime,arr[i].command);

		printf("%s\n",printbuf);
	}
}

/*
   void signal_handler(int signo)
   3초 후 SIGALRM이 온 경우 화면 갱신해준뒤 alarm(3)
   터미널 크기가 바뀌어 SIGWINCH가 온 경우 화면 갱신해 주었다.
   */
void signal_handler(int signo) {
	if (signo == SIGALRM) {
		get_data();
		show_screen();
		alarm(3);
	}
	else if (signo == SIGWINCH) {
		get_data();
		show_screen();
	}
	else
		;
}
/*
   방향키가 들어오면
   좌표를 설정 해 준뒤 화면을 갱신한다.(상하만 가능)
   좌표를 설정할때 좌표X는 0~pnum-1로 고정해준다.
   q가 나오는경우 프로그램 종료
   */
int main(int argc, char *argv[]) {
	if (argc > 1 ) {
		fprintf(stderr, "input error\n");
		exit(0);
	}
	int k;
	signal(SIGALRM, signal_handler);
	signal(SIGWINCH, signal_handler);
	get_data();
	show_screen();
	alarm(3);
	while (k = getch()) {
		if (k == 27) {
			k = getch();
			if (k=='[') {
				k=getch();
				if (k == 'A') x--;
				else if(k== 'B')x++;
				else continue;
				get_data();
				if (pnum <= x)
					x = pnum - 1;
				if (x < 0)
					x = 0;
				show_screen();
			}
		}
		if (k == 'q')
			break;
	}
	clear();
}
