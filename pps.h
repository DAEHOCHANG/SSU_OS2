/*
   get_usr ~ uptime까지의 함수들은 pps에서 쓰입니다.
   혹은 ttop에서도 쓰입니다. 단 함수 구현은 다르게 되어있을 수 있습니다.
   */
void get_usr(char *path);
void get_command(char *path);
void get_cpu();
void get_mem(char *path);
void get_time(char *path);
void get_pid(char *path);
void get_data();
void get_tty(char *path);
void get_stat(char *path);
double uptime();

/*
   아래 함수들은 ttop에서만 쓰입니다.
   */

int getch(void);
void get_first_line();
void get_second_line();
void get_third_line();
void get_fourth_line();
void set_init();
void show_screen();



