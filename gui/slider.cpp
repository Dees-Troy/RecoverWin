// slider.cpp - GUISlider object

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

GUISlider::GUISlider(xml_node<>* node)
{
    xml_attribute<>* attr;
    xml_node<>* child;

    mAction = NULL;
    mSlider = NULL;
    mSliderUsed = NULL;
    mTouch = NULL;
    mTouchW = 20;

    if (!node)
    {
        LOGE("GUISlider created without XML node\n");
        return;
    }

    child = node->first_node("resource");
    if (child)
    {
        attr = child->first_attribute("base");
        if (attr)
            mSlider = PageManager::FindResource(attr->value());

        attr = child->first_attribute("used");
        if (attr)
            mSliderUsed = PageManager::FindResource(attr->value());

        attr = child->first_attribute("touch");
        if (attr)
            mTouch = PageManager::FindResource(attr->value());
    }

    // Load the placement
    LoadPlacement(node->first_node("placement"), &mRenderX, &mRenderY);

    if (mSlider && mSlider->GetResource())
    {
        mRenderW = gr_get_width(mSlider->GetResource());
        mRenderH = gr_get_height(mSlider->GetResource());
    }
    if (mTouch && mTouch->GetResource())
    {
        mTouchW = gr_get_width(mTouch->GetResource());
        mTouchH = gr_get_height(mTouch->GetResource());
    }

    mActionX = mRenderX;
    mActionY = mRenderY;
    mActionW = mRenderW;
    mActionH = mRenderH;

    mAction = new GUIAction(node);

    mCurTouchX = mRenderX;
    mUpdate = 1;

    return;
}

GUISlider::~GUISlider()
{
    delete mAction;
}

int GUISlider::Render(void)
{
    if (!mSlider || !mSlider->GetResource())       return -1;

    // Draw the slider
    gr_blit(mSlider->GetResource(), 0, 0, mRenderW, mRenderH, mRenderX, mRenderY);

    // Draw the used
    if (mSliderUsed && mSliderUsed->GetResource() && mCurTouchX > mRenderX)
    {
        gr_blit(mSliderUsed->GetResource(), 0, 0, mCurTouchX - mRenderX, mRenderH, mRenderX, mRenderY);
    }

    // Draw the touch icon
    if (mTouch && mTouch->GetResource())
    {
        gr_blit(mTouch->GetResource(), 0, 0, mTouchW, mTouchH, mCurTouchX, (mRenderY + ((mRenderH - mTouchH) / 2)));
    }

    mUpdate = 0;
    return 0;
}

int GUISlider::Update(void)
{
    if (mUpdate)    return 2;
    return 0;
}

int GUISlider::NotifyTouch(TOUCH_STATE state, int x, int y)
{
    static bool dragging = false;

    switch (state)
    {
    case TOUCH_START:
        if (x >= mRenderX && x <= mRenderX + mTouchW && 
            y >= mRenderY && y <= mRenderY + mRenderH)
        {
            mCurTouchX = x - (mTouchW / 2);
            if (mCurTouchX < mRenderX)  mCurTouchX = mRenderX;
            dragging = true;
        }
        break;

    case TOUCH_DRAG:
        if (!dragging)  return 0;
        if (y < mRenderY - mTouchH || y > mRenderY + (mTouchH * 2))
        {
            mCurTouchX = mRenderX;
            dragging = false;
            mUpdate = 1;
            break;
        }
        mCurTouchX = x - (mTouchW / 2);
        if (mCurTouchX < mRenderX)                          mCurTouchX = mRenderX;
        if (mCurTouchX > mRenderX + mRenderW - mTouchW)     mCurTouchX = mRenderX + mRenderW - mTouchW;
        mUpdate = 1;
        break;

    case TOUCH_RELEASE:
        if (!dragging)  return 0;
        if (mCurTouchX >= mRenderX + mRenderW - mTouchW)
        {
            mAction->doActions();
        }
        mCurTouchX = mRenderX;
        dragging = false;
        mUpdate = 1;
        break;
    }
    return 0;
}

