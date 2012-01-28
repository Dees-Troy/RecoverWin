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

#ifndef _VARIABLES_HEADER_
#define _VARIABLES_HEADER_

#define VAR_VERSION_STR              "2.0.0"

#define VAR_USE_COMPRESSION_VAR      "_use_compression"

#define VAR_BACKUP_SYSTEM_VAR        "_backup_system"
#define VAR_BACKUP_DATA_VAR          "_backup_data"
#define VAR_BACKUP_BOOT_VAR          "_backup_boot"
#define VAR_BACKUP_RECOVERY_VAR      "_backup_recovery"
#define VAR_BACKUP_CACHE_VAR         "_backup_cache"
#define VAR_BACKUP_ANDSEC_VAR        "_backup_andsec"
#define VAR_BACKUP_SDEXT_VAR         "_backup_sdext"
#define VAR_BACKUP_SP1_VAR           "_backup_sp1"
#define VAR_BACKUP_SP2_VAR           "_backup_sp2"
#define VAR_BACKUP_SP3_VAR           "_backup_sp3"
#define VAR_BACKUP_AVG_IMG_RATE      "_backup_avg_img_rate"
#define VAR_BACKUP_AVG_FILE_RATE     "_backup_avg_file_rate"
#define VAR_BACKUP_AVG_FILE_COMP_RATE    "_backup_avg_file_comp_rate"

#define VAR_RESTORE_SYSTEM_VAR       "_restore_system"
#define VAR_RESTORE_DATA_VAR         "_restore_data"
#define VAR_RESTORE_BOOT_VAR         "_restore_boot"
#define VAR_RESTORE_RECOVERY_VAR     "_restore_recovery"
#define VAR_RESTORE_CACHE_VAR        "_restore_cache"
#define VAR_RESTORE_ANDSEC_VAR       "_restore_andsec"
#define VAR_RESTORE_SDEXT_VAR        "_restore_sdext"
#define VAR_RESTORE_SP1_VAR          "_restore_sp1"
#define VAR_RESTORE_SP2_VAR          "_restore_sp2"
#define VAR_RESTORE_SP3_VAR          "_restore_sp3"
#define VAR_RESTORE_AVG_IMG_RATE     "_restore_avg_img_rate"
#define VAR_RESTORE_AVG_FILE_RATE    "_restore_avg_file_rate"
#define VAR_RESTORE_AVG_FILE_COMP_RATE    "_restore_avg_file_comp_rate"

#define VAR_SHOW_SPAM_VAR            "_show_spam"
#define VAR_COLOR_THEME_VAR          "_color_theme"
#define VAR_VERSION_VAR              "_version"
#define VAR_SORT_FILES_BY_DATE_VAR   "_sort_files_by_date"
#define VAR_GUI_SORT_ORDER           "_gui_sort_order"
#define VAR_ZIP_LOCATION_VAR         "_zip_location"
#define VAR_FORCE_MD5_CHECK_VAR      "_force_md5_check"
#define VAR_SKIP_MD5_CHECK_VAR       "_skip_md5_check"
#define VAR_SKIP_MD5_GENERATE_VAR    "_skip_md5_generate"
#define VAR_SIGNED_ZIP_VERIFY_VAR    "_signed_zip_verify"
#define VAR_REBOOT_AFTER_FLASH_VAR   "_reboot_after_flash_option"
#define VAR_TIME_ZONE_VAR            "_time_zone"
#define VAR_RM_RF_VAR                "_rm_rf"

#define VAR_BACKUPS_FOLDER_VAR       "_backups_folder"

#define VAR_SP1_PARTITION_NAME_VAR   "_sp1_name"
#define VAR_SP2_PARTITION_NAME_VAR   "_sp2_name"
#define VAR_SP3_PARTITION_NAME_VAR   "_sp3_name"

#define VAR_SDEXT_SIZE               "_sdext_size"
#define VAR_SWAP_SIZE                "_swap_size"
#define VAR_SDPART_FILE_SYSTEM       "_sdpart_file_system"
#define VAR_TIME_ZONE_GUISEL         "_time_zone_guisel"
#define VAR_TIME_ZONE_GUIOFFSET      "_time_zone_guioffset"
#define VAR_TIME_ZONE_GUIDST         "_time_zone_guidst"

#define VAR_ACTION_BUSY              "_busy"

#define VAR_ALLOW_PARTITION_SDCARD   "_allow_partition_sdcard"

#define VAR_SCREEN_OFF               "_screen_off"

#define VAR_REBOOT_SYSTEM            "_reboot_system"
#define VAR_REBOOT_RECOVERY          "_reboot_recovery"
#define VAR_REBOOT_POWEROFF          "_reboot_poweroff"
#define VAR_REBOOT_BOOTLOADER        "_reboot_bootloader"

#define VAR_CURRENT_THEME            "_theme"
#define VAR_CURRENT_PACKAGE          "_package"
#define VAR_CURRENT_PAGE             "_page"

// Also used:
//   _boot_is_mountable
//   _system_is_mountable
//   _data_is_mountable
//   _cache_is_mountable
//   _sdcext_is_mountable
//   _sdcint_is_mountable
//   _sd-ext_is_mountable
//   _sp1_is_mountable
//   _sp2_is_mountable
//   _sp3_is_mountable

#endif  // _VARIABLES_HEADER_

