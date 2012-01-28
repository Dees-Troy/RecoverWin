// objects.h - Base classes for object manager of GUI

#ifndef _OBJECTS_HEADER
#define _OBJECTS_HEADER

#include "rapidxml.hpp"
#include <vector>
#include <string>
#include <map>

extern "C" {
#include "../minzip/Zip.h"
}

using namespace rapidxml;

#include "../data.hpp"
#include "resources.hpp"
#include "pages.hpp"

class RenderObject
{
public:
    enum Placement {
        TOP_LEFT = 0,
        TOP_RIGHT = 1,
        BOTTOM_LEFT = 2,
        BOTTOM_RIGHT = 3,
        CENTER = 4,
    };

public:
    RenderObject()              { mRenderX = 0; mRenderY = 0; mRenderW = 0; mRenderH = 0; mPlacement = TOP_LEFT; }
    virtual ~RenderObject()     {}

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void) = 0;

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void)        { return 0; }

    // GetRenderPos - Returns the current position of the object
    virtual int GetRenderPos(int& x, int& y, int& w, int& h)        { x = mRenderX; y = mRenderY; w = mRenderW; h = mRenderH; return 0; }

    // SetRenderPos - Update the position of the object
    //  Return 0 on success, <0 on error
    virtual int SetRenderPos(int x, int y, int w = 0, int h = 0)    { mRenderX = x; mRenderY = y; if (w || h) { mRenderW = w; mRenderH = h; } return 0; }

    // GetPlacement - Returns the current placement
    virtual int GetPlacement(Placement& placement)                  { placement = mPlacement; return 0; }

    // SetPlacement - Update the current placement
    virtual int SetPlacement(Placement placement)                   { mPlacement = placement; return 0; }

    // SetPageFocus - Notify when a page gains or loses focus
    virtual void SetPageFocus(int inFocus)                          { return; }

protected:
    int mRenderX, mRenderY, mRenderW, mRenderH;
    Placement mPlacement;
};

class ActionObject
{
public:
    ActionObject()              { mActionX = 0; mActionY = 0; mActionW = 0; mActionH = 0; }
    virtual ~ActionObject()     {}

public:
    // NotifyTouch - Notify of a touch event
    //  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y)        { return 0; }

    // NotifyKey - Notify of a key press
    //  Return 0 on success (and consume key), >0 to pass key to next handler, and <0 on error
    virtual int NotifyKey(int key)                                  { return 1; }

    // GetRenderPos - Returns the current position of the object
    virtual int GetActionPos(int& x, int& y, int& w, int& h)        { x = mActionX; y = mActionY; w = mActionW; h = mActionH; return 0; }

    // SetRenderPos - Update the position of the object
    //  Return 0 on success, <0 on error
    virtual int SetActionPos(int x, int y, int w = 0, int h = 0);

    // IsInRegion - Checks if the request is handled by this object
    //  Return 0 if this object handles the request, 1 if not
    virtual int IsInRegion(int x, int y)                            { return ((x < mActionX || x > mActionX + mActionW || y < mActionY || y > mActionY + mActionH) ? 0 : 1); }

    // NotifyVarChange - Notify of a variable change
    //  Returns 0 on success, <0 on error
    virtual int NotifyVarChange(std::string varName, std::string value)     { return 0; }

protected:
    int mActionX, mActionY, mActionW, mActionH;
};

class Conditional
{
public:
    Conditional(xml_node<>* node);

public:
    bool IsConditionVariable(std::string var);
    bool isConditionTrue();
    bool isConditionValid();
    void NotifyPageSet();

protected:
    class Condition
    {
    public:
        std::string mVar1;
        std::string mVar2;
        std::string mCompareOp;
        std::string mLastVal;
    };

    std::vector<Condition> mConditions;

protected:
    bool isMounted(std::string vol);
    bool isConditionTrue(Condition* condition);

};

// Derived Objects
// GUIText - Used for static text
class GUIText : public RenderObject, public ActionObject, public Conditional

{
public:
    // w and h may be ignored, in which case, no bounding box is applied
    GUIText(xml_node<>* node);

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // Retrieve the size of the current string (dynamic strings may change per call)
    virtual int GetCurrentBounds(int& w, int& h);

    // Notify of a variable change
    virtual int NotifyVarChange(std::string varName, std::string value);

protected:
    std::string mText;
    std::string mLastValue;
    COLOR mColor;
    Resource* mFont;
    int mIsStatic;
    int mVarChanged;
    int mFontHeight;

protected:
    std::string parseText(void);
};

// GUIImage - Used for static image
class GUIImage : public RenderObject
{
public:
    GUIImage(xml_node<>* node);

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // SetRenderPos - Update the position of the object
    //  Return 0 on success, <0 on error
    virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

protected:
    Resource* mImage;
};

// GUIFill - Used for fill colors
class GUIFill : public RenderObject
{
public:
    GUIFill(xml_node<>* node);

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

protected:
    COLOR mColor;
};

// GUIAction - Used for standard actions
class GUIAction : public ActionObject, public Conditional
{
public:
    GUIAction(xml_node<>* node);

public:
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);
    virtual int NotifyKey(int key);
    virtual int NotifyVarChange(std::string varName, std::string value);
    virtual int doActions();

protected:
    class Action
    {
    public:
        std::string mFunction;
        std::string mArg;
    };

    std::vector<Action> mActions;
    int mKey;

protected:
    int getKeyByName(std::string key);
    virtual int doAction(Action action, int isThreaded = 0);
    static void* thread_start(void *cookie);
    void flash_zip(std::string filename, std::string pageName);

};

class GUIConsole : public RenderObject, public ActionObject
{
public:
    GUIConsole(xml_node<>* node);

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // SetRenderPos - Update the position of the object
    //  Return 0 on success, <0 on error
    virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

    // IsInRegion - Checks if the request is handled by this object
    //  Return 0 if this object handles the request, 1 if not
    virtual int IsInRegion(int x, int y);

    // NotifyTouch - Notify of a touch event
    //  Return 0 on success, >0 to ignore remainder of touch, and <0 on error (Return error to allow other handlers)
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
    enum SlideoutState
    {
        hidden = 0,
        visible,
        request_hide,
        request_show
    };
    Resource* mFont;
    Resource* mSlideoutImage;
    COLOR mForegroundColor;
    COLOR mBackgroundColor;
    COLOR mScrollColor;
    unsigned int mFontHeight;
    int mCurrentLine;
    unsigned int mLastCount;
    unsigned int mMaxRows;
    int mStartY;
    int mSlideoutX, mSlideoutY, mSlideoutW, mSlideoutH;
    int mSlideinX, mSlideinY, mSlideinW, mSlideinH;
    int mConsoleX, mConsoleY, mConsoleW, mConsoleH;
    int mLastTouchX, mLastTouchY;
    int mSlideMultiplier;
    int mSlideout;
    SlideoutState mSlideoutState;

protected:
    virtual int RenderSlideout(void);
    virtual int RenderConsole(void);

};

class GUIButton : public RenderObject, public ActionObject, public Conditional
{
public:
    GUIButton(xml_node<>* node);
    virtual ~GUIButton();

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // SetPos - Update the position of the render object
    //  Return 0 on success, <0 on error
    virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

    // NotifyTouch - Notify of a touch event
    //  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
    GUIImage* mButtonImg;
    Resource* mButtonIcon;
    GUIText* mButtonLabel;
    GUIAction* mAction;
    int mTextX, mTextY, mTextW, mTextH;
    int mIconX, mIconY, mIconW, mIconH;
    bool mRendered;
};

class GUICheckbox: public RenderObject, public ActionObject, public Conditional
{
public:
    GUICheckbox(xml_node<>* node);
    virtual ~GUICheckbox();

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // SetPos - Update the position of the render object
    //  Return 0 on success, <0 on error
    virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

    // NotifyTouch - Notify of a touch event
    //  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
    Resource* mChecked;
    Resource* mUnchecked;
    GUIText* mLabel;
    int mTextX, mTextY;
    int mCheckX, mCheckY, mCheckW, mCheckH;
    int mLastState;
    bool mRendered;
    std::string mVarName;
};

class GUIFileSelector : public RenderObject, public ActionObject
{
public:
    GUIFileSelector(xml_node<>* node);
    virtual ~GUIFileSelector();

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // NotifyTouch - Notify of a touch event
    //  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

    // NotifyVarChange - Notify of a variable change
    virtual int NotifyVarChange(std::string varName, std::string value);

    // SetPos - Update the position of the render object
    //  Return 0 on success, <0 on error
    virtual int SetRenderPos(int x, int y, int w = 0, int h = 0);

    // SetPageFocus - Notify when a page gains or loses focus
    virtual void SetPageFocus(int inFocus);

protected:
    struct FileData {
        std::string fileName;
        unsigned char fileType;     // Uses d_type format from struct dirent
        mode_t protection;          // Uses mode_t format from stat
        uid_t userId;
        gid_t groupId;
        off_t fileSize;
        time_t lastAccess;          // Uses time_t format from stat
        time_t lastModified;        // Uses time_t format from stat
        time_t lastStatChange;      // Uses time_t format from stat
    };

protected:
    virtual int GetSelection(int x, int y);

    virtual int GetFileList(const std::string folder);
    static bool fileSort(FileData d1, FileData d2);

protected:
    std::vector<FileData> mFolderList;
    std::vector<FileData> mFileList;
    std::string mPathVar;
    std::string mExtn;
    std::string mVariable;
    int mStart;
    int mLineSpacing;
    int mShowFolders, mShowFiles, mShowNavFolders;
    int mUpdate;
    int mBackgroundX, mBackgroundY, mBackgroundW, mBackgroundH;
	static int mSortOrder;
    unsigned mFontHeight;
    unsigned mLineHeight;
    int mIconWidth, mIconHeight;
    Resource* mFolderIcon;
    Resource* mFileIcon;
    Resource* mBackground;
    Resource* mFont;
    COLOR mBackgroundColor;
    COLOR mFontColor;
};

// GUIAnimation - Used for animations
class GUIAnimation : public RenderObject
{
public:
    GUIAnimation(xml_node<>* node);

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

protected:
    AnimationResource* mAnimation;
    int mFrame;
    int mFPS;
    int mLoop;
    int mRender;
    int mUpdateCount;
};

class GUIProgressBar : public RenderObject, public ActionObject
{
public:
    GUIProgressBar(xml_node<>* node);

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // NotifyVarChange - Notify of a variable change
    //  Returns 0 on success, <0 on error
    virtual int NotifyVarChange(std::string varName, std::string value);

protected:
    Resource* mEmptyBar;
    Resource* mFullBar;
    std::string mMinValVar;
    std::string mMaxValVar;
    std::string mCurValVar;
    float mSlide;
    float mSlideInc;
    int mSlideFrames;
    int mLastPos;

protected:
    virtual int RenderInternal(void);       // Does the actual render

};

class GUIKeyboard : public RenderObject, public ActionObject
{
public:
    GUIKeyboard(xml_node<>* node);
    virtual ~GUIKeyboard();

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // NotifyTouch - Notify of a touch event
    //  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
    struct Keys
    {
        int x, y, w, h;
        char value;
    };

protected:
    std::vector<Keys> mKeys;
    std::string mVariable;
    std::string mContents;
    GUIImage* mImage;
};

class GUISlider : public RenderObject, public ActionObject
{
public:
    GUISlider(xml_node<>* node);
    virtual ~GUISlider();

public:
    // Render - Render the full object to the GL surface
    //  Return 0 on success, <0 on error
    virtual int Render(void);

    // Update - Update any UI component animations (called <= 30 FPS)
    //  Return 0 if nothing to update, 1 on success and contiue, >1 if full render required, and <0 on error
    virtual int Update(void);

    // NotifyTouch - Notify of a touch event
    //  Return 0 on success, >0 to ignore remainder of touch, and <0 on error
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);

protected:
    GUIAction* mAction;
    Resource* mSlider;
    Resource* mSliderUsed;
    Resource* mTouch;
    int mTouchW, mTouchH;
    int mCurTouchX;
    int mUpdate;
};


// Helper APIs
bool LoadPlacement(xml_node<>* node, int* x, int* y, int* w = NULL, int* h = NULL, RenderObject::Placement* placement = NULL);

#endif  // _OBJECTS_HEADER

