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
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "ddftw.h"
#include "common.h"
#include "extra-functions.h"
#include "bootloader.h"
#include "backstore.h"
#include "data.h"

struct dInfo tmp, sys, dat, boo, rec, cac, sdcext, sdcint, ase, sde, sp1, sp2, sp3;
char device_name[20];

void dumpPartitionTable(void);

static int isMTDdevice = 0, isEMMCdevice = 0;

struct dInfo* findDeviceByLabel(const char* label)
{
    if (!label)                             return NULL;
    if (strcmp(label, "system") == 0)       return &sys;
    if (strcmp(label, "userdata") == 0)     return &dat;
    if (strcmp(label, "data") == 0)         return &dat;
    if (strcmp(label, "boot") == 0)         return &boo;
    if (strcmp(label, "recovery") == 0)     return &rec;
    if (strcmp(label, "cache") == 0)        return &cac;
    if (strcmp(label, "sd-ext") == 0)       return &sde;
	if (strcmp(label, "and-sec") == 0)      return &ase;

    // New sdcard methods
    if (strcmp(label, "sdcard") == 0)       return &sdcext;
    if (strcmp(label, "sdc-ext") == 0)      return &sdcext;
    if (strcmp(label, "sdc-int") == 0)      return &sdcint;

    // Special Partitions (such as WiMAX, efs, and PDS)
    if (strcmp(label, EXPAND(SP1_NAME)) == 0)       return &sp1;
    if (strcmp(label, EXPAND(SP2_NAME)) == 0)       return &sp2;
    if (strcmp(label, EXPAND(SP3_NAME)) == 0)       return &sp3;

    return NULL;
}

struct dInfo* findDeviceByBlockDevice(const char* blockDevice)
{
    if (!blockDevice)                       return NULL;
    if (strcmp(blockDevice, sys.blk) == 0)  return &sys;
    if (strcmp(blockDevice, dat.blk) == 0)  return &dat;
    if (strcmp(blockDevice, boo.blk) == 0)  return &boo;
    if (strcmp(blockDevice, rec.blk) == 0)  return &rec;
    if (strcmp(blockDevice, cac.blk) == 0)  return &cac;
    if (strcmp(blockDevice, sde.blk) == 0)  return &sde;
    if (strcmp(blockDevice, sdcext.blk) == 0)  return &sdcext;
    if (strcmp(blockDevice, sdcint.blk) == 0)  return &sdcint;
    if (strcmp(blockDevice, sp1.blk) == 0)  return &sp1;
    if (strcmp(blockDevice, sp2.blk) == 0)  return &sp2;
    if (strcmp(blockDevice, sp3.blk) == 0)  return &sp3;
    return NULL;
}

#define SAFE_STR(str)       (str ? str : "<NULL>")

// This routine handles the case where we can't open either /proc/mtd or /proc/emmc
int setLocationData(const char* label, const char* blockDevice, const char* mtdDevice, const char* fstype, unsigned long long size)
{
    struct dInfo* loc = NULL;

    if (label)                  loc = findDeviceByLabel(label);
    if (!loc && blockDevice)    loc = findDeviceByBlockDevice(blockDevice);

    if (!loc)
        return -1;

    if (label)                  strcpy(loc->mnt, label);
    if (blockDevice)            strcpy(loc->blk, blockDevice);
    if (mtdDevice){
		strcpy(loc->dev, mtdDevice);
		loc->memory_type = mtd;
		if (strcmp(loc->mnt, "data") == 0)
			strcpy(loc->format_location, "userdata");
		else
			strcpy(loc->format_location, loc->mnt);
	} else {
		loc->memory_type = emmc;
		strcpy(loc->format_location, loc->blk);
	}

    // This is a simple 
    if (strcmp(loc->mnt, "boot") == 0 && fstype)
    {
        loc->mountable = 0;
        if (strcmp(fstype, "vfat") == 0 || memcmp(fstype, "ext", 3) == 0)
            loc->mountable = 1;
    }

    if (fstype)                 strcpy(loc->fst, fstype);
    if (size && loc->sze == 0)  loc->sze = size;

    return 0;
}

int getSizesViaPartitions()
{
    FILE* fp;
    char line[512];
    int ret = 0;

    // In this case, we'll first get the partitions we care about (with labels)
    fp = fopen("/proc/partitions", "rt");
    if (fp == NULL)
        return -1;

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        unsigned long major, minor, blocks;
        char device[64];
        char tmpString[64];

        if (strlen(line) < 7 || line[0] == 'm')     continue;
        sscanf(line + 1, "%lu %lu %lu %s", &major, &minor, &blocks, device);
		
        // Adjust block size to byte size
        unsigned long long size = blocks * 1024LL;
        sprintf(tmpString, "%s%s", _block, device);
        setLocationData(NULL, tmpString, NULL, NULL, size);
    }
    fclose(fp);
    return 0;
}

int getLocationsViafstab()
{
    FILE* fp;
    char line[512];
    int ret = 0;

    // In this case, we'll first get the partitions we care about (with labels)
    fp = fopen("/etc/recovery.fstab", "rt");
    if (fp == NULL)
    {
        LOGE("=> Unable to open /etc/recovery.fstab\n");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char mount[32], fstype[32], device[256];
        char* pDevice = device;

        if (line[0] != '/')     continue;
        sscanf(line + 1, "%s %s %s", mount, fstype, device);

        // Attempt to flip mounts until we find the block device
        char realDevice[256];
        memset(realDevice, 0, sizeof(realDevice));
        while (readlink(device, realDevice, sizeof(realDevice)) > 0)
        {
            strcpy(device, realDevice);
            memset(realDevice, 0, sizeof(realDevice));
        }

        if (device[0] != '/')   pDevice = NULL;
        setLocationData(mount, pDevice, pDevice, fstype, 0);
    }
    fclose(fp);

    // We can ignore the results of this call. But if it works, it at least helps get details
    getSizesViaPartitions();

    // Now, let's retrieve base partition sizes
    if (isEMMCdevice)
    {
        fp = __popen("fdisk -l /dev/block/mmcblk0","r");
        if (fp == NULL)
        {
            LOGE("=> Unable to retrieve partition sizes via fdisk\n");
            return -1;
        }
    
        while (fgets(line, sizeof(line), fp) != NULL)
        {
            char isBoot[64], device[64], blocks[2][16], *pSizeBlock;
            unsigned long long size = 0;
    
            if (line[0] != '/')     continue;
            sscanf(line, "%s %s %*s %s %s", device, isBoot, blocks[0], blocks[1]);
    

            if (isBoot[0] == '*')   pSizeBlock = blocks[1];
            else                    pSizeBlock = blocks[0];

            // If the block size isn't accurate, don't record it.
            if (pSizeBlock[strlen(pSizeBlock)-1] == '+')    pSizeBlock = NULL;

            // This could be NULL if we decided the size wasn't accurate
            if (pSizeBlock)
            {
                size = ((unsigned long long) atol(pSizeBlock)) * 1024;
            }

            if (size && (setLocationData(NULL, device, NULL, NULL, size) == 0))
            {
//                LOGI("  Mount %s size: %d\n", device, size);
            }
        }
        fclose(fp);
    }

    return ret;
}

// get locations from our device.info
int getLocationsViaProc(const char* fstype)
{
	FILE *fp = NULL;
    char line[255];

    if (fstype == NULL)
    {
        LOGE("Invalid argument to getLocationsViaProc\n");
        return -1;
    }

    sprintf(line, "/proc/%s", fstype);
    fp = fopen(line, "rt");
    if (fp == NULL)
    {
        LOGW("Device does not support %s\n", line);
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char device[32], label[32];
        unsigned long size = 0;
        char mtdDevice[32];
        char* fstype = NULL;
        int deviceId;

        sscanf(line, "%s %lx %*s %*c%s", device, &size, label);

        // Skip header and blank lines
        if ((strcmp(device, "dev:") == 0) || (strlen(line) < 8))
            continue;

        // Strip off the : and " from the details
        device[strlen(device)-1] = '\0';
        label[strlen(label)-1] = '\0';

        if (sscanf(device,"mtd%d", &deviceId) == 1)
        {
            isMTDdevice = 1;
            sprintf(mtdDevice, "%s%s", _mtd, device);
            sprintf(device, "%smtdblock%d", _block, deviceId);
            strcpy(tmp.fst,"mtd");
            fstype = "mtd";
        }
        else
        {
            isEMMCdevice = 1;
			strcpy(mtdDevice, device);
            fstype = "emmc";
        }

        if (strcmp(label, "efs") == 0)
            fstype = "yaffs2";

        setLocationData(label, device, mtdDevice, fstype, (unsigned long long) size);
    }

    fclose(fp);

    // We still proceed on to use the fstab to query devices as well
    return getLocationsViafstab();
}

unsigned long long getUsedSizeViaDu(const char* path)
{
    char cmd[512];
    sprintf(cmd, "du %s | awk '{ print $1 }'", path);

    FILE *fp;
    fp = __popen(cmd, "r");
    
    char str[512];
    fgets(str, sizeof(str), fp);
    __pclose(fp);

    unsigned long long size = atol(str);
    size *= 1024;

    return size;
}

void updateMntUsedSize(struct dInfo* mMnt)
{
#ifdef RECOVERY_SDCARD_ON_DATA
    if (mMnt == &sdcext)
    {
        struct statfs st;
        if (statfs("/mnt/data-sdc/.", &st) != 0)    return;

        mMnt->used = ((st.f_blocks - st.f_bfree) * st.f_bsize);
        return;
    }
#endif

	if (strcmp(mMnt->mnt, ".android_secure") == 0)
    {
		// android_secure is a little different - we mount sdcard and use du to figure out how much space is being taken up by android_secure
        
        // We can ignore any error from this, it doesn't matter
        phx_mount(sdcext);

        mMnt->used = getUsedSizeViaDu("/sdcard/.android_secure");
        mMnt->sze = mMnt->used;
        return;
	}

    char path[512];
    struct statfs st;
    int mounted;

    if (!mMnt->mountable)
    {
        // Since this partition isn't mountable, we're going to mark it's used size as it's standard size
        mMnt->used = mMnt->sze;
        return;
    }

    mounted = phx_isMounted(*mMnt);
    if (!mounted)
    {
        // If we fail, just move on
        if (phx_mount(*mMnt))
            return;
    }
    
    sprintf(path, "/%s/.", mMnt->mnt);
    if (statfs(path, &st) != 0)
    {
        LOGE("Unable to stat '%s'\n", path);
        return;
    }

    mMnt->used = ((st.f_blocks - st.f_bfree) * st.f_bsize);

    if (!mounted)   phx_unmount(*mMnt);
    return;
}

void updateUsedSized()
{
    updateMntUsedSize(&boo);
    updateMntUsedSize(&sys);
    updateMntUsedSize(&dat);
    updateMntUsedSize(&cac);
    updateMntUsedSize(&rec);
    updateMntUsedSize(&sdcext);
    updateMntUsedSize(&sdcint);
    updateMntUsedSize(&sde);
	updateMntUsedSize(&ase);
    updateMntUsedSize(&sp1);
    updateMntUsedSize(&sp2);
    updateMntUsedSize(&sp3);

    dumpPartitionTable();
    return;
}

void listMntInfo(struct dInfo* mMnt, char* variable_name)
{
	LOGI("%s information:\n   mnt: '%s'\n   blk: '%s'\n   dev: '%s'\n   fst: '%s'\n   fnm: '%s'\n   format location: '%s'\n   mountable: %i\n   backup method: %i\n   memory type: %i\n\n", variable_name, mMnt->mnt, mMnt->blk, mMnt->dev, mMnt->fst, mMnt->fnm, mMnt->format_location, mMnt->mountable, mMnt->backup, mMnt->memory_type);
};
	

int getLocations()
{
    // This decides if a partition can be mounted and appears in the fstab
    sys.mountable = 1;
    dat.mountable = 1;
    cac.mountable = 1;
    sde.mountable = 1;
//    boo.mountable = 1;        // Boot it detected earlier
    rec.mountable = 0;
    sdcext.mountable = 1;
    sdcint.mountable = 1;
    ase.mountable = 0;
    sp1.mountable = SP1_MOUNTABLE;
    sp2.mountable = SP2_MOUNTABLE;
    sp3.mountable = SP3_MOUNTABLE;

    // This decides how we backup/restore a block
    sys.backup = files;
    dat.backup = files;
    cac.backup = files;
    sde.backup = files;
    boo.backup = image;
    rec.backup = image;
    sdcext.backup = none;
    sdcint.backup = none;
    ase.backup = files;
    sp1.backup = SP1_BACKUP_METHOD;
    sp2.backup = SP2_BACKUP_METHOD;
    sp3.backup = SP3_BACKUP_METHOD;

    if (getLocationsViaProc("emmc") != 0 && getLocationsViaProc("mtd") != 0 && getLocationsViafstab() != 0)
    {
        LOGE("E: Unable to get device locations.\n");
        return -1;
    }

    // Handle adding .android_secure and sd-ext
    strcpy(ase.dev, "/sdcard/.android_secure"); // android secure stuff
    strcpy(ase.blk, ase.dev);
    strcpy(ase.mnt, ".android_secure");
    strcpy(ase.fst, "vfat");

    if (strlen(sdcext.blk) > 0)
    {
        int tmpInt;
        char tmpBase[50];
        char tmpWildCard[50];
    
        // We make the base via sdcard block
        strcpy(sde.mnt, "sd-ext");
        strcpy(tmpBase, sdcext.blk);
        tmpBase[strlen(tmpBase)-1] = '\0';
        sprintf(tmpWildCard,"%s%%d",tmpBase);
        sscanf(sdcext.blk, tmpWildCard, &tmpInt); // sdcard block used as sd-ext base
        sprintf(sde.blk, "%s%d",tmpBase, tmpInt+1);
    }

    get_device_id();

	LOGI("=> Let's update filesystem types.\n");
	verifyFst(); // use blkid to REALLY REALLY determine partition filesystem type
	LOGI("=> And update our fstab also.\n");
	createFstab(); // used for our busybox mount command
    LOGI("=> Update the usage statistics.\n\n");
    updateUsedSized();  // Retrieves the used space of all partitions

    // Now, let's update the data manager...
    DataManager_SetIntValue("_boot_is_mountable", boo.mountable ? 1 : 0);
    DataManager_SetIntValue("_system_is_mountable", sys.mountable ? 1 : 0);
    DataManager_SetIntValue("_data_is_mountable", dat.mountable ? 1 : 0);
    DataManager_SetIntValue("_cache_is_mountable", cac.mountable ? 1 : 0);
    DataManager_SetIntValue("_sdcext_is_mountable", sdcext.mountable ? 1 : 0);
    DataManager_SetIntValue("_sdcint_is_mountable", sdcint.mountable ? 1 : 0);
    DataManager_SetIntValue("_sd-ext_is_mountable", sde.mountable ? 1 : 0);
    DataManager_SetIntValue("_sp1_is_mountable", sp1.mountable ? 1 : 0);
    DataManager_SetIntValue("_sp2_is_mountable", sp2.mountable ? 1 : 0);
    DataManager_SetIntValue("_sp3_is_mountable", sp3.mountable ? 1 : 0);

    listMntInfo(&boo, "boot");
    listMntInfo(&sys, "system");
    listMntInfo(&dat, "data");
    listMntInfo(&cac, "cache");
    listMntInfo(&rec, "recovery");
    listMntInfo(&sdcext, "sdcext");
    listMntInfo(&sdcint, "sdcint");
    listMntInfo(&sde, "sd-ext");
	listMntInfo(&ase, "android_secure");
    listMntInfo(&sp1, "special 1");
    listMntInfo(&sp2, "special 2");
    listMntInfo(&sp3, "special 3");
    return 0;
}

static void createFstabEntry(FILE* fp, struct dInfo* mnt)
{
    char tmpString[255];
    struct stat st;

    if (!*(mnt->blk))
    {
        mnt->mountable = 0;
        return;
    }

    sprintf(tmpString,"%s /%s %s rw\n", mnt->blk, mnt->mnt, mnt->fst);
    fputs(tmpString, fp);
    sprintf(tmpString, "/%s", mnt->mnt);

    // We only create the folder if the block device exists
    if (stat(mnt->blk, &st) == 0 && stat(tmpString, &st) != 0)
    {
        if (mkdir(tmpString, 0777) == -1)
            LOGI("=> Can not create %s folder.\n", tmpString);
        else
            LOGI("=> Created %s folder.\n", tmpString);
    }
    return;
}

// write fstab so we can mount in adb shell
void createFstab()
{
	FILE *fp;
	fp = fopen("/etc/fstab", "w");
	if (fp == NULL)
        LOGI("=> Can not open /etc/fstab.\n");
    else 
	{
        if (boo.mountable)      createFstabEntry(fp, &boo);
        if (sys.mountable)      createFstabEntry(fp, &sys);
		if (dat.mountable)      createFstabEntry(fp, &dat);
        if (cac.mountable)      createFstabEntry(fp, &cac);
        if (sdcext.mountable)   createFstabEntry(fp, &sdcext);
        if (sdcint.mountable)   createFstabEntry(fp, &sdcint);
        if (sde.mountable)      createFstabEntry(fp, &sde);
        if (sp1.mountable)      createFstabEntry(fp, &sp1);
        if (sp2.mountable)      createFstabEntry(fp, &sp2);
        if (sp3.mountable)      createFstabEntry(fp, &sp3);
	}
	fclose(fp);
}

void verifyFst()
{
	FILE *fp;
	char blkOutput[100];
	char* blk;
    char* arg;
    char* ptr;
    struct dInfo* dat;

    // This has a tendency to hang on MTD devices.
    if (isMTDdevice)    return;

	fp = __popen("blkid","r");
	while (fgets(blkOutput,sizeof(blkOutput),fp) != NULL)
    {
        blk = blkOutput;
        ptr = blkOutput;
        while (*ptr > 32 && *ptr != ':')        ptr++;
        if (*ptr == 0)                          continue;
        *ptr = 0;

        // Increment by two, but verify that we don't hit a NULL
        ptr++;
        if (*ptr != 0)      ptr++;

        // Now, find the TYPE field
        while (1)
        {
            arg = ptr;
            while (*ptr > 32)       ptr++;
            if (*ptr != 0)
            {
                *ptr = 0;
                ptr++;
            }

            if (strlen(arg) > 6)
            {
                if (memcmp(arg, "TYPE=\"", 6) == 0)  break;
                if (memcmp(arg, "TYPE=\"", 6) == 0)  break;
                if (memcmp(arg, "TYPE=\"", 6) == 0)  break;
            }

            if (*ptr == 0)
            {
                arg = NULL;
                break;
            }
        }

        if (arg && strlen(arg) > 7)
        {
            arg += 6;   // Skip the TYPE=" portion
            arg[strlen(arg)-1] = '\0';  // Drop the tail quote
        }
        else
            continue;

        dat = findDeviceByBlockDevice(blk);
        if (dat)
			strcpy(sys.fst,arg);
	}
	__pclose(fp);
}

char backupToChar(enum backup_method method)
{
    switch (method)
    {
    case unknown:
    default:
        return 'u';
    case none:
        return 'n';
    case image:
        return 'i';
    case files:
        return 'f';
    }
    return 'u';
}

void dumpPartitionEntry(struct dInfo* mnt)
{
    char* mntName = mnt->mnt;
    if (strcmp(mntName, ".android_secure") == 0)    mntName = (char*) "andsec";

    fprintf(stderr, "| %8s | %27s | %6s | %8lu | %8lu | %d | %c |\n", 
            mntName, mnt->blk, mnt->fst, (unsigned long) (mnt->sze / 1024), (unsigned long) (mnt->used / 1024), mnt->mountable ? 1 : 0, 
            backupToChar(mnt->backup));
}

void dumpPartitionTable(void)
{
    fprintf(stderr, "+----------+-----------------------------+--------+----------+----------+---+---+\n");
    fprintf(stderr, "| Mount    | Block Device                | fst    | Size(KB) | Used(KB) | M | B |\n");
    fprintf(stderr, "+----------+-----------------------------+--------+----------+----------+---+---+\n");

    dumpPartitionEntry(&tmp);
    dumpPartitionEntry(&sys);
    dumpPartitionEntry(&dat);
    dumpPartitionEntry(&boo);
    dumpPartitionEntry(&rec);
    dumpPartitionEntry(&cac);
    dumpPartitionEntry(&sdcext);
    dumpPartitionEntry(&sdcint);
    dumpPartitionEntry(&ase);
    dumpPartitionEntry(&sde);
    dumpPartitionEntry(&sp1);
    dumpPartitionEntry(&sp2);
    dumpPartitionEntry(&sp3);
    fprintf(stderr, "+----------+-----------------------------+------+----------+----------+---+---+\n");
}

