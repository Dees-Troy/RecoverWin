// Header goes here

#ifndef _DDFTW_HEADER
#define _DDFTW_HEADER

static const char _dinfo_file[] = "/etc/device.info";
static const char _block[] = "/dev/block/";
static const char _mtd[] = "/dev/mtd/";
static int has_datadata = 0;

enum backup_method {
    unknown = 0, 
    none, 
    image, 
    files,
};

enum flash_memory_type {
    unknown_flash_memory_type = 0, 
    mtd, 
    emmc, 
};

struct dInfo {
	char mnt[20];
	char blk[100];
	char dev[100];
	char fst[10];
	char fnm[256];
	char format_location[256];
	unsigned long long sze;
    unsigned long long used;
    int mountable;
    enum backup_method backup;
	enum flash_memory_type memory_type;
};

struct dInfo* findDeviceByLabel(const char* label);

void readRecFstab();
void verifyFst();
void createFstab();
void listMntInfo(struct dInfo* mMnt, char* variable_name);
int getLocations();
void updateUsedSized();

extern struct dInfo tmp, sys, dat, datadata, boo, rec, cac, sdcext, sdcint, ase, sde, sp1, sp2, sp3;
extern char device_name[20];

#endif  // _DDFTW_HEADER

