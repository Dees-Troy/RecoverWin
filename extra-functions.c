/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ctype.h>
#include "cutils/misc.h"
#include "cutils/properties.h"
#include <dirent.h>
#include <getopt.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/limits.h>
#include <termios.h>
#include <time.h>

#include "phx_reboot.h"
#include "bootloader.h"
#include "common.h"
#include "extra-functions.h"
#include "cutils/properties.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "minzip/Zip.h"
#include "recovery_ui.h"
#include "roots.h"
#include "ddftw.h"
#include "backstore.h"
#include "data.h"

//kang system() from bionic/libc/unistd and rename it __system() so we can be even more hackish :)
#undef _PATH_BSHELL
#define _PATH_BSHELL "/sbin/sh"

extern char **environ;

// sdcard partitioning variables
char swapsize[32];
int swap;
char extsize[32];
int ext;
int ext_format; // 3 or 4

int
__system(const char *command)
{
  pid_t pid;
	sig_t intsave, quitsave;
	sigset_t mask, omask;
	int pstat;
	char *argp[] = {"sh", "-c", NULL, NULL};

	if (!command)		/* just checking... */
		return(1);

	argp[2] = (char *)command;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &omask);
	switch (pid = vfork()) {
	case -1:			/* error */
		sigprocmask(SIG_SETMASK, &omask, NULL);
		return(-1);
	case 0:				/* child */
		sigprocmask(SIG_SETMASK, &omask, NULL);
		execve(_PATH_BSHELL, argp, environ);
    _exit(127);
  }

	intsave = (sig_t)  bsd_signal(SIGINT, SIG_IGN);
	quitsave = (sig_t) bsd_signal(SIGQUIT, SIG_IGN);
	pid = waitpid(pid, (int *)&pstat, 0);
	sigprocmask(SIG_SETMASK, &omask, NULL);
	(void)bsd_signal(SIGINT, intsave);
	(void)bsd_signal(SIGQUIT, quitsave);
	return (pid == -1 ? -1 : pstat);
}

static struct pid {
	struct pid *next;
	FILE *fp;
	pid_t pid;
} *pidlist;

FILE *
__popen(const char *program, const char *type)
{
	struct pid * volatile cur;
	FILE *iop;
	int pdes[2];
	pid_t pid;

	if ((*type != 'r' && *type != 'w') || type[1] != '\0') {
		errno = EINVAL;
		return (NULL);
	}

	if ((cur = malloc(sizeof(struct pid))) == NULL)
		return (NULL);

	if (pipe(pdes) < 0) {
		free(cur);
		return (NULL);
	}

	switch (pid = vfork()) {
	case -1:			/* Error. */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		free(cur);
		return (NULL);
		/* NOTREACHED */
	case 0:				/* Child. */
	    {
		struct pid *pcur;
		/*
		 * because vfork() instead of fork(), must leak FILE *,
		 * but luckily we are terminally headed for an execl()
		 */
		for (pcur = pidlist; pcur; pcur = pcur->next)
			close(fileno(pcur->fp));

		if (*type == 'r') {
			int tpdes1 = pdes[1];

			(void) close(pdes[0]);
			/*
			 * We must NOT modify pdes, due to the
			 * semantics of vfork.
			 */
			if (tpdes1 != STDOUT_FILENO) {
				(void)dup2(tpdes1, STDOUT_FILENO);
				(void)close(tpdes1);
				tpdes1 = STDOUT_FILENO;
			}
		} else {
			(void)close(pdes[1]);
			if (pdes[0] != STDIN_FILENO) {
				(void)dup2(pdes[0], STDIN_FILENO);
				(void)close(pdes[0]);
			}
		}
		execl(_PATH_BSHELL, "sh", "-c", program, (char *)NULL);
		_exit(127);
		/* NOTREACHED */
	    }
	}

	/* Parent; assume fdopen can't fail. */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void)close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		(void)close(pdes[0]);
	}

	/* Link into list of file descriptors. */
	cur->fp = iop;
	cur->pid =  pid;
	cur->next = pidlist;
	pidlist = cur;

	return (iop);
}

/*
 * pclose --
 *	Pclose returns -1 if stream is not associated with a `popened' command,
 *	if already `pclosed', or waitpid returns an error.
 */
int
__pclose(FILE *iop)
{
	struct pid *cur, *last;
	int pstat;
	pid_t pid;

	/* Find the appropriate file pointer. */
	for (last = NULL, cur = pidlist; cur; last = cur, cur = cur->next)
		if (cur->fp == iop)
			break;

	if (cur == NULL)
		return (-1);

	(void)fclose(iop);

	do {
		pid = waitpid(cur->pid, &pstat, 0);
	} while (pid == -1 && errno == EINTR);

	/* Remove the entry from the linked list. */
	if (last == NULL)
		pidlist = cur->next;
	else
		last->next = cur->next;
	free(cur);

	return (pid == -1 ? -1 : pstat);
}

#define CMDLINE_SERIALNO        "androidboot.serialno="
#define CMDLINE_SERIALNO_LEN    (strlen(CMDLINE_SERIALNO))
#define CPUINFO_SERIALNO        "Serial"
#define CPUINFO_SERIALNO_LEN    (strlen(CPUINFO_SERIALNO))
#define CPUINFO_HARDWARE        "Hardware"
#define CPUINFO_HARDWARE_LEN    (strlen(CPUINFO_HARDWARE))

void get_device_id()
{
	FILE *fp;
    char line[2048];
	char hardware_id[32];
	char* token;

    // Assign a blank device_id to start with
    device_id[0] = 0;

    // First, try the cmdline to see if the serial number was supplied
	fp = fopen("/proc/cmdline", "rt");
	if (fp != NULL)
    {
        // First step, read the line. For cmdline, it's one long line
        fgets(line, sizeof(line), fp);
        fclose(fp);

        // Now, let's tokenize the string
        token = strtok(line, " ");

        // Let's walk through the line, looking for the CMDLINE_SERIALNO token
        while (token)
        {
            // We don't need to verify the length of token, because if it's too short, it will mismatch CMDLINE_SERIALNO at the NULL
            if (memcmp(token, CMDLINE_SERIALNO, CMDLINE_SERIALNO_LEN) == 0)
            {
                // We found the serial number!
                strcpy(device_id, token + CMDLINE_SERIALNO_LEN);
                return;
            }
            token = strtok(NULL, " ");
        }
    }

	// Now we'll try cpuinfo for a serial number
	fp = fopen("/proc/cpuinfo", "rt");
	if (fp != NULL)
    {
		while (fgets(line, sizeof(line), fp) != NULL) { // First step, read the line.
			if (memcmp(line, CPUINFO_SERIALNO, CPUINFO_SERIALNO_LEN) == 0)  // check the beginning of the line for "Serial"
			{
				// We found the serial number!
				token = line + CPUINFO_SERIALNO_LEN; // skip past "Serial"
				while ((*token > 0 && *token <= 32 ) || *token == ':') token++; // skip over all spaces and the colon
				if (*token != 0) {
                    token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
						memset(device_id, 0, sizeof(device_id));
						strncpy(device_id, token, strlen(token) - 1);
					} else {
						strcpy(device_id, token);
					}
					LOGI("=> serial from cpuinfo: '%s'\n", device_id);
					fclose(fp);
					return;
				}
			} else if (memcmp(line, CPUINFO_HARDWARE, CPUINFO_HARDWARE_LEN) == 0) {// We're also going to look for the hardware line in cpuinfo and save it for later in case we don't find the device ID
				// We found the hardware ID
				token = line + CPUINFO_HARDWARE_LEN; // skip past "Hardware"
				while ((*token > 0 && *token <= 32 ) || *token == ':')  token++; // skip over all spaces and the colon
				if (*token != 0) {
                    token[30] = 0;
					if (token[strlen(token)-1] == 10) { // checking for endline chars and dropping them from the end of the string if needed
                        memset(hardware_id, 0, sizeof(hardware_id));
						strncpy(hardware_id, token, strlen(token) - 1);
					} else {
						strcpy(hardware_id, token);
					}
					LOGI("=> hardware id from cpuinfo: '%s'\n", hardware_id);
				}
			}
		}
		fclose(fp);
    }
	
	if (hardware_id[0] != 0) {
		LOGW("\nusing hardware id for device id: '%s'\n", hardware_id);
		strcpy(device_id, hardware_id);
		return;
	}

    strcpy(device_id, "serialno");
	LOGE("=> device id not found, using '%s'.", device_id);
    return;
}


/*partial kangbang from system/vold
TODO: Currently only one mount is supported, defaulting
/mnt/sdcard to lun0 and anything else gets no love. Fix this.
*/
#ifndef CUSTOM_LUN_FILE
#define CUSTOM_LUN_FILE "/sys/devices/platform/usb_mass_storage/lun"
#endif

int usb_storage_enable(void)
{
    int fd;

    Volume *vol = volume_for_path("/sdcard"); 
    if (!vol)
    {
        LOGE("Unable to locate volume information.");
        return -1;
    }

    if ((fd = open(CUSTOM_LUN_FILE"0/file", O_WRONLY)) < 0)
    {
        LOGE("Unable to open ums lunfile: (%s)", strerror(errno));
        return -1;
    }

    if ((write(fd, vol->device, strlen(vol->device)) < 0) &&
        (!vol->device2 || (write(fd, vol->device, strlen(vol->device2)) < 0))) {
        LOGE("Unable to write to ums lunfile: (%s)", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int usb_storage_disable(void)
{
    int fd;

    if ((fd = open(CUSTOM_LUN_FILE"0/file", O_WRONLY)) < 0)
    {
        LOGE("Unable to open ums lunfile: (%s)", strerror(errno));
        return -1;
    }

    char ch = 0;
    if (write(fd, &ch, 1) < 0)
    {
        LOGE("Unable to write to ums lunfile: (%s)", strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void wipe_dalvik_cache()
{
        ensure_path_mounted("/data");
        ensure_path_mounted("/cache");
        ui_print("\n-- Wiping Dalvik Cache Directories...\n");
        __system("rm -rf /data/dalvik-cache");
        ui_print("Cleaned: /data/dalvik-cache...\n");
        __system("rm -rf /cache/dalvik-cache");
        ui_print("Cleaned: /cache/dalvik-cache...\n");
        __system("rm -rf /cache/dc");
        ui_print("Cleaned: /cache/dc\n");

        struct stat st;
        if (0 != stat(sde.blk, &st))
        {
            ui_print("/sd-ext not present, skipping\n");
        } else {
        	__system("mount /sd-ext");
    	    LOGI("Mounting /sd-ext\n");
    	    if (stat("/sd-ext/dalvik-cache",&st) == 0)
    	    {
                __system("rm -rf /sd-ext/dalvik-cache");
        	    ui_print("Cleaned: /sd-ext/dalvik-cache...\n");
    	    }
        }
        ensure_path_unmounted("/data");
        ui_print("-- Dalvik Cache Directories Wipe Complete!\n\n");
}

// BATTERY STATS
void wipe_battery_stats()
{
    ensure_path_mounted("/data");
    struct stat st;
    if (0 != stat("/data/system/batterystats.bin", &st))
    {
        ui_print("No Battery Stats Found. No Need To Wipe.\n");
    } else {
        remove("/data/system/batterystats.bin");
        ui_print("Cleared: Battery Stats...\n");
        ensure_path_unmounted("/data");
    }
}

// ROTATION SETTINGS
void wipe_rotate_data()
{
    ensure_path_mounted("/data");
    __system("rm -r /data/misc/akmd*");
    __system("rm -r /data/misc/rild*");
    ui_print("Cleared: Rotatation Data...\n");
    ensure_path_unmounted("/data");
}   

void fix_perms()
{
	ensure_path_mounted("/data");
	ensure_path_mounted("/system");
	ui_show_progress(1,30);
    ui_print("\n-- Fixing Permissions\n");
	ui_print("This may take a few minutes.\n");
	__system("./sbin/fix_permissions.sh");
	ui_print("-- Done.\n\n");
	ui_reset_progress();
}

// kang'd this from recovery.c cuz there wasnt a recovery.h!
int
erase_volume(const char *volume) {
    ui_print("Formatting %s...\n", volume);

    if (strcmp(volume, "/cache") == 0) {
        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
    }

    return format_volume(volume);
}

int get_battery_level(void)
{
    static int lastVal = -1;
    static time_t nextSecCheck = 0;

    struct timeval curTime;
    gettimeofday(&curTime, NULL);
    if (curTime.tv_sec > nextSecCheck)
    {
        char cap_s[4];
        FILE * cap = fopen("/sys/class/power_supply/battery/capacity","rt");
        if (cap)
        {
            fgets(cap_s, 4, cap);
            fclose(cap);
            lastVal = atoi(cap_s);
            if (lastVal > 100)  lastVal = 100;
            if (lastVal < 0)    lastVal = 0;
        }
        nextSecCheck = curTime.tv_sec + 60;
    }
    return lastVal;
}

char* 
print_batt_cap()  {
	char* full_cap_s = (char*)malloc(30);
	char full_cap_a[30];
	
	int cap_i = get_battery_level();
    
    //int len = strlen(cap_s);
	//if (cap_s[len-1] == '\n') {
	//	cap_s[len-1] = 0;
	//}
	
	// Get a usable time
	struct tm *current;
	time_t now;
	now = time(0);
	current = localtime(&now);
	
	sprintf(full_cap_a, "Battery Level: %i%% @ %02d:%02d", cap_i, current->tm_hour, current->tm_min);
	strcpy(full_cap_s, full_cap_a);
	
	return full_cap_s;
}

void update_tz_environment_variables() {
    setenv("TZ", DataManager_GetStrValue(VAR_TIME_ZONE_VAR), 1);
    tzset();
}

#define MNT_SYSTEM 	0
#define MNT_DATA	1
#define MNT_CACHE	2
#define MNT_SDCARD	3
#define MNT_SDEXT	4
#define MNT_BACK	5

void chkMounts()
{
	FILE *fp;
	char tmpOutput[255];
	sysIsMounted = 0;
	datIsMounted = 0;
	cacIsMounted = 0;
	sdcIsMounted = 0;
	sdeIsMounted = 0;
	fp = __popen("cat /proc/mounts", "r");
	while (fgets(tmpOutput,255,fp) != NULL)
	{
	    sscanf(tmpOutput,"%s %*s %*s %*s %*d %*d",tmp.blk);
	    if (strcmp(tmp.blk,sys.blk) == 0)
	    {
	    	sysIsMounted = 1;
	    }
	    if (strcmp(tmp.blk,dat.blk) == 0)
	    {
	    	datIsMounted = 1;
	    }
	    if (strcmp(tmp.blk,cac.blk) == 0)
	    {
	    	cacIsMounted = 1;
	    }
	    if (strcmp(tmp.blk,sdcext.blk) == 0)
	    {
	    	sdcIsMounted = 1;
	    }
	    if (strcmp(tmp.blk,sde.blk) == 0)
	    {
	    	sdeIsMounted = 1;
	    }
	}
	__pclose(fp);
}

char* isMounted(int mInt)
{
	int isTrue = 0;
	struct stat st;
	char* tmp_set = (char*)malloc(25);
	if (mInt == MNT_SYSTEM)
	{
	    if (sysIsMounted == 1) {
			strcpy(tmp_set, "unmount");
	    } else {
			strcpy(tmp_set, "mount");
	    }
		strcat(tmp_set, " /system");
	}
	if (mInt == MNT_DATA)
	{
	    if (datIsMounted == 1) {
			strcpy(tmp_set, "unmount");
	    } else {
			strcpy(tmp_set, "mount");
	    }
		strcat(tmp_set, " /data");
	}
	if (mInt == MNT_CACHE)
	{
	    if (cacIsMounted == 1) {
			strcpy(tmp_set, "unmount");
	    } else {
			strcpy(tmp_set, "mount");
	    }
		strcat(tmp_set, " /cache");
	}
	if (mInt == MNT_SDCARD)
	{
	    if (sdcIsMounted == 1) {
			strcpy(tmp_set, "unmount");
	    } else {
			strcpy(tmp_set, "mount");
	    }
		strcat(tmp_set, " /sdcard");
	}
	if (mInt == MNT_SDEXT)
	{
		if (stat(sde.blk,&st) != 0)
		{
			strcpy(tmp_set, "-------");
			sdeIsMounted = -1;
		} else {
		    if (sdeIsMounted == 1) {
				strcpy(tmp_set, "unmount");
		    } else {
				strcpy(tmp_set, "mount");
		    }
		}
		strcat(tmp_set, " /sd-ext");
	}
	return tmp_set;
}

/*
    Checks md5 for a path
    Return values:
        -3 : Different file name in MD5 than provided
        -2 : Zip does not exist
        -1 : MD5 does not exist
        0 : Failed
        1 : Success
*/
int check_md5(char* path) {
    char cmd[PATH_MAX + 30];
    sprintf(cmd, "/sbin/md5check.sh \"%s\"", path);
    
    //ui_print("\nMD5 Command: %s", cmd);
    
    FILE * cs = __popen(cmd, "r");
    char cs_s[7];
    fgets(cs_s, 7, cs);

    //ui_print("\nMD5 Message: %s", cs_);
    __pclose(cs);
    
    int o = -99;
    if (strncmp(cs_s, "OK", 2) == 0)
        o = 1;
    else if (strncmp(cs_s, "FAILURE", 7) == 0)
        o = 0;
    else if (strncmp(cs_s, "NO5", 3) == 0)
        o = -1;
    else if (strncmp(cs_s, "NOZ", 3) == 0)
        o = -2;
    else if (strncmp(cs_s, "DF", 2) == 0)
        o = -3;
    else // Unknown error
        o = -99;

    return o;
}

void run_script(char *str)
{
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = { "/sbin/sh", "-c", str, "1>&2", NULL };
        execv("/sbin/sh", args);
        _exit(-1);
    }
    int status;
    while (waitpid(pid, &status, WNOHANG) == 0) {
        ui_print(".");
        usleep(1000000);
    }
    ui_print("\n");
}
