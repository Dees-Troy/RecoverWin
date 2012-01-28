// Header goes here

#include "ddftw.h"

#ifndef _BACKSTORE_HEADER
#define _BACKSTORE_HEADER

static const char backup_folder[] = "/sdcard/phoenix/backups";
static const char bs_size[] = "4096";

char* nan_img_set(int setting, int backstore);

int nandroid_back_exe();
int nandroid_rest_exe();

void set_restore_files();
char* nan_compress();

int sdSpace;

int makeMD5(const char *imgDir, const char *imgFile);
int checkMD5(const char *imgDir, const char *imgFile);

int phx_isMounted(struct dInfo mMnt);
int phx_mount(struct dInfo mMnt);
int phx_unmount(struct dInfo uMnt);

#endif  // _BACKSTORE_HEADER
