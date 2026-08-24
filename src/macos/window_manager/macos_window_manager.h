#ifndef MACOS_WINDOW_MANAGER_H
#define MACOS_WINDOW_MANAGER_H

///////////////////////////////////////////////////////////////////////////////
//~ brt: Includes

//- brt: make obj-c not bork. Thanks, Sankt N.
#define nil nil
#undef internal
#undef global
#define FileInfo FinderFileInfo
#define NS_SUPPRESS_MIN_MAX_ABS
# import <Cocoa/Cocoa.h>
# import <Metal/Metal.h>
# import <MetalKit/MetalKit.h>
# import <Carbon/Carbon.h>
#undef nil
#define internal      static
#define global        static
#undef FileInfo

///////////////////////////////////////////////////////////////////////////////
//~ brt: Window State

@interface MAC_WM_NSWindow : NSWindow<NSWindowDelegate>
{
  NSTimer *live_resize_timer;
}
@property (nonatomic) NSEventModifierFlags previous_flags;
@end


typedef struct MAC_WM_TitleBarClientArea MAC_WM_TitleBarClientArea;
struct MAC_WM_TitleBarClientArea
{
  MAC_WM_TitleBarClientArea *next;
  Rng2F32 rect;
};

typedef struct MAC_WM_Window MAC_WM_Window;
struct MAC_WM_Window
{
  MAC_WM_Window *next;
  MAC_WM_Window *prev;
  MAC_WM_NSWindow *nswindow;
  B32 first_paint_done;
  B32 custom_border;
  F32 custom_border_title_thickness;
  B32 dragging_window;
  Arena *paint_arena;
  MAC_WM_TitleBarClientArea *first_title_bar_client_area;
  MAC_WM_TitleBarClientArea *last_title_bar_client_area;
};

///////////////////////////////////////////////////////////////////////////////
//~ brt: State Bundle

typedef struct MAC_WM_State MAC_WM_State;
struct MAC_WM_State
{
  Arena *arena;
  MAC_WM_Window *first_window;
  MAC_WM_Window *last_window;
  MAC_WM_Window *free_window;
  B32 in_live_resize;
  WM_Cursor last_set_cursor;
  WM_SystemInfo gfx_info;
};

///////////////////////////////////////////////////////////////////////////////
//~ brt: Globals

global MAC_WM_State *mac_wm_state = 0;
global WM_EventList mac_wm_event_list = {0};
global Arena *mac_wm_event_arena = 0;

///////////////////////////////////////////////////////////////////////////////
//~ brt: Helpers

internal NSString *mac_wm_nsstring_from_string( String8 string );
internal WM_Window mac_wm_handle_from_window( MAC_WM_Window *window );
internal MAC_WM_Window *mac_wm_window_from_handle( WM_Window handle );
internal NSWindow *mac_wm_nswindow_from_window( MAC_WM_Window *window );
internal MAC_WM_Window *mac_wm_window_from_nswindow( NSWindow *window );
internal WM_Key mac_wm_os_key_from_vkey( U32 vkey );
internal void mac_wm_set_window_buttons_positions(MAC_WM_Window *window);

internal WM_Event *mac_wm_push_event( WM_EventKind kind, MAC_WM_Window *window );

internal void mac_wm_send_dummy_event( void );

#endif // MACOS_WINDOW_MANAGER_H
