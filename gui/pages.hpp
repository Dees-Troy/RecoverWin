// pages.hpp - Base classes for page manager of GUI

#ifndef _PAGES_HEADER
#define _PAGES_HEADER

typedef struct {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
} COLOR;

// Utility Functions
int ConvertStrToColor(std::string str, COLOR* color);
int gui_forceRender(void);
int gui_changePage(std::string newPage);
int gui_changeOverlay(std::string newPage);

class Resource;
class ResourceManager;
class RenderObject;
class ActionObject;

class Page
{
public:
    virtual ~Page()             {}

public:
    Page(xml_node<>* page, xml_node<>* templates = NULL);
    std::string GetName(void)   { return mName; }

public:
    virtual int Render(void);
    virtual int Update(void);
    virtual int NotifyTouch(TOUCH_STATE state, int x, int y);
    virtual int NotifyKey(int key);
    virtual int NotifyVarChange(std::string varName, std::string value);
    virtual void SetPageFocus(int inFocus);

protected:
    std::string mName;
    std::vector<RenderObject*> mRenders;
    std::vector<ActionObject*> mActions;

    ActionObject* mTouchStart;
    COLOR mBackground;

protected:
    bool ProcessNode(xml_node<>* page, xml_node<>* templates = NULL, int depth = 0);
};

class PageSet
{
public:
    PageSet(char* xmlFile);
    virtual ~PageSet();

public:
    int Load(ZipArchive* package);

    Page* FindPage(std::string name);
    int SetPage(std::string page);
    int SetOverlay(Page* page);
    Resource* FindResource(std::string name);

    // Helper routine for identifing if we're the current page
    int IsCurrentPage(Page* page);

    // These are routing routines
    int Render(void);
    int Update(void);
    int NotifyTouch(TOUCH_STATE state, int x, int y);
    int NotifyKey(int key);
    int NotifyVarChange(std::string varName, std::string value);

protected:
    int LoadPages(xml_node<>* pages, xml_node<>* templates = NULL);
    int LoadVariables(xml_node<>* vars);

protected:
    char* mXmlFile;
    xml_document<> mDoc;
    ResourceManager* mResources;
    std::vector<Page*> mPages;
    Page* mCurrentPage;
    Page* mOverlayPage;     // This is a special case, used for "locking" the screen
};

class PageManager
{
public:
    // Used by GUI
    static int LoadPackage(std::string name, std::string package);
    static PageSet* SelectPackage(std::string name);
    static int ReloadPackage(std::string name, std::string package);
    static void ReleasePackage(std::string name);
    static bool IsCurrentPackage(std::string name);

    // Used for actions and pages
    static int ChangePage(std::string name);
    static int ChangeOverlay(std::string name);
    static Resource* FindResource(std::string name);
    static Resource* FindResource(std::string package, std::string name);

    // Used for console-only mode - Can be reverted via ChangePage
    static int SwitchToConsole(void);

    // Helper to identify if a particular page is the active page
    static int IsCurrentPage(Page* page);

    // These are routing routines
    static int Render(void);
    static int Update(void);
    static int NotifyTouch(TOUCH_STATE state, int x, int y);
    static int NotifyKey(int key);
    static int NotifyVarChange(std::string varName, std::string value);

protected:
    static PageSet* FindPackage(std::string name);

protected:
    static std::map<std::string, PageSet*> mPageSets;
    static PageSet* mCurrentSet;
    static PageSet* mBaseSet;
};

#endif  // _PAGES_HEADER

