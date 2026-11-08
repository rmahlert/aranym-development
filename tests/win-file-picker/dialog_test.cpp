/* Exercise the production selector against real Windows drive mappings.
 * Only drawing/event dispatch is stubbed; directory and path code is real. */
#ifndef FILE_SELECTOR_SOURCE
#define FILE_SELECTOR_SOURCE "../../src/gui-sdl/dlgFileSelect.cpp"
#endif
#include FILE_SELECTOR_SOURCE
#include "../../src/gui-sdl/file.cpp"

Dialog::Dialog(SGOBJ *d):dlg(d),return_obj(-1) {}
Dialog::~Dialog() {}
void Dialog::init() {}
void Dialog::idle() {}
void Dialog::mouseClick(const SDL_Event &,int,int) {}
void Dialog::keyPress(const SDL_Event &) {}
int Dialog::processDialog() { return GUI_CONTINUE; }
void Dialog::processResult() {}
char *safe_strncpy(char *to,const char *from,size_t n) { if(n) {strncpy(to,from,n-1);to[n-1]=0;}return to; }

class TestDialog:public DlgFileSelect {
public:
    TestDialog(char *path):DlgFileSelect(fsdlg,path,true) { static_cast<Dialog *>(this)->idle(); }
    int click(int n) { return_obj=n;return processDialog(); }
    bool choose(const char *name) {
        int i=0;for(listentry *p=gui_file_list;p;p=p->next,++i) {
            if(!strcmp(p->filename,name)) {click(SGFSDLG_FIRSTENTRY+i);return true;}
        }return false;
    }
};
static int failures;
static void check(bool ok,const char *what) {
    printf("%s %s\n",ok?"PASS":"FAIL",what);fflush(stdout);if(!ok)++failures;
}
int main() {
    char path[MAX_FILENAME_LENGTH]="C:\\";
    {
        TestDialog d(path);
        d.click(SGFSDLG_UPDIR);
        check(!strcmp(dlgpath,"Computer"),"Up from C root exposes Computer");
        check(d.click(SGFSDLG_OKAY)==Dialog::GUI_CONTINUE&&!d.pressedOk(),"Computer cannot be confirmed as image");
        check(d.choose("D:/"),"D drive is listed and selectable");
        check(!strcmp(dlgpath,"D:/"),"D root reached");
        check(d.choose("Images"),"folder on D can be entered");
        check(d.choose("boot.img"),"boot image on D can be selected");
        check(d.click(SGFSDLG_OKAY)==Dialog::GUI_CLOSE&&d.pressedOk(),"D image selection confirmed");
        check(!strcmp(path,"D:/Images/boot.img"),"returned path is on D with no C prefix");
    }
    strcpy(path,"C:/");
    {
        TestDialog d(path);d.click(SGFSDLG_ROOTDIR);
        check(!strcmp(dlgpath,"Computer")&&d.choose("D:/"),"PC button switches from C to D");
        d.click(SGFSDLG_UPDIR);check(!strcmp(dlgpath,"Computer"),"Up from D returns to drive list");
        strcpy(dlgfname,"D:\\Images\\boot.img");
        check(d.click(SGFSDLG_OKAY)==Dialog::GUI_CLOSE&&!strcmp(path,"D:/Images/boot.img"),"typed absolute Windows path is not prefixed with C");
    }
    strcpy(path,"D:\\Images\\boot.img");
    {
        TestDialog d(path);check(!strcmp(dlgpath,"D:/Images/"),"existing backslash path opens on correct drive");
        check(d.click(SGFSDLG_OKAY)==Dialog::GUI_CLOSE&&!strcmp(path,"D:/Images/boot.img"),"existing filename round trips");
    }
    strcpy(path,"D:/missing/folder/boot.img");
    {TestDialog d(path);check(!strcmp(dlgpath,"Computer"),"missing path falls back to drive list");d.click(SGFSDLG_CANCEL);check(!strcmp(path,"D:/missing/folder/boot.img"),"Cancel preserves original caller path");}
    strcpy(path,"E:/");
    {TestDialog d(path);check(!strcmp(dlgpath,"E:/"),"empty drive root remains accessible instead of falling back");d.click(SGFSDLG_OKAY);check(!strcmp(path,"E:/"),"empty drive can be chosen as a folder");}
    printf("Windows drive picker %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
