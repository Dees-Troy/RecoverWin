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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/vfs.h>
#include <sys/mount.h>

#include "phx_reboot.h"
#include "backstore.h"
#include "ddftw.h"
#include "extra-functions.h"
#include "roots.h"
#include "format.h"
#include "data.h"

int getWordFromString(int word, const char* string, char* buffer, int bufferLen)
{
    char* start = NULL;

    do
    {
        // Ignore leading whitespace
        while (*string > 0 && *string < 33)     ++string;

        start = (char*) string;
        while (*string > 32)                    ++string;
    } while (--word > 0);

    // Handle word not found
    if (*start == '\0')
    {
        buffer[0] = '\0';
        return 0;
    }

    memset(buffer, 0, bufferLen);
    if ((string - start) > bufferLen)
        memcpy(buffer, start, bufferLen-1);
    else
        memcpy(buffer, start, string - start);

    return (string - start);
}

void SetDataState(char* operation, char* partition, int errorCode, int done)
{
    DataManager_SetStrValue("_operation", operation);
    DataManager_SetStrValue("_partition", partition);
    DataManager_SetIntValue("_operation_status", errorCode);
    DataManager_SetIntValue("_operation_state", done);
}

#define ITEM_NAN_BACKUP		0
#define ITEM_NAN_SYSTEM		1
#define ITEM_NAN_DATA      	2
#define ITEM_NAN_BOOT       3
#define ITEM_NAN_RECOVERY   4
#define ITEM_NAN_CACHE		5
#define ITEM_NAN_SP1        6
#define ITEM_NAN_SP2        7
#define ITEM_NAN_SP3        8
#define ITEM_NAN_ANDSEC     9
#define ITEM_NAN_SDEXT      10
#define ITEM_NAN_COMPRESS   11
#define ITEM_NAN_BACK       12

char* nan_compress()
{
	char* tmp_set = (char*)malloc(40);
	strcpy(tmp_set, "[ ] Compress Backup (slow but saves space)");
	if (DataManager_GetIntValue(VAR_USE_COMPRESSION_VAR) == 1) {
		tmp_set[1] = 'x';
	}
	return tmp_set;
}

void
set_restore_files()
{
    const char* nan_dir = DataManager_GetStrValue("_restore");

    // Start with the default values
    int restore_system = -1;
    int restore_data = -1;
    int restore_cache = -1;
    int restore_recovery = -1;
    int restore_boot = -1;
    int restore_andsec = -1;
    int restore_sdext = -1;
    int restore_sp1 = -1;
    int restore_sp2 = -1;
    int restore_sp3 = -1;

    DIR* d;
    d = opendir(nan_dir);
    if (d == NULL)
    {
        LOGE("error opening %s\n", nan_dir);
        return;
    }

    struct dirent* de;
    while ((de = readdir(d)) != NULL)
    {
        // Strip off three components
        char str[256];
        char* label;
        char* fstype = NULL;
        char* extn = NULL;
        char* ptr;
        struct dInfo* dev = NULL;

        strcpy(str, de->d_name);
        label = str;
        ptr = label;
        while (*ptr && *ptr != '.')     ptr++;
        if (*ptr == '.')
        {
            *ptr = 0x00;
            ptr++;
            fstype = ptr;
        }
        while (*ptr && *ptr != '.')     ptr++;
        if (*ptr == '.')
        {
            *ptr = 0x00;
            ptr++;
            extn = ptr;
        }

        if (extn == NULL || strcmp(extn, "win") != 0)   continue;

        dev = findDeviceByLabel(label);
        if (dev == NULL)
        {
            LOGE(" Unable to locate device by label\n");
            continue;
        }

        strncpy(dev->fnm, de->d_name, 256);
        dev->fnm[255] = '\0';

        // Now, we just need to find the correct label
        if (dev == &sys)        restore_system = 1;
        if (dev == &dat)        restore_data = 1;
        if (dev == &boo)        restore_boot = 1;
        if (dev == &rec)        restore_recovery = 1;
        if (dev == &cac)        restore_cache = 1;
        if (dev == &sde)        restore_sdext = 1;
        if (dev == &sp1)        restore_sp1 = 1;
        if (dev == &sp2)        restore_sp2 = 1;
        if (dev == &sp3)        restore_sp3 = 1;
        if (dev == &ase)        restore_andsec = 1;
    }
    closedir(d);

    // Set the final values
    DataManager_SetIntValue(VAR_RESTORE_SYSTEM_VAR, restore_system);
    DataManager_SetIntValue(VAR_RESTORE_DATA_VAR, restore_data);
    DataManager_SetIntValue(VAR_RESTORE_CACHE_VAR, restore_cache);
    DataManager_SetIntValue(VAR_RESTORE_RECOVERY_VAR, restore_recovery);
    DataManager_SetIntValue(VAR_RESTORE_BOOT_VAR, restore_boot);
    DataManager_SetIntValue(VAR_RESTORE_ANDSEC_VAR, restore_andsec);
    DataManager_SetIntValue(VAR_RESTORE_SDEXT_VAR, restore_sdext);
    DataManager_SetIntValue(VAR_RESTORE_SP1_VAR, restore_sp1);
    DataManager_SetIntValue(VAR_RESTORE_SP2_VAR, restore_sp2);
    DataManager_SetIntValue(VAR_RESTORE_SP3_VAR, restore_sp3);

    return;
}

int phx_isMounted(struct dInfo mMnt)
{
    // Do not try to mount non-mountable partitions
    if (!mMnt.mountable)        return -1;

    struct stat st1, st2;
    char path[255];

    sprintf(path, "/%s/.", mMnt.mnt);
    if (stat(path, &st1) != 0)  return -1;

    sprintf(path, "/%s/../.", mMnt.mnt);
    if (stat(path, &st2) != 0)  return -1;

    int ret = (st1.st_dev != st2.st_dev) ? 1 : 0;

    return ret;
}

/* Made my own mount, because aosp api "ensure_path_mounted" relies on recovery.fstab
** and we needed one that can mount based on what the file system really is (from blkid), 
** and not what's in fstab or recovery.fstab
*/
int phx_mount(struct dInfo mMnt)
{
    char target[255];
    int ret = 0;

    // Do not try to mount non-mountable partitions
    if (!mMnt.mountable)        return 0;
    if (phx_isMounted(mMnt))     return 0;

    sprintf(target, "/%s", mMnt.mnt);
    if (mount(mMnt.blk, target, mMnt.fst, 0, NULL) != 0)
    {
        LOGE("Unable to mount %s\n", target);
        ret = 1;
    }
    return ret;
}

int phx_unmount(struct dInfo uMnt)
{
    char target[255];
    int ret = 0;

    // Do not try to mount non-mountable partitions
    if (!uMnt.mountable)        return 0;
    if (!phx_isMounted(uMnt))    return 0;

    sprintf(target, "/%s", uMnt.mnt);
    if (umount(target) != 0)
    {
        LOGE("Unable to unmount %s\n", target);
        ret = 1;
    }
	return ret;
}

/* New backup function
** Condensed all partitions into one function
*/
int phx_backup(struct dInfo bMnt, const char *bDir)
{
    // set compression or not
	char bTarArg[32];
	if (DataManager_GetIntValue(VAR_USE_COMPRESSION_VAR)) {
		strcpy(bTarArg,"-czv");
	} else {
		strcpy(bTarArg,"-cv");
	}
    FILE *bFp;

#ifdef RECOVERY_SDCARD_ON_DATA
    strcat(bTarArg, " -X /tmp/excludes.lst");

    bFp = fopen("/tmp/excludes.lst", "wt");
    if (bFp)
    {
        fprintf(bFp, "/data/media\n");
        fprintf(bFp, "./media\n");
        fprintf(bFp, "media\n");
        fclose(bFp);
    }
#endif

    char str[512];
	unsigned long long bPartSize;
	char *bImage = malloc(sizeof(char)*50);
	char *bMount = malloc(sizeof(char)*50);
	char *bCommand = malloc(sizeof(char)*255);

    if (bMnt.backup == files)
    {
        // detect mountable partitions
		if (strcmp(bMnt.mnt,".android_secure") == 0) { // if it's android secure, add sdcard to prefix
			strcpy(bMount,"/sdcard/");
			strcat(bMount,bMnt.mnt);
			sprintf(bImage,"and-sec.%s.win",bMnt.fst); // set image name based on filesystem, should always be vfat for android_secure
		} else {
			strcpy(bMount,"/");
			strcat(bMount,bMnt.mnt);
			sprintf(bImage,"%s.%s.win",bMnt.mnt,bMnt.fst); // anything else that is mountable, will be partition.filesystem.win
            SetDataState("Mounting", bMnt.mnt, 0, 0);
			if (phx_mount(bMnt))
            {
				ui_print("-- Could not mount: %s\n-- Aborting.\n",bMount);
				free(bCommand);
				free(bMount);
				free(bImage);
				return 1;
			}
		}
		bPartSize = bMnt.used;
		sprintf(bCommand,"cd %s && tar %s -f %s%s ./*",bMount,bTarArg,bDir,bImage); // form backup command
	} else if (bMnt.backup == image) {
		strcpy(bMount,bMnt.mnt);
		bPartSize = bMnt.sze;
		sprintf(bImage,"%s.%s.win",bMnt.mnt,bMnt.fst); // non-mountable partitions such as boot/sp1/recovery
		if (strcmp(bMnt.fst,"mtd") == 0) {
			sprintf(bCommand,"dump_image %s %s%s",bMnt.mnt,bDir,bImage); // if it's mtd, we use dump image
		} else {
			sprintf(bCommand,"dd bs=%s if=%s of=%s%s",bs_size,bMnt.blk,bDir,bImage); // if it's emmc, use dd
		}
		ui_print("\n");
	}
    else
    {
        LOGE("Unknown backup method for mount %s\n", bMnt.mnt);
        return 1;
    }

	LOGI("=> Filename: %s\n",bImage);
	LOGI("=> Size of %s is %lu KB.\n\n", bMount, (unsigned long) (bPartSize / 1024));
	int i;
	char bUppr[20];
	strcpy(bUppr,bMnt.mnt);
	for (i = 0; i < (int) strlen(bUppr); i++) { // make uppercase of mount name
		bUppr[i] = toupper(bUppr[i]);
	}
	ui_print("[%s (%lu MB)]\n", bUppr, (unsigned long) (bPartSize / (1024 * 1024))); // show size in MB

    SetDataState("Backup", bMnt.mnt, 0, 0);
	int bProgTime;
	time_t bStart, bStop;
	char bOutput[1024];

    time(&bStart); // start timer
    ui_print("...Backing up %s partition.\n",bMount);
    bFp = __popen(bCommand, "r"); // sending backup command formed earlier above
    if(DataManager_GetIntValue(VAR_SHOW_SPAM_VAR) == 2) { // if spam is on, show all lines
        while (fgets(bOutput,sizeof(bOutput),bFp) != NULL) {
            ui_print_overwrite("%s",bOutput);
        }
    } else { // else just show single line
        while (fscanf(bFp,"%s",bOutput) == 1) {
            if(DataManager_GetIntValue(VAR_SHOW_SPAM_VAR) == 1) ui_print_overwrite("%s",bOutput);
        }
    }
    ui_print_overwrite(" * Done.\n");
    __pclose(bFp);

    ui_print(" * Verifying backup size.\n");
    SetDataState("Verifying", bMnt.mnt, 0, 0);

    sprintf(bCommand, "%s%s", bDir, bImage);
    struct stat st;
    if (stat(bCommand, &st) != 0 || st.st_size == 0)
    {
        ui_print("E: File size is zero bytes. Aborting...\n\n"); // oh noes! file size is 0, abort! abort!
        phx_unmount(bMnt);
        free(bCommand);
        free(bMount);
        free(bImage);
        return 1;
    }

    ui_print(" * File size: %llu bytes.\n", st.st_size); // file size

    // Only verify image sizes
    if (bMnt.backup == image)
    {
        LOGI(" * Expected size: %llu Got: %lld\n", bMnt.sze, st.st_size);
        if (bMnt.sze != (unsigned long long) st.st_size)
        {
            ui_print("E: File size is incorrect. Aborting.\n\n"); // they dont :(
            free(bCommand);
            free(bMount);
            free(bImage);
            return 1;
        }
    }

    ui_print(" * Generating md5...\n");
    SetDataState("Generating MD5", bMnt.mnt, 0, 0);
    makeMD5(bDir, bImage); // make md5 file
    time(&bStop); // stop timer
    ui_print("[%s DONE (%d SECONDS)]\n\n",bUppr,(int)difftime(bStop,bStart)); // done, finally. How long did it take?
    phx_unmount(bMnt); // unmount partition we just restored to (if it's not a mountable partition, it will just bypass)

    free(bCommand);
	free(bMount);
	free(bImage);
	return 0;
}

unsigned long long get_backup_size(struct dInfo* mnt)
{
    if (!mnt)
    {
        LOGE("Invalid call to get_backup_size with NULL parameter\n");
        return 0;
    }

    switch (mnt->backup)
    {
    case files:
        return mnt->used;
    case image:
        return (unsigned long long) mnt->sze;
    case none:
        LOGW("Request backup details for partition '%s' which has no backup method.", mnt->mnt);
        break;

    default:
        LOGE("Backup type unknown for partition '%s'\n", mnt->mnt);
        break;
    }
    return 0;
}

int CalculateBackupDetails(int* total_partitions, unsigned long long* total_img_bytes, unsigned long long* total_file_bytes)
{
    *total_partitions = 0;

    if (DataManager_GetIntValue(VAR_BACKUP_SYSTEM_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (sys.backup == image)    *total_img_bytes += get_backup_size(&sys);
        else                        *total_file_bytes += get_backup_size(&sys);
    }

    if (DataManager_GetIntValue(VAR_BACKUP_DATA_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (dat.backup == image)    *total_img_bytes += get_backup_size(&dat);
        else                        *total_file_bytes += get_backup_size(&dat);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_CACHE_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (cac.backup == image)    *total_img_bytes += get_backup_size(&cac);
        else                        *total_file_bytes += get_backup_size(&cac);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_RECOVERY_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (rec.backup == image)    *total_img_bytes += get_backup_size(&rec);
        else                        *total_file_bytes += get_backup_size(&rec);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_SP1_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (sp1.backup == image)    *total_img_bytes += get_backup_size(&sp1);
        else                        *total_file_bytes += get_backup_size(&sp1);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_SP2_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (sp2.backup == image)    *total_img_bytes += get_backup_size(&sp2);
        else                        *total_file_bytes += get_backup_size(&sp2);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_SP3_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (sp3.backup == image)    *total_img_bytes += get_backup_size(&sp3);
        else                        *total_file_bytes += get_backup_size(&sp3);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_BOOT_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (boo.backup == image)    *total_img_bytes += get_backup_size(&boo);
        else                        *total_file_bytes += get_backup_size(&boo);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_ANDSEC_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (ase.backup == image)    *total_img_bytes += get_backup_size(&ase);
        else                        *total_file_bytes += get_backup_size(&ase);
    }
    if (DataManager_GetIntValue(VAR_BACKUP_SDEXT_VAR) == 1)
    {
        *total_partitions = *total_partitions + 1;
        if (sde.backup == image)    *total_img_bytes += get_backup_size(&sde);
        else                        *total_file_bytes += get_backup_size(&sde);
    }
    return 0;
}

int recursive_mkdir(const char* path)
{
    char pathCpy[512];
    char wholePath[512];
    strcpy(pathCpy, path);
    strcpy(wholePath, "/");

    char* tok = strtok(pathCpy, "/");
    while (tok)
    {
        strcat(wholePath, tok);
        if (mkdir(wholePath, 0777) && errno != EEXIST)
        {
            LOGE("Unable to create folder: %s  (errno=%d)\n", wholePath, errno);
            return -1;
        }
        strcat(wholePath, "/");

        tok = strtok(NULL, "/");
    }
    if (mkdir(wholePath, 0777) && errno != EEXIST)      return -1;
    return 0;
}

int phx_do_backup(const char* enableVar, struct dInfo* mnt, const char* image_dir, unsigned long long img_bytes, unsigned long long file_bytes, unsigned long long* img_bytes_remaining, unsigned long long* file_bytes_remaining, unsigned long* img_byte_time, unsigned long* file_byte_time)
{
    // Check if this partition is being backed up...
	if (!DataManager_GetIntValue(enableVar))    return 0;

    // Let's calculate the percentage of the bar this section is expected to take
    unsigned long int img_bps = DataManager_GetIntValue(VAR_BACKUP_AVG_IMG_RATE);
    unsigned long int file_bps;

    if (DataManager_GetIntValue(VAR_USE_COMPRESSION_VAR))    file_bps = DataManager_GetIntValue(VAR_BACKUP_AVG_FILE_COMP_RATE);
    else                                                    file_bps = DataManager_GetIntValue(VAR_BACKUP_AVG_FILE_RATE);

    if (DataManager_GetIntValue(VAR_SKIP_MD5_GENERATE_VAR) == 1)
    {
        // If we're skipping MD5 generation, our BPS is faster by about 1.65
        file_bps = (unsigned long) (file_bps * 1.65);
        img_bps = (unsigned long) (img_bps * 1.65);
    }

    // We know the speed for both, how far into the whole backup are we, based on time
    unsigned long total_time = (img_bytes / img_bps) + (file_bytes / file_bps);
    unsigned long remain_time = (*img_bytes_remaining / img_bps) + (*file_bytes_remaining / file_bps);

    float pos = (total_time - remain_time) / (float) total_time;
    ui_set_progress(pos);

    LOGI("Estimated Total time: %lu  Estimated remaining time: %lu\n", total_time, remain_time);

    // Determine how much we're about to process
    unsigned long long blocksize = get_backup_size(mnt);

    // And get the time
    unsigned long section_time;
    if (mnt->backup == image)       section_time = blocksize / img_bps;
    else                            section_time = blocksize / file_bps;

    // Set the position
    pos = section_time / (float) total_time;
    ui_show_progress(pos, section_time);

    time_t start, stop;
    time(&start);

    if (phx_backup(*mnt, image_dir) == 1) { // did the backup process return an error ? 0 = no error
        SetDataState("Backup failed", mnt->mnt, 1, 1);
        ui_print("-- Error occured, check recovery.log. Aborting.\n"); //oh noes! abort abort!
        return 1;
    }
    time(&stop);

    LOGI("Partition Backup time: %d\n", (int) difftime(stop, start));

    // Now, decrement out byte counts
    if (mnt->backup == image)
    {
        *img_bytes_remaining -= blocksize;
        *img_byte_time += (int) difftime(stop, start);
    }
    else
    {
        *file_bytes_remaining -= blocksize;
        *file_byte_time += (int) difftime(stop, start);
    }

    return 0;
}

int
nandroid_back_exe()
{
    SetDataState("Starting", "backup", 0, 0);

    if (ensure_path_mounted(SDCARD_ROOT) != 0) {
		ui_print("-- Could not mount: %s.\n-- Aborting.\n",SDCARD_ROOT);
        SetDataState("Unable to mount", "/sdcard", 1, 1);
		return 1;
	}

    // Create backup folder
    struct tm *t;
    char timestamp[64];
	char image_dir[255];
	char exe[255];
	time_t start, stop;
	time_t seconds;
	seconds = time(0);
    t = localtime(&seconds);
    sprintf(timestamp,"%04d-%02d-%02d--%02d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec); // make time stamp
	sprintf(image_dir,"%s/%s/%s/", backup_folder, device_id, timestamp); // for backup folder

    if (recursive_mkdir(image_dir))
    {
        LOGE("Unable to create folder: '%s'\n", backup_folder);
        SetDataState("Backup failed", image_dir, 1, 1);
        return -1;
    }

    // Record the start time
    time(&start);

    // Prepare operation
    ui_print("\n[BACKUP STARTED]\n");
    ui_print(" * Backup Folder: %s\n", backup_folder);
    ui_print(" * Verifying filesystems...\n");
    verifyFst();
    createFstab();
    ui_print(" * Verifying partition sizes...\n");
    updateUsedSized();
    unsigned long long sdc_free = sdcext.sze - sdcext.used; 

    // Compute totals
    int total = 0;
    unsigned long long total_img_bytes = 0, total_file_bytes = 0;
    CalculateBackupDetails(&total, &total_img_bytes, &total_file_bytes);
    unsigned long long total_bytes = total_img_bytes + total_file_bytes;

    if (total == 0 || total_bytes == 0)
    {
        LOGE("Unable to compute target usage (%d partitions, %llu bytes)\n", total, total_bytes);
        SetDataState("Backup failed", image_dir, 1, 1);
        return -1;
    }

    ui_print(" * Total number of partition to back up: %d\n", total);
    ui_print(" * Total size of all data, in KB: %llu\n", total_bytes / 1024);
    ui_print(" * Available space on the SD card, in KB: %llu\n", sdc_free / 1024);

    // We can't verify sufficient space on devices where sdcard is a portion of the data partition
#ifndef RECOVERY_SDCARD_ON_DATA
    // Verify space
    if (sdc_free < (total_bytes + 0x2000000))       // We require at least 32MB of additional space
    {
        LOGE("Insufficient space on SDCARD. Required space is %lluKB, available %lluKB\n", (total_bytes + 0x2000000) / 1024, sdc_free / 1024);
        SetDataState("Backup failed", image_dir, 1, 1);
        return -1;
    }
#else
    ui_print(" * This device does not support verifying available free space.\n");
#endif

    // Prepare progress bar...
    unsigned long long img_bytes_remaining = total_img_bytes;
    unsigned long long file_bytes_remaining = total_file_bytes;
    unsigned long img_byte_time = 0, file_byte_time = 0;
	struct stat st;

    ui_set_progress(0.0);

	// SYSTEM
    if (phx_do_backup(VAR_BACKUP_SYSTEM_VAR, &sys, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))       return 1;

	// DATA
    if (phx_do_backup(VAR_BACKUP_DATA_VAR, &dat, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))         return 1;

    // BOOT
    if (phx_do_backup(VAR_BACKUP_BOOT_VAR, &boo, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))         return 1;

    // RECOVERY
    if (phx_do_backup(VAR_BACKUP_RECOVERY_VAR, &rec, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))     return 1;

    // CACHE
    if (phx_do_backup(VAR_BACKUP_CACHE_VAR, &cac, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))        return 1;

    // SP1
    if (phx_do_backup(VAR_BACKUP_SP1_VAR, &sp1, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))          return 1;

    // SP2
    if (phx_do_backup(VAR_BACKUP_SP2_VAR, &sp2, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))          return 1;

    // SP3
    if (phx_do_backup(VAR_BACKUP_SP3_VAR, &sp3, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))          return 1;

    // ANDROID-SECURE
	if (stat(ase.dev, &st) ==0)
		if (phx_do_backup(VAR_BACKUP_ANDSEC_VAR, &ase, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))       return 1;

    // SD-EXT
	if (stat(sde.dev, &st) ==0)
		if (phx_do_backup(VAR_BACKUP_SDEXT_VAR, &sde, image_dir, total_img_bytes, total_file_bytes, &img_bytes_remaining, &file_bytes_remaining, &img_byte_time, &file_byte_time))        return 1;

    ui_print(" * Verifying filesystems...\n");
    verifyFst();
    createFstab();
    ui_print(" * Verifying partition sizes...\n");
    updateUsedSized();

    time(&stop);

    // Average BPS
    unsigned long int img_bps = total_img_bytes / img_byte_time;
    unsigned long int file_bps = total_file_bytes / file_byte_time;

    LOGI("img_bps = %lu  total_img_bytes = %llu  img_byte_time = %lu\n", img_bps, total_img_bytes, img_byte_time);
    ui_print("Average backup rate for file systems: %lu MB/sec\n", (file_bps / (1024 * 1024)));
    ui_print("Average backup rate for imaged drives: %lu MB/sec\n", (img_bps / (1024 * 1024)));

    if (DataManager_GetIntValue(VAR_SKIP_MD5_GENERATE_VAR) == 1)
    {
        // If we're skipping MD5 generation, our BPS is faster by about 1.65
        file_bps = (unsigned long) (file_bps / 1.65);
        img_bps = (unsigned long) (img_bps / 1.65);
    }

    img_bps += (DataManager_GetIntValue(VAR_BACKUP_AVG_IMG_RATE) * 4);
    img_bps /= 5;

    if (DataManager_GetIntValue(VAR_USE_COMPRESSION_VAR))    file_bps += (DataManager_GetIntValue(VAR_BACKUP_AVG_FILE_COMP_RATE) * 4);
    else                                                    file_bps += (DataManager_GetIntValue(VAR_BACKUP_AVG_FILE_RATE) * 4);
    file_bps /= 5;

    DataManager_SetIntValue(VAR_BACKUP_AVG_IMG_RATE, img_bps);
    if (DataManager_GetIntValue(VAR_USE_COMPRESSION_VAR))    DataManager_SetIntValue(VAR_BACKUP_AVG_FILE_COMP_RATE, file_bps);
    else                                                    DataManager_SetIntValue(VAR_BACKUP_AVG_FILE_RATE, file_bps);

    int total_time = (int) difftime(stop, start);
    unsigned long long new_sdc_free = (sdcext.sze - sdcext.used) / (1024 * 1024);
    sdc_free /= (1024 * 1024);

	ui_print("[%lu MB TOTAL BACKED UP TO SDCARD]\n",(unsigned long) (sdc_free - new_sdc_free));
	ui_print("[BACKUP COMPLETED IN %d SECONDS]\n\n", total_time); // the end
    SetDataState("Backup Succeeded", "", 0, 1);
	return 0;
}

int phx_restore(struct dInfo rMnt, const char *rDir)
{
	int i;
	FILE *reFp;
	char rUppr[20];
	char rMount[30];
	char rFilesystem[10];
	char rFilename[255];
	char rCommand[255];
	char rOutput[255];
	time_t rStart, rStop;
	strcpy(rUppr,rMnt.mnt);
	for (i = 0; i < (int) strlen(rUppr); i++) {
		rUppr[i] = toupper(rUppr[i]);
	}
	ui_print("[%s]\n",rUppr);
	time(&rStart);
    int md5_result;
	if (DataManager_GetIntValue(VAR_SKIP_MD5_CHECK_VAR)) {
		SetDataState("Verifying MD5", rMnt.mnt, 0, 0);
		ui_print("...Verifying md5 hash for %s.\n",rMnt.mnt);
		md5_result = checkMD5(rDir, rMnt.fnm); // verify md5, check if no error; 0 = no error.
	} else {
		md5_result = 0;
		ui_print("Skipping MD5 check based on user setting.\n");
	}
	if(md5_result == 0)
	{
		strcpy(rFilename,rDir);
        if (rFilename[strlen(rFilename)-1] != '/')
        {
            strcat(rFilename, "/");
        }
		strcat(rFilename,rMnt.fnm);
		sprintf(rCommand,"ls -l %s | awk -F'.' '{ print $2 }'",rFilename); // let's get the filesystem type from filename
        reFp = __popen(rCommand, "r");
		LOGI("=> Filename is: %s\n",rMnt.fnm);
		while (fscanf(reFp,"%s",rFilesystem) == 1) { // if we get a match, store filesystem type
			LOGI("=> Filesystem is: %s\n",rFilesystem); // show it off to the world!
		}
		__pclose(reFp);

		if ((DataManager_GetIntValue(VAR_RM_RF_VAR) == 1 && (strcmp(rMnt.mnt,"system") == 0 || strcmp(rMnt.mnt,"data") == 0 || strcmp(rMnt.mnt,"cache") == 0)) || strcmp(rMnt.mnt,".android_secure") == 0) { // we'll use rm -rf instead of formatting for system, data and cache if the option is set, always use rm -rf for android secure
			char rCommand2[255];
			ui_print("...using rm -rf to wipe %s\n", rMnt.mnt);
			if (strcmp(rMnt.mnt,".android_secure") == 0) {
				phx_mount(sdcext); // for android secure we must make sure that the sdcard is mounted
				sprintf(rCommand, "rm -rf %s/*", rMnt.dev);
				sprintf(rCommand2, "rm -rf %s/.*", rMnt.dev);
			} else {
				phx_mount(rMnt); // mount the partition first
				sprintf(rCommand,"rm -rf %s%s/*", "/", rMnt.mnt);
				sprintf(rCommand2,"rm -rf %s%s/.*", "/", rMnt.mnt);
			}
            SetDataState("Wiping", rMnt.mnt, 0, 0);
			__system(rCommand);
			__system(rCommand2);
			ui_print("....done wiping.\n");
		} else {
			ui_print("...Formatting %s\n",rMnt.mnt);
            SetDataState("Formatting", rMnt.mnt, 0, 0);
			if (strcmp(rMnt.fst, "yaffs2") == 0) {
				if (strcmp(rMnt.mnt, "data") == 0) {
					phx_format(rFilesystem,"userdata"); // on MTD yaffs2, data is actually found under userdata
				} else {
					phx_format(rFilesystem,rMnt.mnt); // use mount location instead of block for formatting on mtd devices
				}
            } else {
			    phx_format(rFilesystem,rMnt.blk); // let's format block, based on filesystem from filename above
            }
			ui_print("....done formatting.\n");
		}

        if (rMnt.backup == files)
        {
            phx_mount(rMnt);
            strcpy(rMount,"/");
            if (strcmp(rMnt.mnt,".android_secure") == 0) { // if it's android_secure, we have add prefix
                strcat(rMount,"sdcard/");
            }
            strcat(rMount,rMnt.mnt);
            sprintf(rCommand,"cd %s && tar -xvf %s",rMount,rFilename); // formulate shell command to restore
        } else if (rMnt.backup == image) {
            if (strcmp(rFilesystem,"mtd") == 0) { // if filesystem is mtd, we use flash image
    			sprintf(rCommand,"flash_image %s %s",rMnt.mnt,rFilename);
    			strcpy(rMount,rMnt.mnt);
    		} else { // if filesystem is emmc, we use dd
    			sprintf(rCommand,"dd bs=%s if=%s of=%s",bs_size,rFilename,rMnt.dev);
    			strcpy(rMount,rMnt.mnt);
    		}
        } else {
            LOGE("Unknown backup method for mount %s\n", rMnt.mnt);
            return 1;
        }

		ui_print("...Restoring %s\n\n",rMount);
        SetDataState("Restoring", rMnt.mnt, 0, 0);
		reFp = __popen(rCommand, "r");
		if(DataManager_GetIntValue(VAR_SHOW_SPAM_VAR) == 2) { // spam
			while (fgets(rOutput,sizeof(rOutput),reFp) != NULL) {
				ui_print_overwrite("%s",rOutput);
			}
		} else {
			while (fscanf(reFp,"%s",rOutput) == 1) {
				if(DataManager_GetIntValue(VAR_SHOW_SPAM_VAR) == 1) ui_print_overwrite("%s",rOutput);
			}
		}
		__pclose(reFp);
		ui_print_overwrite("....done restoring.\n");
		if (strcmp(rMnt.mnt,".android_secure") != 0) { // any partition other than android secure,
			phx_unmount(rMnt); // let's unmount (unmountable partitions won't matter)
		}
	} else {
		ui_print("...Failed md5 check. Aborted.\n\n");
		return 1;
	}
	time(&rStop);
	ui_print("[%s DONE (%d SECONDS)]\n\n",rUppr,(int)difftime(rStop,rStart));
	return 0;
}

int 
nandroid_rest_exe()
{
    SetDataState("", "", 0, 0);

    const char* nan_dir = DataManager_GetStrValue("phx_restore");

	if (ensure_path_mounted(SDCARD_ROOT) != 0) {
		ui_print("-- Could not mount: %s.\n-- Aborting.\n",SDCARD_ROOT);
		return 1;
	}

    int total = 0;
    total += (DataManager_GetIntValue(VAR_RESTORE_SYSTEM_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_DATA_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_CACHE_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_RECOVERY_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_SP1_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_SP2_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_SP3_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_BOOT_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_ANDSEC_VAR) == 1 ? 1 : 0);
    total += (DataManager_GetIntValue(VAR_RESTORE_SDEXT_VAR) == 1 ? 1 : 0);

    float sections = 1.0 / total;

	time_t rStart, rStop;
	time(&rStart);
	ui_print("\n[RESTORE STARTED]\n\n");
	if (DataManager_GetIntValue(VAR_RESTORE_SYSTEM_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(sys,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
    if (DataManager_GetIntValue(VAR_RESTORE_DATA_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(dat,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
    if (DataManager_GetIntValue(VAR_RESTORE_BOOT_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(boo,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
    if (DataManager_GetIntValue(VAR_RESTORE_RECOVERY_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(rec,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
    if (DataManager_GetIntValue(VAR_RESTORE_CACHE_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(cac,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
    if (DataManager_GetIntValue(VAR_RESTORE_SP1_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(sp1,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
    if (DataManager_GetIntValue(VAR_RESTORE_SP2_VAR) == 1) {
        ui_show_progress(sections, 150);
        if (phx_restore(sp2,nan_dir) == 1) {
            ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
            return 1;
        }
    }
    if (DataManager_GetIntValue(VAR_RESTORE_SP3_VAR) == 1) {
        ui_show_progress(sections, 150);
        if (phx_restore(sp3,nan_dir) == 1) {
            ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
            return 1;
        }
    }
    if (DataManager_GetIntValue(VAR_RESTORE_ANDSEC_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(ase,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
    if (DataManager_GetIntValue(VAR_RESTORE_SDEXT_VAR) == 1) {
        ui_show_progress(sections, 150);
		if (phx_restore(sde,nan_dir) == 1) {
			ui_print("-- Error occured, check recovery.log. Aborting.\n");
            SetDataState("Restore failed", "", 1, 1);
			return 1;
		}
	}
	time(&rStop);
	ui_print("[RESTORE COMPLETED IN %d SECONDS]\n\n",(int)difftime(rStop,rStart));
	__system("sync");
	LOGI("=> Let's update filesystem types.\n");
	verifyFst();
	LOGI("=> And update our fstab also.\n");
	createFstab();
    SetDataState("Restore Succeeded", "", 0, 1);
	return 0;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

int makeMD5(const char *imgDir, const char *imgFile)
{
	int bool = 1;

    if (DataManager_GetIntValue(VAR_SKIP_MD5_GENERATE_VAR) == 1) {
        // When skipping the generate, we return success
        return 0;
    }

	if (ensure_path_mounted("/sdcard") != 0) {
		LOGI("=> Can not mount sdcard.\n");
		return bool;
	} else {
		FILE *fp;
		char tmpString[255];
		char tmpAnswer[15];
		sprintf(tmpString,"cd %s && md5sum %s > %s.md5",imgDir,imgFile,imgFile);
		fp = __popen(tmpString, "r");
		fgets(tmpString, 255, fp);
		getWordFromString(1, tmpString, tmpAnswer, sizeof(tmpAnswer));
		if (strcmp(tmpAnswer,"md5sum:") == 0) {
            ui_print("....MD5 Error: %s (%s)", tmpString, tmpAnswer);
		} else {
			ui_print("....MD5 Created.\n");
			bool = 0;
		}
		__pclose(fp);
	}
	return bool;
}

int checkMD5(const char *imgDir, const char *imgFile)
{
	int bool = 1;

    if (DataManager_GetIntValue(VAR_SKIP_MD5_CHECK_VAR) == 1) {
        // When skipping the test, we return success
        return 0;
    }

	if (ensure_path_mounted("/sdcard") != 0) {
		LOGI("=> Can not mount sdcard.\n");
		return bool;
	} else {
		FILE *fp;
		char tmpString[255];
		char tmpAnswer[15];
		sprintf(tmpString,"cd %s && md5sum -c %s.md5",imgDir,imgFile);
		fp = __popen(tmpString, "r");
		fgets(tmpString, 255, fp);
		getWordFromString(2, tmpString, tmpAnswer, sizeof(tmpAnswer));
		if (strcmp(tmpAnswer,"OK") == 0) {
			ui_print("....MD5 Check: %s\n", tmpAnswer);
			bool = 0;
		} else {
			ui_print("....MD5 Error: %s (%s)", tmpString, tmpAnswer);
		}
		__pclose(fp);
	}
	return bool;
}
