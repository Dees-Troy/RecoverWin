int __system(const char *command);
FILE * __popen(const char *program, const char *type);
int __pclose(FILE *iop);

// Device ID variable / function
char device_id[64];
void get_device_id();


int erase_volume(const char *volume);
static long tmplog_offset = 0;

// Battery level
char* print_batt_cap();

void confirm_format(char* volume_name, char* volume_path);

char* zip_verify();
char* reboot_after_flash();

char* save_reboot_setting();
char time_zone_offset_string[5];
char time_zone_dst_string[10];
void update_tz_environment_variables();

// Menu Stuff
#define GO_HOME 69

int go_home;
int go_menu;
int go_restart;
int go_reboot;

char* isMounted(int mInt);
void chkMounts();
int sysIsMounted;
int datIsMounted;
int cacIsMounted;
int sdcIsMounted;
int sdeIsMounted;
char multi_zip_array[10][255];
int multi_zip_index;

int get_new_zip_dir;

void fix_perms();
char* toggle_spam();

void run_script(char *str);
