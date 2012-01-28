// text.cpp - GUIText object

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include <string>

extern "C" {
#include "../common.h"
#include "../minui/minui.h"
#include "../recovery_ui.h"
}

#include "rapidxml.hpp"
#include "objects.hpp"

GUIText::GUIText(xml_node<>* node)
    : Conditional(node)
{
    xml_attribute<>* attr;
    xml_node<>* child;

    mFont = NULL;
    mIsStatic = 1;
    mVarChanged = 0;
    mFontHeight = 0;

    if (!node)      return;

    // Initialize color to solid black
    memset(&mColor, 0, sizeof(COLOR));
    mColor.alpha = 255;

    attr = node->first_attribute("color");
    if (attr)
    {
        std::string color = attr->value();
        ConvertStrToColor(color, &mColor);
    }

    // Load the font, and possibly override the color
    child = node->first_node("font");
    if (child)
    {
        attr = child->first_attribute("resource");
        if (attr)
            mFont = PageManager::FindResource(attr->value());

        attr = child->first_attribute("color");
        if (attr)
        {
            std::string color = attr->value();
            ConvertStrToColor(color, &mColor);
        }
    }

    // Load the placement
    LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY, &mRenderW, &mRenderH, &mPlacement);

    child = node->first_node("text");
    if (child)  mText = child->value();

    // Simple way to check for static state
    mLastValue = DataManager::ParseText(mText);
    if (mLastValue != mText)   mIsStatic = 0;

    gr_getFontDetails(mFont ? mFont->GetResource() : NULL, (unsigned*) &mFontHeight, NULL);
    return;
}

int GUIText::Render(void)
{
    if (!isConditionTrue())     return 0;

    void* fontResource = NULL;

    if (mFont)  fontResource = mFont->GetResource();

    mLastValue = DataManager::ParseText(mText);
    mVarChanged = 0;

    int x = mRenderX, y = mRenderY;
    int width = gr_measureEx(mLastValue.c_str(), fontResource);

    if (mPlacement != TOP_LEFT && mPlacement != BOTTOM_LEFT)
    {
        if (mPlacement == CENTER)
            x -= (width / 2);
        else
            x -= width;
    }
    if (mPlacement != TOP_LEFT && mPlacement != TOP_RIGHT)
    {
        if (mPlacement == CENTER)
            y -= (mFontHeight / 2);
        else
            y -= mFontHeight;
    }

    gr_color(mColor.red, mColor.green, mColor.blue, mColor.alpha);
    gr_textEx(x, y, mLastValue.c_str(), fontResource);
    return 0;
}

int GUIText::Update(void)
{
    if (!isConditionTrue())     return 0;

    static int updateCounter = 3;

    // This hack just makes sure we update at least once a minute for things like clock and battery
    if (updateCounter)  updateCounter--;
    else
    {
        mVarChanged = 1;
        updateCounter = 3;
    }

    if (mIsStatic || !mVarChanged)      return 0;

    std::string newValue = DataManager::ParseText(mText);
    if (mLastValue == newValue)         return 0;
    return 2;
}

int GUIText::GetCurrentBounds(int& w, int& h)
{
    void* fontResource = NULL;

    if (mFont)  fontResource = mFont->GetResource();

    h = mFontHeight;
    w = gr_measureEx(mLastValue.c_str(), fontResource);
    return 0;
}

int GUIText::NotifyVarChange(std::string varName, std::string value)
{
    mVarChanged = 1;
    return 0;
}

