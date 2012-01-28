/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "format.h"
#include "recovery_ui.h"
#include "encryptedfs_provisioning.h"
#include "phx_reboot.h"

#include "extra-functions.h"
#include "data.h"
#include "ddftw.h"
#include "backstore.h"

int notError;

int gui_init(void);
int gui_loadResources(void);
int gui_start(void);
int gui_console_only(void);

int check_md5(char* path);

static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 's' },
  { "update_package", required_argument, NULL, 'u' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_cache", no_argument, NULL, 'c' },
  { "set_encrypted_filesystems", required_argument, NULL, 'e' },
  { "show_text", no_argument, NULL, 't' },
  { NULL, 0, NULL, 0 },
};

static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
//static const char *SDCARD_ROOT = "/sdcard";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *SIDELOAD_TEMP_DIR = "/tmp/sideload";

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /data
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_volume() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 *
 * SECURE FILE SYSTEMS ENABLE/DISABLE
 * 1. user selects "enable encrypted file systems"
 * 2. main system writes "--set_encrypted_filesystems=on|off" to
 *    /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and
 *    "--set_encrypted_filesystems=on|off"
 *    -- after this, rebooting will restart the transition --
 * 5. read_encrypted_fs_info() retrieves encrypted file systems settings from /data
 *    Settings include: property to specify the Encrypted FS istatus and
 *    FS encryption key if enabled (not yet implemented)
 * 6. erase_volume() reformats /data
 * 7. erase_volume() reformats /cache
 * 8. restore_encrypted_fs_info() writes required encrypted file systems settings to /data
 *    Settings include: property to specify the Encrypted FS status and
 *    FS encryption key if enabled (not yet implemented)
 * 9. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 10. main() calls reboot() to boot main system
 */

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

// open a given path, mounting partitions as necessary
FILE*
fopen_path(const char *path, const char *mode) {
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return NULL;
    }

    // When writing, try to create the containing directory, if necessary.
    // Use generous permissions, the system (init.rc) will reset them.
    if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1);

    FILE *fp = fopen(path, mode);
    return fp;
}

// close a file, log an error if the error indicator is set
static void
check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    set_bootloader_message(&boot);
}

static void
set_sdcard_update_bootloader_message() {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
}

// How much of the temp log we have copied to the copy in cache.
static void
copy_log_file(const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(TEMPORARY_LOG_FILE, "r");
        if (tmplog == NULL) {
            LOGE("Can't open %s\n", TEMPORARY_LOG_FILE);
        } else {
            if (append) {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            if (append) {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, TEMPORARY_LOG_FILE);
        }
        check_and_fclose(log, destination);
    }
}


// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(LOG_FILE, true);
    copy_log_file(LAST_LOG_FILE, false);
    chmod(LAST_LOG_FILE, 0640);

    // Reset to mormal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (ensure_path_mounted(COMMAND_FILE) != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

    sync();  // For good measure.
}

static char*
copy_sideloaded_package(const char* original_path) {
  if (ensure_path_mounted(original_path) != 0) {
    LOGE("Can't mount %s\n", original_path);
    return NULL;
  }

  if (ensure_path_mounted(SIDELOAD_TEMP_DIR) != 0) {
    LOGE("Can't mount %s\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }

  if (mkdir(SIDELOAD_TEMP_DIR, 0700) != 0) {
    if (errno != EEXIST) {
      LOGE("Can't mkdir %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
      return NULL;
    }
  }

  // verify that SIDELOAD_TEMP_DIR is exactly what we expect: a
  // directory, owned by root, readable and writable only by root.
  struct stat st;
  if (stat(SIDELOAD_TEMP_DIR, &st) != 0) {
    LOGE("failed to stat %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
    return NULL;
  }
  if (!S_ISDIR(st.st_mode)) {
    LOGE("%s isn't a directory\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }
  if ((st.st_mode & 0777) != 0700) {
    LOGE("%s has perms %o\n", SIDELOAD_TEMP_DIR, st.st_mode);
    return NULL;
  }
  if (st.st_uid != 0) {
    LOGE("%s owned by %lu; not root\n", SIDELOAD_TEMP_DIR, st.st_uid);
    return NULL;
  }

  char copy_path[PATH_MAX];
  strcpy(copy_path, SIDELOAD_TEMP_DIR);
  strcat(copy_path, "/package.zip");

  char* buffer = malloc(BUFSIZ);
  if (buffer == NULL) {
    LOGE("Failed to allocate buffer\n");
    return NULL;
  }

  size_t read;
  FILE* fin = fopen(original_path, "rb");
  if (fin == NULL) {
    LOGE("Failed to open %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }
  FILE* fout = fopen(copy_path, "wb");
  if (fout == NULL) {
    LOGE("Failed to open %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  while ((read = fread(buffer, 1, BUFSIZ, fin)) > 0) {
    if (fwrite(buffer, 1, read, fout) != read) {
      LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
      return NULL;
    }
  }

  free(buffer);

  if (fclose(fout) != 0) {
    LOGE("Failed to close %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  if (fclose(fin) != 0) {
    LOGE("Failed to close %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }

  // "adb push" is happy to overwrite read-only files when it's
  // running as root, but we'll try anyway.
  if (chmod(copy_path, 0400) != 0) {
    LOGE("Failed to chmod %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  return strdup(copy_path);
}

static int compare_string(const void* a, const void* b) {
    return strcasecmp(*(const char**)a, *(const char**)b);
}

int install_zip_package(const char* zip_path_filename) {
	int result;
	
    ensure_path_mounted(SDCARD_ROOT);
	ui_print("\n-- Verify md5 for %s", zip_path_filename);
	int md5chk = check_md5((char*) zip_path_filename);
	bool md5_req = DataManager_GetIntValue(VAR_FORCE_MD5_CHECK_VAR);
	if (md5chk > 0 || (!md5_req && md5chk == -1)) {
		if (md5chk == 1)
			ui_print("\n-- Md5 verified, continue");
		else if (md5chk == -1)
			ui_print("\n-- No md5 file found, ignoring");
		ui_print("\n-- Install %s ...\n", zip_path_filename);
		set_sdcard_update_bootloader_message();
		char* copy = copy_sideloaded_package(zip_path_filename);
		ensure_path_unmounted(SDCARD_ROOT);
		if (copy) {
			result = install_package(copy);
			free(copy);
		} else {
			result = INSTALL_ERROR;
		}
	} else {
	// MD5 check failed for some reason
		switch (md5chk) {
			case 0:
				ui_print("\n-- Md5 did not match");
				break;
			case -1:
				ui_print("\n-- Md5 file not found");
				break;
			case -2:
				ui_print("\n-- Zip file not found");
				break;
			case -3:
				ui_print("\n-- Invalid md5");
				ui_print("\n-- Filename in md5 and zip do not match");
				break;
            default:
                ui_print("\n-- Unknown md5 error");
                break;
		}
		ui_print("\n-- Aborting install");
		result = INSTALL_ERROR;
	}
    ensure_path_mounted(SDCARD_ROOT);
    finish_recovery(NULL);
	return result;
}

void
wipe_data(int confirm) {
    ui_print("\n-- Factory reset started.\n");
    ui_print("Formatting /data...\n");

    //device_wipe_data(); // ??

    // For the Tuna boards, we can't do this! The sdcard is actually /data/media
#ifdef RECOVERY_SDCARD_ON_DATA
    phx_mount(dat);
    __system("rm -f /data/*");
    __system("rm -f /data/.*");

    DIR* d;
    d = opendir("/data");
    if (d != NULL)
    {
        struct dirent* de;
        while ((de = readdir(d)) != NULL)
        {
            if (strcmp(de->d_name, "media") == 0)   continue;

            char cmd[256];
            sprintf(cmd, "rm -fr /data/%s", de->d_name);
            __system(cmd);
        }
        closedir(d);
    }
    phx_unmount(dat);
#else
    erase_volume("/data");
#endif
    ui_print("Formatting /cache...\n");
    erase_volume("/cache");
    struct stat st;
    if (stat(sde.blk,&st) == 0) {
        ui_print("Formatting /sd-ext...\n");
        phx_format(sde.fst,sde.blk);
    }
    if (stat("/sdcard/.android_secure", &st) == 0) {
        ui_print("Formatting /sdcard/.android_secure...\n");
        __system("rm -rf /sdcard/.android_secure/* && rm -rf /sdcard/.android_secure/.*");
    }
	ui_reset_progress();
    ui_print("-- Factory reset complete.\n");
}


static void
print_property(const char *key, const char *name, void *cookie) {
    printf("%s=%s\n", key, name);
}

int
main(int argc, char **argv) {
    time_t start = time(NULL);

    // If these fail, there's not really anywhere to complain...
    freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
    freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);
    printf("Starting recovery on %s", ctime(&start));

    // Fire up the UI engine
    gui_init();

    printf("Loading volume table...\n");
    load_volume_table();

    // Load up all the resources
    gui_loadResources();

    printf("Processing arguments (%d)...\n", argc);
    get_args(&argc, &argv);

    LOGI("=> Installing busybox into /sbin\n");
	__system("/sbin/busybox --install -s /sbin"); // Let's install busybox
	LOGI("=> Linking mtab\n");
	__system("ln -s /proc/mounts /etc/mtab"); // And link mtab for mke2fs
	LOGI("=> Getting locations\n");
    if (getLocations())
    {
        LOGE("Failing out of recovery.\n");
        return -1;
    }

    int previous_runs = 0;
    const char *send_intent = NULL;
    const char *update_package = NULL;
    const char *encrypted_fs_mode = NULL;
    int wipe_data = 0, wipe_cache = 0;
    int toggle_secure_fs = 0;
    encrypted_fs_info encrypted_fs_data;

    int arg;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
        switch (arg) {
        case 'p': previous_runs = atoi(optarg); break;
        case 's': send_intent = optarg; break;
        case 'u': update_package = optarg; break;
        case 'w': wipe_data = wipe_cache = 1; break;
        case 'c': wipe_cache = 1; break;
        case 'e': encrypted_fs_mode = optarg; toggle_secure_fs = 1; break;
        case '?':
            LOGE("Invalid command argument\n");
            continue;
        }
    }

    device_recovery_start();

    printf("Command:");
    for (arg = 0; arg < argc; arg++) {
        printf(" \"%s\"", argv[arg]);
    }
    printf("\n");

    if (update_package) {
        // For backwards compatibility on the cache partition only, if
        // we're given an old 'root' path "CACHE:foo", change it to
        // "/cache/foo".
        if (strncmp(update_package, "CACHE:", 6) == 0) {
            int len = strlen(update_package) + 10;
            char* modified_path = malloc(len);
            strlcpy(modified_path, "/cache/", len);
            strlcat(modified_path, update_package+6, len);
            printf("(replacing path \"%s\" with \"%s\")\n",
                   update_package, modified_path);
            update_package = modified_path;
        }
    }
    printf("\n");

    property_list(print_property, NULL);
    printf("\n");

    int status = INSTALL_SUCCESS;

    if (toggle_secure_fs) {
        gui_console_only();
        if (strcmp(encrypted_fs_mode,"on") == 0) {
            encrypted_fs_data.mode = MODE_ENCRYPTED_FS_ENABLED;
            ui_print("Enabling Encrypted FS.\n");
        } else if (strcmp(encrypted_fs_mode,"off") == 0) {
            encrypted_fs_data.mode = MODE_ENCRYPTED_FS_DISABLED;
            ui_print("Disabling Encrypted FS.\n");
        } else {
            ui_print("Error: invalid Encrypted FS setting.\n");
            status = INSTALL_ERROR;
        }

        // Recovery strategy: if the data partition is damaged, disable encrypted file systems.
        // This preventsthe device recycling endlessly in recovery mode.
        if ((encrypted_fs_data.mode == MODE_ENCRYPTED_FS_ENABLED) &&
                (read_encrypted_fs_info(&encrypted_fs_data))) {
            ui_print("Encrypted FS change aborted, resetting to disabled state.\n");
            encrypted_fs_data.mode = MODE_ENCRYPTED_FS_DISABLED;
        }

        if (status != INSTALL_ERROR) {
            if (erase_volume("/data")) {
                ui_print("Data wipe failed.\n");
                status = INSTALL_ERROR;
            } else if (erase_volume("/cache")) {
                ui_print("Cache wipe failed.\n");
                status = INSTALL_ERROR;
            } else if ((encrypted_fs_data.mode == MODE_ENCRYPTED_FS_ENABLED) &&
                      (restore_encrypted_fs_info(&encrypted_fs_data))) {
                ui_print("Encrypted FS change aborted.\n");
                status = INSTALL_ERROR;
            } else {
                ui_print("Successfully updated Encrypted FS.\n");
                status = INSTALL_SUCCESS;
            }
        }
    } else if (update_package != NULL) {
        gui_console_only();
        status = install_package(update_package);
        if (status != INSTALL_SUCCESS) ui_print("Installation aborted.\n");
    } else if (wipe_data) {
        gui_console_only();
        if (device_wipe_data()) status = INSTALL_ERROR;
        if (erase_volume("/data")) status = INSTALL_ERROR;
        if (wipe_cache && erase_volume("/cache")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) ui_print("Data wipe failed.\n");
    } else if (wipe_cache) {
        gui_console_only();
        if (wipe_cache && erase_volume("/cache")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) ui_print("Cache wipe failed.\n");
    } else {
        status = INSTALL_ERROR;  // No command specified
    }

    if (status != INSTALL_SUCCESS) { // We only want to show menu if error && visible

        // Load up the values - Sleep to let the card be ready
        usleep(500000);
        if (ensure_path_mounted("/sdcard") < 0)
        {
            usleep(500000);
            if (ensure_path_mounted("/sdcard") < 0)
                LOGE("Unable to mount sdcard\n");
        }
        mkdir("/sdcard/phoenix", 0777);
        DataManager_LoadValues("/sdcard/phoenix/.settings");

        // Update some of the main data
        update_tz_environment_variables();

        // This clears up a bug about reboot coming back to recovery with the GUI
        finish_recovery(NULL);

        gui_start();
    }

    // Otherwise, get ready to boot the main system...
    finish_recovery(send_intent);
    ui_print("Rebooting...\n");
    sync();
    reboot(RB_AUTOBOOT);
    return EXIT_SUCCESS;
}
