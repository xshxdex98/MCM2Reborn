/* Native regression checks use production AI/pad/radar code and menu helpers,
   with the game's objects and hardware responses supplied by small fixtures. */
#define DIRECTINPUT_VERSION 0x0800
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include "runtime.h"
#include <mmsystem.h>
#include <dinput.h>
#include "gui.h"
#include "../code/dev/dev.h"
#include "../code/dev/padstate.h"
static DWORD clock_ms = 1000;
static DWORD WINAPI test_time(void) { return clock_ms; }
#define timeGetTime test_time
#include "../code/mcm2/bikeai.c"
#include "../code/dev/pad.c"
#include "../code/dev/trackfilter.c"
#include "../code/mcm2/overlays.c"
#include "../code/mcm2/racebikes.c"

dev_switches dev_settings = {0};
reborn_options g_reborn = {0};
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void log_line(const char* fmt, ...) { (void) fmt; }
FARPROC api(const char* dll, const char* name) { return NULL; }
BOOL write_protected(void* to, const void* from, SIZE_T size) { return TRUE; }
float square_root(float x) { return sqrtf(x); }
float exp_minus(float x) { return expf(-x); }
void __fastcall game_FUN_00500690(BYTE* object) { }
void __fastcall game_orig_FUN_00414000(BYTE* self, void* unused, float dt) { FIELD(self,0x800,int)++; }
int __fastcall game_orig_vf_89_48F390(BYTE* self, void* unused, DWORD a) { return 0; }
int __fastcall game_orig_FUN_0041a770(BYTE* self, void* unused, DWORD a,DWORD b,DWORD c,DWORD d,DWORD e)
{ CHECK(a==1 && b==2 && c==3 && d==4 && e==5); return 7; }
void hud_overlays_drawn(void) { }
void leaders_watch(void) { }
int __fastcall game_orig_vf_14_51DFC0(BYTE* self) { return 1; }
int __fastcall game_orig_vf_14_51F000(BYTE* self) { return 1; }
int __fastcall game_orig_vf_14_521370(BYTE* self) { return 1; }
static int radar_seen;
void __fastcall game_orig_FUN_00521520(BYTE* self, void* unused, int kind) { radar_seen=FIELD(self,0x160,int); }

static BOOL connected[4];
static xinput_state hardware;
static DWORD WINAPI get_pad(DWORD user, xinput_state* state)
{ if (!connected[user]) return ERROR_DEVICE_NOT_CONNECTED; *state=hardware; return ERROR_SUCCESS; }
static void pad_next(void) { clock_ms += 1100; }
static void test_pad(void)
{
    g_loaded=TRUE; g_get=get_pad; g_index=-1; g_sony=FALSE;
    CHECK(!pad_connected()); CHECK(pad_menu_direction()==-1);
    connected[2]=TRUE; pad_next(); CHECK(pad_connected()); CHECK(g_index==2);
    hardware.buttons=0x2000; pad_next(); CHECK(pad_back_down()); CHECK(!pad_start_down());
    hardware.buttons=0x1000; pad_next(); CHECK(pad_accept_down());
    hardware.buttons=0x0009; pad_next(); CHECK(pad_menu_direction()==1);
    hardware.buttons=0x0008; pad_next(); CHECK(pad_menu_direction()==4);
    hardware.buttons=0x0100; pad_next(); CHECK(pad_menu_tab()==-1);
    hardware.buttons=0x0200; pad_next(); CHECK(pad_menu_tab()==1);
    hardware.buttons=0x0300; pad_next(); CHECK(pad_menu_tab()==0);
    hardware.buttons=0; hardware.left_x=-32768; hardware.left_y=-32768;
    pad_next(); CHECK(pad_menu_direction()==2);
    hardware.left_x=-30000; hardware.left_y=12000; pad_next(); CHECK(pad_menu_direction()==3);
    hardware.left_x=9000; hardware.left_y=26000; pad_next(); CHECK(pad_menu_direction()==1);
    hardware.left_x=3000; hardware.left_y=-4000; pad_next(); CHECK(pad_menu_direction()==0);
    hardware.left_x=hardware.left_y=0;
    connected[2]=FALSE; pad_next(); CHECK(!pad_connected()); CHECK(!pad_accept_down()); CHECK(!pad_back_down());
    connected[0]=TRUE; ZeroMemory(&hardware,sizeof hardware); pad_next(); CHECK(pad_connected()); CHECK(g_index==0);
    puts("PASS controller startup, unplug/reconnect, buttons, bumpers, D-pad and dominant stick axis");
}
static void test_sony(void)
{
    DIJOYSTATE joy; xinput_state state;
    ZeroMemory(&joy,sizeof joy); joy.lRx=joy.lRy=-32767; joy.rgdwPOV[0]=(DWORD)-1;
    sony_pad_convert(&joy,&state); CHECK(!state.buttons && !state.left_trigger && !state.right_trigger);
    joy.rgbButtons[2]=0x80; sony_pad_convert(&joy,&state); CHECK(state.buttons==0x2000);
    joy.rgbButtons[2]=0; joy.rgbButtons[1]=0x80; joy.rgdwPOV[0]=4500;
    joy.lX=32767; joy.lY=-32767; joy.lZ=-32767; joy.lRz=32767; joy.lRx=32767;
    sony_pad_convert(&joy,&state); CHECK((state.buttons&0x1009)==0x1009);
    CHECK(state.left_x==32767 && state.left_y==32767 && state.right_x==-32767 && state.right_y==-32767);
    CHECK(state.left_trigger==255 && state.right_trigger==0);
    puts("PASS native PlayStation Circle/Cross, diagonal D-pad, axes and independent triggers");
}
static void test_ai(void)
{
    static BYTE bikes[29][0x900]; int race,i,frame;
    dev_settings.fix_ai_rate=30;
    for(race=0;race<20;race++) {
        for(i=0;i<32;i++) ai_bike_of((BYTE*)(UINT_PTR)(0x10000+race*0x20000+i*16));
        CHECK(race_load_bikes(NULL,NULL,1,2,3,4,5)==7);
        for(i=0;i<32;i++) CHECK(!g_ai_bikes[i].bike);
        ZeroMemory(bikes,sizeof bikes);
        for(frame=0;frame<288;frame++) {
            clock_ms+=7;
            for(i=0;i<29;i++) ai_bike_step(bikes[i],NULL,1.0f/144);
        }
        for(i=0;i<29;i++) CHECK(FIELD(bikes[i],0x800,int)>=59);
    }
    puts("PASS 20 successive 30-rider events at 144 FPS: every AI takes steps");
}
static void test_radar(void)
{
    BYTE radar[0x300]={0}; int n;
    for(n=0;n<=32;n++) {
        FIELD(radar,0x160,int)=n; radar_update_markers(radar,NULL,2);
        CHECK(radar_seen==(n>11?11:n)); CHECK(FIELD(radar,0x160,int)==n);
    }
    puts("PASS radar update caps all 0..32 rider counts and restores the real count");
}
static void test_tracks(void)
{
    CHECK(track_is_stock(0,"QUARRY05")); CHECK(!track_is_stock(0,"QUARRY06"));
    CHECK(track_is_stock(1,"Baja01")); CHECK(track_is_stock(2,"NAT12"));
    CHECK(!track_is_stock(2,"NAT13")); CHECK(!track_is_stock(2,"MCM1_Belas"));
    CHECK(track_is_stock(3,"sx17")); CHECK(!track_is_stock(3,"sx18"));
    CHECK(track_is_stock(4,"tag03")); CHECK(track_is_stock(5,"AIRFIELD"));
    CHECK(!track_is_stock(5,"airfield_custom")); CHECK(!track_is_stock(3,"sx"));
    CHECK(!track_is_stock(2,"nat01_custom")); CHECK(!track_is_stock(6,"nat01"));
    puts("PASS stock/custom classification for every event and strict stem matching");
}

/* Menu integration fixtures: production helper code is included by the runner. */
#define DLG_PARENT 0x2C
#define DLG_SHOWN 0xBC
#define DLG_MODAL 0x148
#define CTL_RECT 0x2C
#define CTL_DESIGN 0x3C
#define CTL_TYPE 0x5C
#define CTL_DIALOG 0xB8
#define SLIDER_RANGE 0x200
#define COMBO_OPEN 0x208
#define COMBO_LIST 0x1FC
#define COMBO_LIST_OWNER 0x250
#define COMBO_BUTTON_OWNER 0x1F0
#define COMBO_SLIDER_OWNER 0x220
#define COMBO_BUTTON_VTABLE ((void**) 0x12340004)
#define COMBO_SLIDER_VTABLE ((void**) 0x12340008)
#define COMBO_LIST_VTABLE ((void**) 0x12340000)
#define UICONTROL_CLASS "UIControl"
#define RADIO_CLASS "UIRadioButton"
static BYTE dialog_fixture[0x8000], controls_fixture[6][0x400];
static BYTE* controls[6]; static BOOL binding; static int clicks, list_updates;
static void walk_begin(BYTE* walk,void* unused,void* root,int depth,const char* kind)
{ FIELD(walk,0,int)=0; FIELD(walk,4,BOOL)=!strcmp(kind,RADIO_CLASS); }
static BYTE* walk_next(BYTE* walk)
{
    int i;
    while((i=FIELD(walk,0,int)++)<6)
        if(!FIELD(walk,4,BOOL) || FIELD(controls[i],CTL_TYPE,int)==4) return controls[i];
    return NULL;
}
static int option_current_index(BYTE* self) { return FIELD(self,0x310,int); }
static void walk_end(BYTE* walk) { }
static BOOL takes_focus(BYTE* c) { return FIELD(c,0x6C,int)&&FIELD(c,0x70,int); }
BOOL pad_binding_active(void) { return binding; }
BOOL input_in_foreground(void) { return TRUE; }
BOOL pad_menus_active(void) { return TRUE; }
BYTE* __fastcall gui_manager_active_dialog(gui_manager* manager) { return dialog_fixture; }
int __fastcall gui_user_set_capture(gui_user* u,void* unused,BYTE* c,BYTE** old) { u->capture=u->focus=c; return 1; }
void __fastcall gui_user_apply_focus(gui_user* u,void* unused,BOOL force) { }
static int __fastcall can_focus(void* self) { return 1; }
static int __fastcall press(void* self,void* unused,DWORD a,DWORD b) { return 1; }
static int __fastcall release(void* self,void* unused,DWORD a,DWORD b) { clicks++; return 1; }
static int __fastcall stepped(void* self,void* unused,int* done) { list_updates++; return 1; }
static int list_selected(BYTE* self) { return FIELD(self,0x200,int); }
static int list_count(BYTE* self) { return FIELD(self,0x1EC,int); }
static int list_select_all(BYTE* self,void* unused,int n) { FIELD(self,0x200,int)=n; return 1; }
typedef int (__fastcall* list_stepped_slot)(void*,void*,int*);
typedef int (__fastcall* control_two)(void*,void*,DWORD,DWORD);
#define slider_value(s) FIELD(s,0x300,int)
#define slider_steps(s) FIELD(s,0x1F0,unsigned)
#define slider_set(s,v,r) (FIELD(s,0x300,int)=(v))
static int back_clicks;
static BYTE* back_button;
static void control_tell(BYTE* s,void* event,DWORD kind) { if(kind==EVENT_CLICK) { back_clicks++; back_button=s; } else list_updates++; }
static BYTE* dialog_find_control(BYTE* d,void* unused,const char* name,int type) {
    int i;
    for(i=0;i<6;i++) if(FIELD(controls[i],CTL_DIALOG,BYTE*)==d && !strcmp((char*)controls[i]+0xF4,name)) return controls[i];
    return NULL;
}
static void combo_open(BYTE* s,void* unused,BOOL open) { FIELD(s,COMBO_OPEN,BOOL)=open; }
#include "menu_under_test.inc"
BOOL pad_menu_back(void) { return pad_back_from_dialog(dialog_fixture); }
#define WORD_TOP "TOP"
#define WORD_BOTTOM "BOTTOM"
#define WORD_LEFT "LEFT"
#define WORD_RIGHT "RIGHT"
#define WORD_CENTER "CENTER"
#define game_FUN_0053b6e0 _stricmp
static const BYTE left_aligned_types[10] = {0,0,0,1,0,0,1,1,1,0};
#include "align_under_test.inc"
static void test_alignment(void) {
    unsigned type;
    for(type=1;type<=12;type++) {
        CHECK((reader_alignment(type,"LEFT","TOP")&7)==1);
        CHECK((reader_alignment(type,"CENTER","TOP")&7)==2);
        CHECK((reader_alignment(type,"RIGHT","TOP")&7)==4);
    }
    puts("PASS actual UI loader alignment for every control type, including lists and combos");
}
#define DLG_USER 0x34
static int marks, released_dc;
static RECT mark_box;
static HBRUSH mark_brush(COLORREF colour) { CHECK(colour==RGB(255,138,0));return (HBRUSH)1; }
static int mark_fill(HDC dc,const RECT* area,HBRUSH brush) { mark_box=*area;marks++;return 1; }
static HRESULT WINAPI mark_get_dc(void* surface,HDC* dc) {*dc=(HDC)1;return S_OK;}
static HRESULT WINAPI mark_release_dc(void* surface,HDC dc) {released_dc++;return S_OK;}
#define CreateSolidBrush mark_brush
#define FillRect mark_fill
#include "mark_under_test.inc"
#undef CreateSolidBrush
#undef FillRect
static void test_focus_mark(void) {
    BYTE screen[0x80]={0};void* surface[32]={0};void* table[32]={0};
    gui_user user={0};const RECT* box=&FIELD(controls[0],CTL_RECT,RECT);
    surface[0]=table;table[0x44/4]=(void*)mark_get_dc;table[0x68/4]=(void*)mark_release_dc;
    FIELD(screen,0x48,void*)=surface;FIELD(dialog_fixture,0x18,void*)=screen;
    FIELD(dialog_fixture,DLG_USER,gui_user*)=&user;
    FIELD(dialog_fixture,DLG_SHOWN,int)=1;FIELD(controls[0],0x70,int)=1;
    user.focus=controls[0];pad_navigation_active=TRUE;
    FIELD(controls[0],0x16C,void*)=FIELD(controls[0],0x170,void*)=NULL;
    marks=released_dc=0;dialog_pad_mark(dialog_fixture);
    CHECK(marks==1);CHECK(released_dc==1);
    CHECK(mark_box.left==box->left && mark_box.right==box->right && mark_box.top==box->bottom+2);
    CHECK(mark_box.bottom-mark_box.top>=2 && mark_box.bottom-mark_box.top<=4);
    FIELD(controls[0],0x16C,void*)=FIELD(controls[0],0x170,void*)=(void*)8;
    marks=0;dialog_pad_mark(dialog_fixture);CHECK(marks==1);
    FIELD(controls[0],0x170,void*)=(void*)16;
    marks=0;dialog_pad_mark(dialog_fixture);CHECK(!marks);
    FIELD(controls[0],0x170,void*)=NULL;
    pad_navigation_active=FALSE;marks=0;dialog_pad_mark(dialog_fixture);CHECK(!marks);
    puts("PASS focus mark only under controls without their own focus picture, and only for the pad");
}
#define MENU_MOVIE 0x7F58
static int video_paused, mpeg_paused;
void video_pause(void* movie,BOOL paused) { (void)movie; mpeg_paused+=(int)paused; }
#include "movie_under_test.inc"
static int __fastcall pause_video(void* self,void* unused,DWORD paused) { video_paused+=(int)paused; return 1; }
static void test_movie(void)
{
    BYTE dialog[0x8000]={0},movie[0x200]={0},video[4]={0}; void* table[17]={0};
    menu_movie_pause(dialog,1); CHECK(!mpeg_paused); FIELD(dialog,MENU_MOVIE,BYTE*)=movie;
    menu_movie_pause(dialog,1); CHECK(mpeg_paused==1); table[16]=(void*)pause_video; *(void***)video=table;
    FIELD(movie,0x1EC,void*)=video; menu_movie_pause(dialog,1); CHECK(video_paused==1);
    puts("PASS missing movie, missing decoder, the MPEG-1 copy and valid menu preview video");
}
static void test_menu(void)
{
    static gui_manager manager; static gui_user user; static gui_input mouse;
    static void* table[80]; int i; BYTE* focus;
    manager.user_count=1; manager.users[0]=&user; manager.mouse=&mouse;
    FIELD(dialog_fixture,DLG_SHOWN,int)=1;
    table[0x78/4]=(void*)can_focus; table[0xDC/4]=(void*)press;
    table[0xE0/4]=(void*)release; table[0x108/4]=(void*)stepped;
    for(i=0;i<6;i++) {
        controls[i]=controls_fixture[i]; *(void***)controls[i]=table;
        FIELD(controls[i],CTL_DIALOG,BYTE*)=dialog_fixture;
        FIELD(controls[i],0x6C,int)=FIELD(controls[i],0x70,int)=1;
        FIELD(controls[i],CTL_TYPE,int)=1;
        SetRect(&FIELD(controls[i],CTL_RECT,RECT),100,100+i*100,400,180+i*100);
        FIELD(controls[i],CTL_DESIGN,RECT)=FIELD(controls[i],CTL_RECT,RECT);
    }
    hardware.buttons=2; hardware.left_x=hardware.left_y=0; pad_next(); pad_menu_tick(&manager);
    CHECK(user.focus==controls[0]); CHECK(pad_navigation_active);
    pad_next(); pad_menu_tick(&manager); CHECK(user.focus==controls[1]);
    focus=user.focus; hardware.buttons=0; pad_next(); pad_menu_tick(&manager); CHECK(user.focus==focus);
    hardware.buttons=0x1000; pad_next(); pad_menu_tick(&manager); CHECK(clicks==1);
    pad_next(); pad_menu_tick(&manager); CHECK(clicks==1);
    binding=TRUE; hardware.buttons=2; pad_next(); pad_menu_tick(&manager); CHECK(user.focus==focus);
    binding=FALSE; FIELD(focus,CTL_TYPE,int)=3; FIELD(focus,0x1EC,int)=3;
    hardware.buttons=0; pad_next(); pad_menu_tick(&manager);
    hardware.buttons=2; pad_next(); pad_menu_tick(&manager); CHECK(FIELD(focus,0x200,int)==1); CHECK(list_updates==1);
    FIELD(focus,CTL_TYPE,int)=8; FIELD(focus,0x1F0,unsigned)=10;
    hardware.buttons=8; pad_next(); pad_menu_tick(&manager); CHECK(FIELD(focus,0x300,int)==1);
    FIELD(focus,CTL_TYPE,int)=6; FIELD(focus,COMBO_LIST,BYTE*)=controls[5];
    FIELD(controls[5],0x1EC,int)=4;
    hardware.buttons=0; pad_next(); pad_menu_tick(&manager);
    hardware.buttons=8; pad_next(); pad_menu_tick(&manager); CHECK(FIELD(controls[5],0x200,int)==1);
    FIELD(focus,COMBO_OPEN,BOOL)=TRUE;
    hardware.buttons=2; pad_next(); pad_menu_tick(&manager); CHECK(FIELD(controls[5],0x200,int)==2);
    CHECK(pad_close_dropdown(focus)); CHECK(!FIELD(focus,COMBO_OPEN,BOOL));
    CHECK(!pad_close_dropdown(focus));
    { BYTE entry[0x300]={0};
      *(void***)entry=COMBO_LIST_VTABLE; FIELD(entry,COMBO_LIST_OWNER,BYTE*)=focus;
      FIELD(focus,COMBO_OPEN,BOOL)=TRUE; CHECK(pad_close_dropdown(entry));
    }
    hardware.buttons=0; mouse.x=25; pad_next(); pad_menu_tick(&manager); CHECK(!pad_navigation_active);
    FIELD(controls[2],0x70,int)=0; CHECK(!pad_focusable(controls[2]));
    { static BYTE parent[0x8000];
      FIELD(parent,DLG_SHOWN,int)=1; FIELD(parent,0x25,BYTE)=4;
      FIELD(dialog_fixture,DLG_PARENT,BYTE*)=parent;
      FIELD(dialog_fixture,DLG_MODAL,int)=1; CHECK(pad_focusable(controls[0]));
      FIELD(dialog_fixture,DLG_MODAL,int)=0; CHECK(!pad_focusable(controls[0]));
      FIELD(dialog_fixture,DLG_PARENT,BYTE*)=NULL;
    }
    strcpy((char*)controls[0]+0xF4,"Back");
    CHECK(pad_back_from_dialog(dialog_fixture)); CHECK(back_clicks==1); CHECK(back_button==controls[0]);
    FIELD(controls[0],0x70,int)=0; CHECK(!pad_back_from_dialog(dialog_fixture));
    FIELD(controls[0],0x70,int)=1;
    hardware.buttons=0; pad_next(); pad_menu_tick(&manager);
    hardware.buttons=0x2000; pad_next(); pad_menu_tick(&manager); CHECK(back_clicks==2);
    pad_next(); pad_menu_tick(&manager); CHECK(back_clicks==2);
    ((char*)controls[0]+0xF4)[0]=0;
    FIELD(controls[1],CTL_TYPE,int)=1; FIELD(controls[1],0x1D8,DWORD)=1;
    CHECK(pad_back_from_dialog(dialog_fixture)); CHECK(back_button==controls[1]);
    FIELD(controls[1],0x1D8,DWORD)=0;
    hardware.buttons=0; pad_next(); pad_menu_tick(&manager);
    FIELD(dialog_fixture,DLG_SHOWN,int)=0; CHECK(!pad_focusable(controls[0]));
    puts("PASS controller focus without startup joystick, A activation/debounce, rebinding, lists, sliders and mouse handoff");

    FIELD(dialog_fixture,DLG_SHOWN,int)=1;
    for(i=0;i<6;i++) { FIELD(controls[i],CTL_TYPE,int)=1; FIELD(controls[i],COMBO_OPEN,BOOL)=FALSE; FIELD(controls[i],0x70,int)=1; }
    user.focus=NULL; clicks=0;
    hardware.buttons=0x1000; pad_next(); pad_menu_tick(&manager);
    CHECK(user.focus==controls[0]); CHECK(!clicks);
    user.focus=NULL; hardware.buttons=0; pad_next(); pad_menu_tick(&manager); CHECK(user.focus==controls[0]);
    hardware.buttons=1; pad_next(); pad_menu_tick(&manager); CHECK(user.focus==controls[5]);
    hardware.buttons=0; pad_next(); pad_menu_tick(&manager);
    hardware.buttons=2; pad_next(); pad_menu_tick(&manager); CHECK(user.focus==controls[0]);
    for(i=1;i<=3;i++) {
        FIELD(controls[i],CTL_TYPE,int)=4; FIELD(controls[i],0x310,int)=i==2;
        SetRect(&FIELD(controls[i],CTL_RECT,RECT),i*200,40,i*200+180,90);
    }
    hardware.buttons=0x0200; pad_next(); pad_menu_tick(&manager); CHECK(clicks==1); CHECK(user.focus==controls[3]);
    pad_next(); pad_menu_tick(&manager); CHECK(clicks==1);
    hardware.buttons=0x0100; pad_next(); pad_menu_tick(&manager); CHECK(clicks==2); CHECK(user.focus==controls[1]);
    FIELD(controls[2],0x310,int)=0; FIELD(controls[1],0x310,int)=1;
    hardware.buttons=0; pad_next(); pad_menu_tick(&manager);
    hardware.buttons=0x0100; pad_next(); pad_menu_tick(&manager); CHECK(clicks==2);
    focus=controls[4]; user.focus=focus; FIELD(focus,CTL_TYPE,int)=8; FIELD(focus,0x1F0,unsigned)=100;
    slider_value(focus)=0;
    hardware.buttons=8; pad_next(); pad_menu_tick(&manager); CHECK(slider_value(focus)==1);
    pad_next(); pad_menu_tick(&manager); CHECK(slider_value(focus)==6);
    hardware.buttons=0; pad_next(); pad_menu_tick(&manager);
    puts("PASS first control shown on a new screen, up/down wrap, LB/RB tabs and held slider speed-up");
}
int main(void)
{
    test_alignment(); test_ai(); test_radar(); test_tracks(); test_movie(); test_pad(); test_sony(); test_menu(); test_focus_mark();
    printf("%d native checks passed\n",checks); return 0;
}
