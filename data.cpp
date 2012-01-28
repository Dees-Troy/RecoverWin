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

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>
#include <utility>
#include <map>
#include <fstream>
#include <sstream>

#include "data.hpp"

extern "C"
{
    #include "common.h"
    #include "data.h"
	#include "ddftw.h"
    #include "phx_reboot.h"

    int get_battery_level(void);
    void get_device_id(void);

    extern char device_id[15];

    void gui_notifyVarChange(const char *name, const char* value);
}

#define FILE_VERSION    0x00010001

using namespace std;

DataManager::TValueMap  DataManager::mValues;
DataManager::TArrayMap  DataManager::mArrays;
DataManager::TStrMap    DataManager::mConstValues;
string                  DataManager::mBackingFile;
int                     DataManager::mInitialized = 0;

int DataManager::ResetDefaults()
{
    mValues.clear();
    mConstValues.clear();
    SetDefaultValues();
    return 0;
}

int DataManager::LoadValues(const string filename)
{
    if (!mInitialized)
        SetDefaultValues();

    // Save off the backing file for set operations
    mBackingFile = filename;

    // Read in the file, if possible
    FILE* in = fopen(filename.c_str(), "rb");
    if (!in)    return 0;

    int file_version;
    if (fread(&file_version, 1, sizeof(int), in) != sizeof(int))    goto error;
    if (file_version != FILE_VERSION)                               goto error;

    while (!feof(in))
    {
        string Name;
        string Value;
        unsigned short length;
        char array[512];

        if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))    goto error;
        if (length >= 512)                                                              goto error;
        if (fread(array, 1, length, in) != length)                                      goto error;
        Name = array;

        if (fread(&length, 1, sizeof(unsigned short), in) != sizeof(unsigned short))    goto error;
        if (length >= 512)                                                              goto error;
        if (fread(array, 1, length, in) != length)                                      goto error;
        Value = array;

        TValueMap::iterator pos;

        pos = mValues.find(Name);
        if (pos != mValues.end())
        {
            pos->second.first = Value;
            pos->second.second = 1;
        }
        else
            mValues.insert(TNameValuePair(Name, TStrIntPair(Value, 1)));
    }
    fclose(in);
    return 0;

error:
    // File version mismatch. Use defaults.
    fclose(in);
    return -1;
}

int DataManager::Flush()
{
    return SaveValues();
}

int DataManager::SaveValues()
{
    if (mBackingFile.empty())       return -1;

    FILE* out = fopen(mBackingFile.c_str(), "wb");
    if (!out)                       return -1;

    int file_version = FILE_VERSION;
    fwrite(&file_version, 1, sizeof(int), out);

    TValueMap::iterator iter;
    for (iter = mValues.begin(); iter != mValues.end(); ++iter)
    {
        // Save only the persisted data
        if (iter->second.second != 0)
        {
            unsigned short length = (unsigned short) iter->first.length() + 1;
            fwrite(&length, 1, sizeof(unsigned short), out);
            fwrite(iter->first.c_str(), 1, length, out);
            length = (unsigned short) iter->second.first.length() + 1;
            fwrite(&length, 1, sizeof(unsigned short), out);
            fwrite(iter->second.first.c_str(), 1, length, out);
        }
    }
    fclose(out);
    return 0;
}

int DataManager::GetValue(const string varName, string& value)
{
    string localStr = varName;

    if (!mInitialized)
        SetDefaultValues();

    // Strip off leading and trailing '%' if provided
    if (localStr.length() > 2 && localStr[0] == '%' && localStr[localStr.length()-1] == '%')
    {
        localStr.erase(0, 1);
        localStr.erase(localStr.length() - 1, 1);
    }

    // Handle magic values
    if (GetMagicValue(localStr, value) == 0)     return 0;

    TStrMap::iterator constPos;
    constPos = mConstValues.find(localStr);
    if (constPos != mConstValues.end())
    {
        value = constPos->second;
        return 0;
    }

    TValueMap::iterator pos;
    pos = mValues.find(localStr);
    if (pos == mValues.end())
        return -1;

    value = pos->second.first;
    return 0;
}

int DataManager::GetValue(const string varName, int& value)
{
    string data;

    if (GetValue(varName,data) != 0)
        return -1;

    value = atoi(data.c_str());
    return 0;
}

// This is a dangerous function. It will create the value if it doesn't exist so it has a valid c_str
string& DataManager::GetValueRef(const string varName)
{
    if (!mInitialized)
        SetDefaultValues();

    TStrMap::iterator constPos;
    constPos = mConstValues.find(varName);
    if (constPos != mConstValues.end())
        return constPos->second;

    TValueMap::iterator pos;
    pos = mValues.find(varName);
    if (pos == mValues.end())
        pos = (mValues.insert(TNameValuePair(varName, TStrIntPair("", 0)))).first;

    return pos->second.first;
}

// This function will return an empty string if the value doesn't exist
string DataManager::GetStrValue(const string varName)
{
    string retVal;

    GetValue(varName, retVal);
    return retVal;
}

// This function will return 0 if the value doesn't exist
int DataManager::GetIntValue(const string varName)
{
    string retVal;

    GetValue(varName, retVal);
    return atoi(retVal.c_str());
}

// This routine will convert all instances of variables
string DataManager::ParseText(const string str)
{
    string retVal;
    static int counter = 0;
    size_t pos = 0;
    size_t next = 0, end = 0;

    while (1)
    {
        next = retVal.find('%', pos);
        if (next == string::npos)      return retVal;
        end = retVal.find('%', next + 1);
        if (end == string::npos)       return retVal;

        // We have a block of data
        string var = retVal.substr(next + 1, (end - next) - 1);
        retVal.erase(next, (end - next) + 1);

        if (next + 1 == end)
        {
            retVal.insert(next, 1, '%');
        }
        else
        {
            string value;

            if (DataManager::GetValue(var, value) == 0)
                retVal.insert(next, value);
        }

        pos = next + 1;
    }
}

int DataManager::SetValue(const string varName, string value, int persist /* = 0 */)
{
    if (!mInitialized)
        SetDefaultValues();

    // Don't allow empty values or numerical starting values
    if (varName.empty() || (varName[0] >= '0' && varName[0] <= '9'))
        return -1;

    TStrMap::iterator constChk;
    constChk = mConstValues.find(varName);
    if (constChk != mConstValues.end())
        return -1;

    TValueMap::iterator pos;
    pos = mValues.find(varName);
    if (pos == mValues.end())
        pos = (mValues.insert(TNameValuePair(varName, TStrIntPair(value, persist)))).first;
    else
        pos->second.first = value;

    if (pos->second.second != 0)
        SaveValues();

    gui_notifyVarChange(varName.c_str(), value.c_str());
    return 0;
}

int DataManager::SetValue(const string varName, int value, int persist /* = 0 */)
{
    ostringstream valStr;
    valStr << value;
    return SetValue(varName, valStr.str(), persist);;
}

int DataManager::SetValue(const string varName, float value, int persist /* = 0 */)
{
    ostringstream valStr;
    valStr << value;
    return SetValue(varName, valStr.str(), persist);;
}

void DataManager::DumpValues()
{
    TValueMap::iterator iter;
    ui_print("Data Manager dump - Values with leading X are persisted.\n");
    for (iter = mValues.begin(); iter != mValues.end(); ++iter)
    {
        ui_print("%c %s=%s\n", iter->second.second ? 'X' : ' ', iter->first.c_str(), iter->second.first.c_str());
    }
}

void DataManager::SetDefaultValues()
{
    string str;

    get_device_id();

    str = "/sdcard/phoenix/backups/";
    str += device_id;

    mInitialized = 1;

    mConstValues.insert(make_pair("true", "1"));
    mConstValues.insert(make_pair("false", "0"));

    mConstValues.insert(make_pair(VAR_VERSION_VAR, VAR_VERSION_STR));
    mConstValues.insert(make_pair(VAR_BACKUPS_FOLDER_VAR, str));

#ifdef BOARD_HAS_NO_REAL_SDCARD
    mConstValues.insert(make_pair(VAR_ALLOW_PARTITION_SDCARD, "0"));
#else
    mConstValues.insert(make_pair(VAR_ALLOW_PARTITION_SDCARD, "1"));
#endif

    if (strlen(EXPAND(SP1_DISPLAY_NAME)))    mConstValues.insert(make_pair(VAR_SP1_PARTITION_NAME_VAR, EXPAND(SP1_DISPLAY_NAME)));
    if (strlen(EXPAND(SP2_DISPLAY_NAME)))    mConstValues.insert(make_pair(VAR_SP2_PARTITION_NAME_VAR, EXPAND(SP2_DISPLAY_NAME)));
    if (strlen(EXPAND(SP3_DISPLAY_NAME)))    mConstValues.insert(make_pair(VAR_SP3_PARTITION_NAME_VAR, EXPAND(SP3_DISPLAY_NAME)));

    mConstValues.insert(make_pair(VAR_REBOOT_SYSTEM, phx_isRebootCommandSupported(rb_system) ? "1" : "0"));
    mConstValues.insert(make_pair(VAR_REBOOT_RECOVERY, phx_isRebootCommandSupported(rb_recovery) ? "1" : "0"));
    mConstValues.insert(make_pair(VAR_REBOOT_POWEROFF, phx_isRebootCommandSupported(rb_poweroff) ? "1" : "0"));
    mConstValues.insert(make_pair(VAR_REBOOT_BOOTLOADER, phx_isRebootCommandSupported(rb_bootloader) ? "1" : "0"));

    mValues.insert(make_pair(VAR_BACKUP_SYSTEM_VAR, make_pair("1", 1)));
    mValues.insert(make_pair(VAR_BACKUP_DATA_VAR, make_pair("1", 1)));
    mValues.insert(make_pair(VAR_BACKUP_BOOT_VAR, make_pair("1", 1)));
    mValues.insert(make_pair(VAR_BACKUP_RECOVERY_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_BACKUP_CACHE_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_BACKUP_SP1_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_BACKUP_SP2_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_BACKUP_SP3_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_BACKUP_ANDSEC_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_BACKUP_SDEXT_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_REBOOT_AFTER_FLASH_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_SIGNED_ZIP_VERIFY_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_FORCE_MD5_CHECK_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_COLOR_THEME_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_USE_COMPRESSION_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_SHOW_SPAM_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_TIME_ZONE_VAR, make_pair("CST6CDT", 1)));
    mValues.insert(make_pair(VAR_ZIP_LOCATION_VAR, make_pair("/sdcard", 1)));
    mValues.insert(make_pair(VAR_SORT_FILES_BY_DATE_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_GUI_SORT_ORDER, make_pair("1", 1)));
    mValues.insert(make_pair(VAR_RM_RF_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_SKIP_MD5_CHECK_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_SKIP_MD5_GENERATE_VAR, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_SDEXT_SIZE, make_pair("512", 1)));
    mValues.insert(make_pair(VAR_SWAP_SIZE, make_pair("32", 1)));
    mValues.insert(make_pair(VAR_SDPART_FILE_SYSTEM, make_pair("ext3", 1)));
    mValues.insert(make_pair(VAR_TIME_ZONE_GUISEL, make_pair("CST6;CDT", 1)));
    mValues.insert(make_pair(VAR_TIME_ZONE_GUIOFFSET, make_pair("0", 1)));
    mValues.insert(make_pair(VAR_TIME_ZONE_GUIDST, make_pair("1", 1)));
    mValues.insert(make_pair(VAR_ACTION_BUSY, make_pair("0", 0)));
    mValues.insert(make_pair(VAR_BACKUP_AVG_IMG_RATE, make_pair("15000000", 1)));
    mValues.insert(make_pair(VAR_BACKUP_AVG_FILE_RATE, make_pair("3000000", 1)));
    mValues.insert(make_pair(VAR_BACKUP_AVG_FILE_COMP_RATE, make_pair("2000000", 1)));
    mValues.insert(make_pair(VAR_RESTORE_AVG_IMG_RATE, make_pair("15000000", 1)));
    mValues.insert(make_pair(VAR_RESTORE_AVG_FILE_RATE, make_pair("3000000", 1)));
    mValues.insert(make_pair(VAR_RESTORE_AVG_FILE_COMP_RATE, make_pair("2000000", 1)));
}

// Magic Values
int DataManager::GetMagicValue(const string varName, string& value)
{
    // Handle special dynamic cases
    if (varName == "_time")
    {
        char tmp[32];

        struct tm *current;
        time_t now;
        now = time(0);
        current = localtime(&now);

        if (current->tm_hour >= 12)
            sprintf(tmp, "%d:%02d PM", current->tm_hour == 12 ? 12 : current->tm_hour - 12, current->tm_min);
        else
            sprintf(tmp, "%d:%02d AM", current->tm_hour == 0 ? 12 : current->tm_hour, current->tm_min);

        value = tmp;
        return 0;
    }
    if (varName == "_battery")
    {
        char tmp[16];

        sprintf(tmp, "%i%%", get_battery_level());
        value = tmp;
        return 0;
    }
    return -1;
}

int DataManager::PushArray(const string varName, string value)
{
    // Don't allow empty values or numerical starting values
    if (varName.empty() || (varName[0] >= '0' && varName[0] <= '9'))
        return -1;

    // Strip off leading and trailing '%' if provided
    string localStr = varName;
    if (localStr.length() > 2 && localStr[0] == '%' && localStr[localStr.length()-1] == '%')
    {
        localStr.erase(0, 1);
        localStr.erase(localStr.length() - 1, 1);
    }

    // Make sure we don't already have a variable of this name
    string tmp;
    if (GetValue(varName, tmp) >= 0)
        return -1;

    TArrayMap::iterator pos;
    pos = mArrays.find(localStr);
    if (pos == mArrays.end())
    {
        TStrArr strArray;
        strArray.push_back(value);
        mArrays.insert(TNameArrayPair(varName, strArray));
    }
    else
    {
        pos->second.push_back(value);
    }

    gui_notifyVarChange(varName.c_str(), value.c_str());
    return 0;
}

int DataManager::PopArray(const string varName, string& value)
{
    string localStr = varName;

    // Strip off leading and trailing '%' if provided
    if (localStr.length() > 2 && localStr[0] == '%' && localStr[localStr.length()-1] == '%')
    {
        localStr.erase(0, 1);
        localStr.erase(localStr.length() - 1, 1);
    }

    TArrayMap::iterator pos;
    pos = mArrays.find(localStr);
    if (pos == mArrays.end())
        return -1;

    if (pos->second.size() == 0)
        return -1;

    value = pos->second.back();
    pos->second.pop_back();

    return 0;
}

extern "C" int DataManager_ResetDefaults()
{
    return DataManager::ResetDefaults();
}


extern "C" int DataManager_LoadValues(const char* filename)
{
    return DataManager::LoadValues(filename);
}

extern "C" int DataManager_Flush()
{
    return DataManager::Flush();
}

extern "C" int DataManager_GetValue(const char* varName, char* value)
{
    int ret;
    string str;

    ret = DataManager::GetValue(varName, str);
    if (ret == 0)
        strcpy(value, str.c_str());
    return ret;
}

extern "C" const char* DataManager_GetStrValue(const char* varName)
{
    string& str = DataManager::GetValueRef(varName);
    return str.c_str();
}

extern "C" int DataManager_GetIntValue(const char* varName)
{
    return DataManager::GetIntValue(varName);
}

extern "C" int DataManager_SetStrValue(const char* varName, char* value)
{
    return DataManager::SetValue(varName, value, 0);
}

extern "C" int DataManager_SetIntValue(const char* varName, int value)
{
    return DataManager::SetValue(varName, value, 0);
}

extern "C" int DataManager_SetFloatValue(const char* varName, float value)
{
    return DataManager::SetValue(varName, value, 0);
}

extern "C" int DataManager_ToggleIntValue(const char* varName)
{
    if (DataManager::GetIntValue(varName))
        return DataManager::SetValue(varName, 0);
    else
        return DataManager::SetValue(varName, 1);
}

extern "C" void DataManager_DumpValues()
{
    return DataManager::DumpValues();
}

