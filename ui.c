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

#include <stdio.h>
#include <stdarg.h>
#include "data.h"

void gui_print(const char *fmt, ...);
void gui_print_overwrite(const char *fmt, ...);

void ui_show_progress(float portion, int seconds)
{
    DataManager_SetFloatValue("ui_progress_portion", portion * 100.0);
    DataManager_SetIntValue("ui_progress_frames", seconds * 30);
}

void ui_set_progress(float fraction)
{
    DataManager_SetFloatValue("ui_progress", (float) (fraction * 100.0));
}

void ui_reset_progress()
{
    DataManager_SetFloatValue("ui_progress", 0.0);
}

void ui_print(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 512, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);

    gui_print("%s", buf);
}

void ui_print_overwrite(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);

    gui_print_overwrite("%s", buf);

}

