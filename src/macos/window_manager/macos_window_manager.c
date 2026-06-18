////////////////////////////////////////////////////////////////////////////////////////////////////
//- brt: ChatGPT gave me this and it looks super promising if/when we need language support without
//      having to take in all of Cocoa's input parsing system
//
#if 0
- (void)keyDown:(NSEvent *)event {
    // 1. Get the keycode (hardware code)
    UInt16 keyCode = [event keyCode];

    // 2. Get current keyboard layout
    TISInputSourceRef layout = TISCopyCurrentKeyboardLayoutInputSource();
    CFDataRef layoutData = TISGetInputSourceProperty(layout, kTISPropertyUnicodeKeyLayoutData);
    const UCKeyboardLayout *keyboardLayout = (const UCKeyboardLayout *)CFDataGetBytePtr(layoutData);

    // 3. Translate keycode + modifiers → Unicode character
    UniChar chars[4];
    UniCharCount realLength = 0;
    UInt32 modifiers = [event modifierFlags] >> 16; // rough mapping

    UCKeyTranslate(keyboardLayout,
                   keyCode,
                   kUCKeyActionDown,
                   modifiers & 0xFF,
                   LMGetKbdType(),
                   kUCKeyTranslateNoDeadKeysBit,
                   NULL,
                   sizeof(chars) / sizeof(chars[0]),
                   &realLength,
                   chars);

    CFRelease(layout);

    // 4. Emit your custom WM_EventKind_Text if needed
    for (UInt32 i = 0; i < realLength; i++) {
        UniChar c = chars[i];
        if (c >= 32 && c != 127) {
            WM_Event *e = wm_push_event(WM_EventKind_Text, self);
            if ([event modifierFlags] & NSEventModifierFlagOption)
                e->modifiers |= WM_Modifier_Alt;
            e->character = c;
        }
    }

    // Optionally: also emit WM_EventKind_KeyDown for physical press
}
#endif

///////////////////////////////////////////////////////////////////////////////
//~ brt: Helpers

@implementation MAC_WM_NSWindow
- (void)flagsChanged:(NSEvent *)ns_event
{
  MAC_WM_Window *window = mac_wm_window_from_nswindow(ns_event.window);
  NSEventModifierFlags new_flags = [ns_event modifierFlags];
  NSEventModifierFlags old_flags = self.previous_flags;
  if ((new_flags & NSEventModifierFlagShift) != (old_flags & NSEventModifierFlagShift))
  {
    WM_Event *event = mac_wm_push_event(!!(new_flags&NSEventModifierFlagShift) ? WM_EventKind_Press : WM_EventKind_Release, window);
    event->key = WM_Key_Shift;
    if(event->key == WM_Key_Alt   && event->modifiers & WM_Modifier_Alt)   { event->modifiers &= ~WM_Modifier_Alt; }
    if(event->key == WM_Key_Ctrl  && event->modifiers & WM_Modifier_Ctrl)  { event->modifiers &= ~WM_Modifier_Ctrl; }
    if(event->key == WM_Key_Shift && event->modifiers & WM_Modifier_Shift) { event->modifiers &= ~WM_Modifier_Shift; }
    //if(event->key == WM_Key_Cmd && event->modifiers & WM_Modifier_Super) { event->modifiers &= ~WM_Modifier_Super; }
  }
  if ((new_flags & NSEventModifierFlagControl) != (old_flags & NSEventModifierFlagControl))
  {
    WM_Event *event = mac_wm_push_event(!!(new_flags&NSEventModifierFlagControl) ? WM_EventKind_Press : WM_EventKind_Release, window);
    event->key = WM_Key_Ctrl;
    if(event->key == WM_Key_Alt   && event->modifiers & WM_Modifier_Alt)   { event->modifiers &= ~WM_Modifier_Alt; }
    if(event->key == WM_Key_Ctrl  && event->modifiers & WM_Modifier_Ctrl)  { event->modifiers &= ~WM_Modifier_Ctrl; }
    if(event->key == WM_Key_Shift && event->modifiers & WM_Modifier_Shift) { event->modifiers &= ~WM_Modifier_Shift; }
    //if(event->key == WM_Key_Cmd && event->modifiers & WM_Modifier_Super) { event->modifiers &= ~WM_Modifier_Super; }
  }
  if ((new_flags & NSEventModifierFlagOption) != (old_flags & NSEventModifierFlagOption))
  {
    WM_Event *event = mac_wm_push_event(!!(new_flags&NSEventModifierFlagOption) ? WM_EventKind_Press : WM_EventKind_Release, window);
    event->key = WM_Key_Alt;
    if(event->key == WM_Key_Alt   && event->modifiers & WM_Modifier_Alt)   { event->modifiers &= ~WM_Modifier_Alt; }
    if(event->key == WM_Key_Ctrl  && event->modifiers & WM_Modifier_Ctrl)  { event->modifiers &= ~WM_Modifier_Ctrl; }
    if(event->key == WM_Key_Shift && event->modifiers & WM_Modifier_Shift) { event->modifiers &= ~WM_Modifier_Shift; }
    //if(event->key == WM_Key_Cmd && event->modifiers & WM_Modifier_Super) { event->modifiers &= ~WM_Modifier_Super; }
  }
#if 0
  if ((new_flags & NSEventModifierFlagCommand) != (old_flags & NSEventModifierFlagCommand))
  {
    WM_Event *event = mac_wm_push_event(!!(new_flags&NSEventModifierFlagCommand) ? WM_EventKind_Press : WM_EventKind_Release, window);
    event->key = WM_Key_Cmd;
    if(event->key == WM_Key_Alt   && event->modifiers & WM_Modifier_Alt)   { event->modifiers &= ~WM_Modifier_Alt; }
    if(event->key == WM_Key_Ctrl  && event->modifiers & WM_Modifier_Ctrl)  { event->modifiers &= ~WM_Modifier_Ctrl; }
    if(event->key == WM_Key_Shift && event->modifiers & WM_Modifier_Shift) { event->modifiers &= ~WM_Modifier_Shift; }
    if(event->key == WM_Key_Cmd && event->modifiers & WM_Modifier_Super) { event->modifiers &= ~WM_Modifier_Super; }
  }
#endif
  if ((new_flags & NSEventModifierFlagCapsLock) != (old_flags & NSEventModifierFlagCapsLock))
  {
    WM_Event *event = mac_wm_push_event(!!(new_flags&NSEventModifierFlagCapsLock) ? WM_EventKind_Press : WM_EventKind_Release, window);
    event->key = WM_Key_CapsLock;
    if(event->key == WM_Key_Alt   && event->modifiers & WM_Modifier_Alt)   { event->modifiers &= ~WM_Modifier_Alt; }
    if(event->key == WM_Key_Ctrl  && event->modifiers & WM_Modifier_Ctrl)  { event->modifiers &= ~WM_Modifier_Ctrl; }
    if(event->key == WM_Key_Shift && event->modifiers & WM_Modifier_Shift) { event->modifiers &= ~WM_Modifier_Shift; }
    //if(event->key == WM_Key_Cmd && event->modifiers & WM_Modifier_Super) { event->modifiers &= ~WM_Modifier_Super; }
  }
  self.previous_flags = new_flags;
  [super flagsChanged:ns_event];
}

- (BOOL)allowsConcurrentViewDrawing {
  return YES;
}

- (BOOL)inLiveResize {
  return YES;
}

-(void)windowWillStartLiveResize:(NSNotification *)notification
{
  mac_wm_state->in_live_resize = 1;
}

-(void)windowDidEndLiveResize:(NSNotification *)notification
{
  mac_wm_state->in_live_resize = 0;
}

- (void)windowWillClose:(NSNotification *)notification
{
  NSWindow *ns_window = notification.object;
  MAC_WM_Window *window = mac_wm_window_from_nswindow(ns_window);
  mac_wm_push_event(WM_EventKind_WindowClose, window);
}

- (NSDragOperation) draggingEntered:(id<NSDraggingInfo>) sender
{
  NSDragOperation result = NSDragOperationCopy;
  return result;
}

- (BOOL) performDragOperation:(id<NSDraggingInfo>) sender
{
  NSPasteboard *pboard = [sender draggingPasteboard];
  NSWindow *ns_window = [sender draggingDestinationWindow];
  MAC_WM_Window *window = mac_wm_window_from_nswindow(ns_window);
  NSArray *types = pboard.types;
  if ([types containsObject:NSPasteboardTypeFileURL])
  {
    NSArray *classes = @[NSURL.class];
    NSDictionary *options = @{NSPasteboardURLReadingFileURLsOnlyKey:@YES};
    NSArray *file_urls = [pboard readObjectsForClasses:classes
                                               options:options];
    NSUInteger count = file_urls.count;
    if (count > 0)
    {
      WM_Event *event = mac_wm_push_event(WM_EventKind_FileDrop, window);
      event->pos = wm_mouse_from_window(event->window);
      for EachIndex(idx, count)
      {
        NSURL *url = file_urls[idx];
        char *cstr = url.path.UTF8String;
        struct stat path_stat;
        stat(cstr, &path_stat);
        B32 is_dir = S_ISDIR(path_stat.st_mode);
        if (!is_dir)
        {
          String8 str8_path = str8_copy(mac_wm_event_arena, str8_cstring(cstr));
          str8_list_push(mac_wm_event_arena, &event->strings, str8_path);
        }
      }

      mac_wm_send_dummy_event();
    }
  }

  return YES;
}

@end

internal NSString *
mac_wm_nsstring_from_string( String8 string )
{
  NSString *result = [[NSString alloc] initWithBytes:string.str
                                              length:string.size
                                            encoding:NSUTF8StringEncoding];
  return result;
}

internal WM_Window
mac_wm_handle_from_window( MAC_WM_Window *window )
{
  WM_Window handle = { (U64) window };
  return handle;
}

internal MAC_WM_Window *
mac_wm_window_from_handle( WM_Window handle )
{
  MAC_WM_Window *window = (MAC_WM_Window *) handle.u64[0];
  return window;
}

internal NSWindow *
mac_wm_nswindow_from_window( MAC_WM_Window *window )
{
  return window->nswindow;
}

internal MAC_WM_Window *
mac_wm_window_from_nswindow( NSWindow *window )
{
  MAC_WM_Window *result = 0;
  for (MAC_WM_Window *w = mac_wm_state->first_window; w; w = w->next)
  {
    if (w->nswindow == window)
    {
      result = w;
      break;
    }
  }

  return result;
} 

internal Rng2F32
mac_wm_rng2f32_from_nsrect( NSRect rect )
{
  Rng2F32 r = {0};
  r.x0 = (F32)(rect.origin.x);
  r.x1 = (F32)(rect.origin.x + rect.size.width);
  r.y0 = (F32)(rect.origin.y);
  r.y1 = (F32)(rect.origin.y + rect.size.height);
  return r;
}

internal void
mac_wm_move_window_button( NSWindow *window, NSWindowButton kind, F32 title_height )
{
  NSButton *button = [window standardWindowButton:kind];
  NSRect frame = button.frame;
  NSPoint new_origin = frame.origin;
  F32 size = frame.size.width;
  F32 base_offset_x = (size + 6.0) * kind;
  F32 base_offset_y = size + 5.2;
  F32 offset = 0.18 * title_height;
  new_origin.x = base_offset_x + offset;
  new_origin.y = base_offset_y - offset;
  [button setFrameOrigin:new_origin];
}

internal void
mac_wm_set_window_buttons_positions( MAC_WM_Window *window )
{
  F32 title_height = window->custom_border_title_thickness;
  mac_wm_move_window_button(window->nswindow, NSWindowCloseButton, title_height);
  mac_wm_move_window_button(window->nswindow, NSWindowMiniaturizeButton, title_height);
  mac_wm_move_window_button(window->nswindow, NSWindowZoomButton, title_height);
}

internal WM_Key
mac_wm_os_key_from_vkey( U32 vkey )
{
  local_persist B32 first = 1;
  local_persist WM_Key key_table[512];
  if (first)
  {
    first = 0;
    MemoryZeroArray(key_table);
    key_table[kVK_ANSI_A] = WM_Key_A;
    key_table[kVK_ANSI_B] = WM_Key_B;
    key_table[kVK_ANSI_C] = WM_Key_C;
    key_table[kVK_ANSI_D] = WM_Key_D;
    key_table[kVK_ANSI_E] = WM_Key_E;
    key_table[kVK_ANSI_F] = WM_Key_F;
    key_table[kVK_ANSI_G] = WM_Key_G;
    key_table[kVK_ANSI_H] = WM_Key_H;
    key_table[kVK_ANSI_I] = WM_Key_I;
    key_table[kVK_ANSI_J] = WM_Key_J;
    key_table[kVK_ANSI_K] = WM_Key_K;
    key_table[kVK_ANSI_L] = WM_Key_L;
    key_table[kVK_ANSI_M] = WM_Key_M;
    key_table[kVK_ANSI_N] = WM_Key_N;
    key_table[kVK_ANSI_O] = WM_Key_O;
    key_table[kVK_ANSI_P] = WM_Key_P;
    key_table[kVK_ANSI_Q] = WM_Key_Q;
    key_table[kVK_ANSI_R] = WM_Key_R;
    key_table[kVK_ANSI_S] = WM_Key_S;
    key_table[kVK_ANSI_T] = WM_Key_T;
    key_table[kVK_ANSI_U] = WM_Key_U;
    key_table[kVK_ANSI_V] = WM_Key_V;
    key_table[kVK_ANSI_W] = WM_Key_W;
    key_table[kVK_ANSI_X] = WM_Key_X;
    key_table[kVK_ANSI_Y] = WM_Key_Y;
    key_table[kVK_ANSI_Z] = WM_Key_Z;
    key_table[kVK_ANSI_0] = WM_Key_0;
    key_table[kVK_ANSI_1] = WM_Key_1;
    key_table[kVK_ANSI_2] = WM_Key_2;
    key_table[kVK_ANSI_3] = WM_Key_3;
    key_table[kVK_ANSI_4] = WM_Key_4;
    key_table[kVK_ANSI_5] = WM_Key_5;
    key_table[kVK_ANSI_6] = WM_Key_6;
    key_table[kVK_ANSI_7] = WM_Key_7;
    key_table[kVK_ANSI_8] = WM_Key_8;
    key_table[kVK_ANSI_9] = WM_Key_9;
    key_table[kVK_ANSI_Keypad0] = WM_Key_Num0;
    key_table[kVK_ANSI_Keypad1] = WM_Key_Num1;
    key_table[kVK_ANSI_Keypad2] = WM_Key_Num2;
    key_table[kVK_ANSI_Keypad3] = WM_Key_Num3;
    key_table[kVK_ANSI_Keypad4] = WM_Key_Num4;
    key_table[kVK_ANSI_Keypad5] = WM_Key_Num5;
    key_table[kVK_ANSI_Keypad6] = WM_Key_Num6;
    key_table[kVK_ANSI_Keypad7] = WM_Key_Num7;
    key_table[kVK_ANSI_Keypad8] = WM_Key_Num8;
    key_table[kVK_ANSI_Keypad9] = WM_Key_Num9;
    key_table[kVK_F1] = WM_Key_F1;
    key_table[kVK_F2] = WM_Key_F2;
    key_table[kVK_F3] = WM_Key_F3;
    key_table[kVK_F4] = WM_Key_F4;
    key_table[kVK_F5] = WM_Key_F5;
    key_table[kVK_F6] = WM_Key_F6;
    key_table[kVK_F7] = WM_Key_F7;
    key_table[kVK_F8] = WM_Key_F8;
    key_table[kVK_F9] = WM_Key_F9;
    key_table[kVK_F10] = WM_Key_F10;
    key_table[kVK_F11] = WM_Key_F11;
    key_table[kVK_F12] = WM_Key_F12;
    key_table[kVK_F13] = WM_Key_F13;
    key_table[kVK_F14] = WM_Key_F14;
    key_table[kVK_F15] = WM_Key_F15;
    key_table[kVK_F16] = WM_Key_F16;
    key_table[kVK_F17] = WM_Key_F17;
    key_table[kVK_F18] = WM_Key_F18;
    key_table[kVK_F19] = WM_Key_F19;
    key_table[kVK_F20] = WM_Key_F20;

    // --- Punctuation and symbols ---
    key_table[kVK_Space]           = WM_Key_Space;
    key_table[kVK_ANSI_Grave]      = WM_Key_Tick;          // ` (same as VK_OEM_3)
    key_table[kVK_ANSI_Minus]      = WM_Key_Minus;         // -
    key_table[kVK_ANSI_Equal]      = WM_Key_Equal;         // =
    key_table[kVK_ANSI_LeftBracket]= WM_Key_LeftBracket;   // [
    key_table[kVK_ANSI_RightBracket]=WM_Key_RightBracket;  // ]
    key_table[kVK_ANSI_Semicolon]  = WM_Key_Semicolon;     // ;
    key_table[kVK_ANSI_Quote]      = WM_Key_Quote;         // '
    key_table[kVK_ANSI_Comma]      = WM_Key_Comma;         // ,
    key_table[kVK_ANSI_Period]     = WM_Key_Period;        // .
    key_table[kVK_ANSI_Slash]      = WM_Key_Slash;         // /
    key_table[kVK_ANSI_Backslash]  = WM_Key_BackSlash;     // \

    // --- Control / function keys ---
    key_table[kVK_Tab]             = WM_Key_Tab;
    key_table[kVK_Escape]          = WM_Key_Esc;
    key_table[kVK_Return]          = WM_Key_Return;
    key_table[kVK_Delete]          = WM_Key_Backspace;     // Backspace (Delete on Mac)
    key_table[kVK_ForwardDelete]   = WM_Key_Delete;        // Forward delete (fn+Delete)
    key_table[kVK_Help]            = WM_Key_Insert;        // Insert = Help key on extended keyboard

    key_table[kVK_PageUp]          = WM_Key_PageUp;
    key_table[kVK_PageDown]        = WM_Key_PageDown;
    key_table[kVK_Home]            = WM_Key_Home;
    key_table[kVK_End]             = WM_Key_End;

    key_table[kVK_UpArrow]         = WM_Key_Up;
    key_table[kVK_LeftArrow]       = WM_Key_Left;
    key_table[kVK_DownArrow]       = WM_Key_Down;
    key_table[kVK_RightArrow]      = WM_Key_Right;

    // --- Locks and modifiers ---
    key_table[kVK_CapsLock]        = WM_Key_CapsLock;
    key_table[kVK_Function]        = WM_Key_Menu;          // “Fn” often used as Menu-equivalent
    key_table[kVK_Command]         = WM_Key_Ctrl;          // ⌘ as Ctrl analog
    key_table[kVK_Control]         = WM_Key_Ctrl;
    key_table[kVK_RightControl]    = WM_Key_Ctrl;
    key_table[kVK_Shift]           = WM_Key_Shift;
    key_table[kVK_RightShift]      = WM_Key_Shift;
    key_table[kVK_Option]          = WM_Key_Alt;
    key_table[kVK_RightOption]     = WM_Key_Alt;

    // --- Numeric keypad ---
    key_table[kVK_ANSI_KeypadDivide]   = WM_Key_NumSlash;
    key_table[kVK_ANSI_KeypadMultiply] = WM_Key_NumStar;
    key_table[kVK_ANSI_KeypadMinus]    = WM_Key_NumMinus;
    key_table[kVK_ANSI_KeypadPlus]     = WM_Key_NumPlus;
    key_table[kVK_ANSI_KeypadDecimal]  = WM_Key_NumPeriod;
  }
  return key_table[vkey];
}

///////////////////////////////////////////////////////////////////////////////
//~ brt: @wm_hooks Main Initialization API (Implemented Per-OS)

internal void
wm_init(void)
{

  //- brt: initialize basics
  Arena *arena = arena_alloc();
  mac_wm_state = push_array(arena, MAC_WM_State, 1);
  mac_wm_state->arena = arena;
  mac_wm_state->gfx_info.default_refresh_rate = 60.f;
  mac_wm_state->gfx_info.double_click_time = (F32)[NSEvent doubleClickInterval];
  mac_wm_state->gfx_info.caret_blink_time = 0.5f;

  //- brt: fill out menu bar
  @autoreleasepool
  {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSMenu *menu_bar = [[NSMenu alloc] init];
    [NSApp setMainMenu:menu_bar];

    NSMenuItem *app_menu_item = [[NSMenuItem alloc] init];
    [menu_bar addItem:app_menu_item];

    NSMenu *app_menu = [[NSMenu alloc] init];
    [app_menu_item setSubmenu:app_menu];

    NSMenuItem *quit_menu_item = [[NSMenuItem alloc] initWithTitle:@"Quit "BUILD_TITLE
                                                            action:@selector(terminate:)
                                                     keyEquivalent:@"q"];
    [app_menu addItem:quit_menu_item];
    [NSApp finishLaunching]; 
  }
}

///////////////////////////////////////////////////////////////////////////////
//~ brt:  @wm_hooks Graphics System Info (Implemented Per-OS)

internal WM_SystemInfo *
wm_get_system_info(void)
{
  return &mac_wm_state->gfx_info;
}

///////////////////////////////////////////////////////////////////////////////
//~ brt:  @wm_hooks Clipboards (Implemented Per-OS)

internal void
wm_set_clipboard_text(String8 string)
{
  NSString *ns_string = mac_wm_nsstring_from_string(string);
  NSPasteboard *pboard = [NSPasteboard generalPasteboard];
  [pboard clearContents];
  [pboard setString:ns_string
            forType:NSPasteboardTypeString];
}

internal String8
wm_get_clipboard_text(Arena *arena)
{
  NSPasteboard *pboard = [NSPasteboard generalPasteboard];
  NSString *ns_string = [pboard stringForType:NSPasteboardTypeString];
  char *cstr = ns_string.UTF8String;
  String8 result = str8_copy(arena, str8_cstring(cstr));
  [ns_string release];
  return result;
}

///////////////////////////////////////////////////////////////////////////////
//~ brt:  @wm_hooks Windows (Implemented Per-OS)

internal WM_Window
wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title)
{
  B32 custom_border = !!(flags & WM_WindowFlag_CustomBorder);
  B32 use_default_position = !!(flags & WM_WindowFlag_UseDefaultPosition);
  Vec2F32 pos = rect.p0;
  Vec2F32 dim = dim_2f32(rect);

  //- brt: allocate window
  MAC_WM_Window *w = mac_wm_state->free_window;
  if (w)
  {
    SLLStackPop(mac_wm_state->free_window);
  }
  else
  {
    w = push_array_no_zero(mac_wm_state->arena, MAC_WM_Window, 1);
  }
  MemoryZeroStruct(w);
  DLLPushBack(mac_wm_state->first_window, mac_wm_state->last_window, w);


  //- brt: create nswindow
  @autoreleasepool
  {
    F32 scale = 1.0/NSScreen.mainScreen.backingScaleFactor;
    NSRect rect = NSMakeRect(use_default_position ? 0 : pos.x*scale,
                             use_default_position ? 0 : pos.y*scale,
                             dim.x*scale,
                             dim.y*scale);
    NSUInteger mask = NSWindowStyleMaskTitled |
                      NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable |
                      NSWindowStyleMaskFullSizeContentView |
                      NSWindowStyleMaskResizable;

    MAC_WM_NSWindow *ns_window = [[MAC_WM_NSWindow alloc] initWithContentRect:rect
                                                   styleMask:mask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    w->nswindow = ns_window;

    NSString *ns_title = mac_wm_nsstring_from_string(title);
    [ns_window setTitle:ns_title];

    //- brt: custom border
    if (flags & WM_WindowFlag_CustomBorder)
    {
      [ns_window setTitleVisibility:NSWindowTitleHidden];
      [ns_window setTitlebarAppearsTransparent:YES];
      [ns_window setMovable:NO];
      //[ns_window center];
      w->custom_border = 1;
      w->paint_arena = arena_alloc();
#if 1
      [[ns_window standardWindowButton:NSWindowCloseButton] setHidden:YES];
      [[ns_window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
      [[ns_window standardWindowButton:NSWindowZoomButton] setHidden:YES];
#endif
      //mac_wm_set_window_buttons_positions(w);
    }

    [ns_window registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    ns_window.delegate = ns_window;
    //[ns_window setAcceptsMouseMovedEvents:YES];
  }

  //- brt: convert to handle & return
  WM_Window handle = {(U64)w};
  return handle;
}

internal void
wm_window_close(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  [w->nswindow performClose:0];
  w->nswindow = 0;
}

internal void
wm_window_set_title(WM_Window handle, String8 title)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  Temp scratch = scratch_begin(0, 0);
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  [w->nswindow setTitle:mac_wm_nsstring_from_string(title)];
  scratch_end(scratch);
}

internal void
wm_window_first_paint(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  w->first_paint_done = 1;
  [w->nswindow makeKeyAndOrderFront:0];
}

internal void
wm_window_focus(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return; }
  [NSApp activateIgnoringOtherApps:YES];
  [ns_window makeKeyAndOrderFront:0];
}

internal B32
wm_window_is_focused(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return 0;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return 0; }
  NSWindow *key_window = [NSApp keyWindow];
  return (key_window == ns_window);
}

internal B32
wm_window_is_fullscreen(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return 0;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return 0; }

  return (([ns_window styleMask] & NSWindowStyleMaskFullScreen) != 0);
}

internal void
wm_window_set_fullscreen(WM_Window handle, B32 fullscreen)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return; }

  BOOL isCurrentlyFullscreen = (([ns_window styleMask] & NSWindowStyleMaskFullScreen) != 0);
  if (fullscreen && !isCurrentlyFullscreen)
  {
    [ns_window toggleFullScreen:0];
  }
  else if (!fullscreen && isCurrentlyFullscreen)
  {
    [ns_window toggleFullScreen:0];
  }
}

internal B32
wm_window_is_maximized(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return 0;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return 0; }

  return [ns_window isZoomed];
}

internal void
wm_window_set_maximized(WM_Window handle, B32 maximized)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return; }

  BOOL isZoomed = [ns_window isZoomed];
  if (maximized && !isZoomed)
  {
    [ns_window zoom:0];
  }
  else if (!maximized && isZoomed)
  {
    [ns_window zoom:0];
  }
}

internal B32
wm_window_is_minimized(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return 0;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return 0; }

  return [ns_window isMiniaturized];
}

internal void
wm_window_set_minimized(WM_Window handle, B32 minimized)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return; }

  if (minimized && ![ns_window isMiniaturized])
  {
    [ns_window miniaturize:0];
  }
  else if (!minimized && [ns_window isMiniaturized])
  {
    [ns_window deminiaturize:0];
  }
}

internal void
wm_window_bring_to_front(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  NSWindow *ns_window = (__bridge NSWindow *)w->nswindow;
  if (!ns_window) { return; }

  // Activate the app if inactive, then bring the window forward
  if (![NSApp isActive])
  {
    [NSApp activateIgnoringOtherApps:YES];
  }

  [ns_window orderFrontRegardless];
}

internal void
wm_window_set_monitor(WM_Window handle, WM_Monitor monitor)
{
  MAC_WM_Window *w = (MAC_WM_Window *)handle.u64[0];
  CGDirectDisplayID display_id = (CGDirectDisplayID)monitor.u64[0];
  NSScreen *target_screen = 0;
  for (NSScreen *screen in NSScreen.screens)
  {
    NSNumber *num = screen.deviceDescription[@"NSScreenNumber"];
    if (num && num.unsignedIntValue == display_id)
    {
      target_screen = screen;
      break;
    }
  }
  if (target_screen != 0)
  {
    NSRect current_frame = w->nswindow.frame;
    NSSize window_size = current_frame.size;
    NSRect work_frame = target_screen.visibleFrame;
    CGFloat new_x = NSMidX(work_frame) - window_size.width * 0.5;
    CGFloat new_y = NSMidY(work_frame) - window_size.height * 0.5;
    NSRect new_frame = NSMakeRect(new_x, new_y, window_size.width, window_size.height);
    [w->nswindow setFrame:new_frame display:YES animate:NO];
  }
}

internal void
wm_window_clear_custom_border_data(WM_Window handle)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if (window->custom_border)
  {
    arena_clear(window->paint_arena);
    window->first_title_bar_client_area = window->last_title_bar_client_area = 0;
    window->custom_border_title_thickness = 0;
  }
}

internal void
wm_window_push_custom_title_bar(WM_Window handle, F32 thickness)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if (window->custom_border_title_thickness != thickness)
  {
    window->custom_border_title_thickness = thickness;
    mac_wm_set_window_buttons_positions(window);
  }
}

internal void
wm_window_push_custom_edges(WM_Window handle, F32 thickness)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  // TODO(rjf)
}

internal void
wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect)
{
  if(MemoryIsZeroStruct(&handle)) {return;}
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if (window->custom_border)
  {
    MAC_WM_TitleBarClientArea *area = push_array(window->paint_arena, MAC_WM_TitleBarClientArea, 1);
    area->rect = rect;
    SLLQueuePush(window->first_title_bar_client_area, window->last_title_bar_client_area, area);
  }
}

internal Rng2F32
wm_rect_from_window(WM_Window handle)
{
  Rng2F32 r = {0};
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if (window)
  {
    CGFloat scale = window->nswindow.screen.backingScaleFactor;
    NSRect rect_pt = window->nswindow.frame;
    NSRect rect_px = NSMakeRect(rect_pt.origin.x * scale,
                                rect_pt.origin.y * scale,
                                rect_pt.size.width * scale,
                                rect_pt.size.height * scale);
    r = mac_wm_rng2f32_from_nsrect(rect_px);
  }
  return r;
}

internal Rng2F32
wm_client_rect_from_window(WM_Window handle)
{
  Rng2F32 r = {0};
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if (window)
  {
    CGFloat scale = window->nswindow.screen.backingScaleFactor;
    NSRect rect_pt = window->nswindow.contentView.frame;
    NSRect rect_px = NSMakeRect(rect_pt.origin.x * scale,
                                rect_pt.origin.y * scale,
                                rect_pt.size.width * scale,
                                rect_pt.size.height * scale);
    r = mac_wm_rng2f32_from_nsrect(rect_px);
  }
  return r;
}

internal F32
wm_dpi_from_window(WM_Window handle)
{
  F32 result = 96.f;
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  NSWindow *nswindow = mac_wm_nswindow_from_window(window);
  if (nswindow != 0)
  {
    //- brt: map scale factor -> win32 DPI
    result = nswindow.screen.backingScaleFactor * 96.f;
  }
  return result;
}

////////////////////////////////
//~ rjf: @per_os_impl External Windows (Implemented Per-OS)

internal WM_ExtWindow
wm_focused_external_window(void)
{
  WM_ExtWindow result = {0};
  // TODO(rjf)
  return result;
}

internal void
wm_focus_external_window(WM_ExtWindow handle)
{
  // TODO(rjf)
}

////////////////////////////////
//~ rjf: @wm_hooks Monitors (Implemented Per-OS)

internal WM_MonitorArray
wm_push_monitors_array(Arena *arena)
{
  WM_MonitorArray array = {0};

  //- brt: count displays
  U32 displays_count;
  {
    CGGetActiveDisplayList(1024, NULL, &displays_count);
  }

  //- brt: copy displays
  {
    Temp scratch = scratch_begin(&arena, 1);

    CGDirectDisplayID *cg_displays = push_array_no_zero(scratch.arena, CGDirectDisplayID, displays_count);
    CGGetActiveDisplayList(displays_count, cg_displays, &displays_count);

    //- brt: CGDirectDisplayID -> WM_Window
    array.count = displays_count;
    array.v = push_array(arena, WM_Monitor, array.count);
    for (U64 idx = 0; idx < array.count; idx++)
    {
      array.v[idx].u64[0] = (U64)cg_displays[idx];
    }
    scratch_end(scratch);
  }
  return array;
}

StaticAssert(sizeof(CGDirectDisplayID) <= sizeof(U64), mac_wm_direct_display_id_size_check);

internal WM_Monitor
wm_primary_monitor(void)
{
  WM_Monitor result = {0};
  CGDirectDisplayID disp_id = CGMainDisplayID();
  result.u64[0] = (U64)disp_id;
  return result;
}

internal WM_Monitor
wm_monitor_from_window(WM_Window handle)
{
  WM_Monitor result = {0};

  NSWindow *nswindow = 0;
  {
    MAC_WM_Window *window = mac_wm_window_from_handle(handle);
    nswindow = mac_wm_nswindow_from_window(window);
  }
  if (nswindow != 0)
  {
    NSNumber *screen_id = nswindow.screen.deviceDescription[@"NSScreenNumber"];
    if (screen_id != 0)
    {
      result.u64[0] = (U64)[screen_id unsignedIntValue];
    }
  }

  return result;
}

internal String8
wm_name_from_monitor(Arena *arena, WM_Monitor monitor)
{
  String8 result = {0};
  CGDirectDisplayID disp_id = (CGDirectDisplayID)monitor.u64[0];
  CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(disp_id);
  CFStringRef uuid_str_cf = CFUUIDCreateString(0, uuid);
  const char *uuid_str_cstr = CFStringGetCStringPtr(uuid_str_cf, kCFStringEncodingUTF8);
  if (uuid_str_cstr != 0)
  {
    result = str8_copy(arena, str8_cstring(uuid_str_cstr));
  }
  CFRelease(uuid_str_cf);
  CFRelease(uuid);
  return result;
}

internal Vec2F32
wm_dim_from_monitor(WM_Monitor monitor)
{
  // TODO(rjf)
  return v2f32(0, 0);
}

internal F32
wm_dpi_from_monitor(WM_Monitor monitor)
{
  // TODO(rjf)
  return 96.f;
}

////////////////////////////////
//~ rjf: @wm_hooks Events (Implemented Per-OS)

internal WM_Event *
mac_wm_push_event( WM_EventKind kind, MAC_WM_Window *window )
{
  WM_Event *result = wm_event_list_push_new(mac_wm_event_arena, &mac_wm_event_list, kind);
  result->window = mac_wm_handle_from_window(window);
  result->modifiers = wm_get_modifiers();
  return result;
}

internal void
mac_wm_send_dummy_event(void)
{
  NSPoint location = {0, 0};
  NSEvent *dummy = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:location
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:0
                                       context:0
                                       subtype:0
                                         data1:0
                                         data2:0];
  dispatch_async(dispatch_get_main_queue(),
  ^{
    [NSApp postEvent:dummy atStart:NO];
  });
}

internal void
wm_send_wakeup_event(void)
{
  NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:NSMakePoint(0, 0)
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:0
                                       context:0
                                       subtype:0x401
                                         data1:0
                                         data2:0];
  dispatch_async(dispatch_get_main_queue(),
  ^{
    [NSApp postEvent:event atStart:NO];
  });
}

internal WM_EventList
wm_get_events(Arena *arena, B32 wait)
{
  mac_wm_event_arena = arena;
  MemoryZeroStruct(&mac_wm_event_list);

  if (!mac_wm_state->in_live_resize) @autoreleasepool
  {
    NSDate *deadline = wait ? [NSDate distantFuture] : [NSDate distantPast];
    NSEvent *ns_event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:deadline
                                              inMode:NSEventTrackingRunLoopMode
                                            dequeue:YES];
    for (;ns_event;)
    {
      B32 should_send = 1;

      MAC_WM_Window *window = mac_wm_window_from_nswindow(ns_event.window);
      WM_Window window_handle = mac_wm_handle_from_window(window);
      B32 release = 0;

      switch (ns_event.type)
      {
        //- brt: wakeup event
        case NSEventTypeApplicationDefined:{}break;

        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp:
        {
          release = 1;
        } // fallthrough
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown:
        {
          WM_Event *event = mac_wm_push_event(release ? WM_EventKind_Release : WM_EventKind_Press, window);
          event->window = window_handle;
          if (ns_event.type == NSEventTypeLeftMouseDown || ns_event.type == NSEventTypeLeftMouseUp)
          {
            event->key = WM_Key_LeftMouseButton;
          }
          else if (ns_event.type == NSEventTypeRightMouseDown || ns_event.type == NSEventTypeRightMouseUp)
          {
            event->key = WM_Key_RightMouseButton;
          }
          else if (ns_event.type == NSEventTypeOtherMouseDown || ns_event.type == NSEventTypeOtherMouseUp)
          {
            event->key = WM_Key_MiddleMouseButton;
          }
          NSPoint pos = ns_event.locationInWindow;
          F32 scale_factor = ns_event.window.screen.backingScaleFactor;
          event->pos.x = (F32) pos.x*scale_factor;
          event->pos.y = (F32) (ns_event.window.contentView.frame.size.height - pos.y)*scale_factor;

          window->dragging_window = 0;
          if (event->key == WM_Key_LeftMouseButton && !release)
          {
            Vec2F32 pos_client = event->pos;
            if (pos_client.y < window->custom_border_title_thickness)
            {
              B32 is_over_title_bar_client_area = 0;
              for (MAC_WM_TitleBarClientArea *area = window->first_title_bar_client_area;
                   area != 0;
                   area = area->next)
              {
                Rng2F32 rect = area->rect;
                if (rect.x0 <= pos_client.x && pos_client.x < rect.x1 &&
                    rect.y0 <= pos_client.y && pos_client.y < rect.y1)
                {
                  is_over_title_bar_client_area = 1;
                  break;
                }
              }
              window->dragging_window = !is_over_title_bar_client_area;
            }
          }
        } break;

        case NSEventTypeKeyUp:
        {
          release = 1;
        } // fallthrough
        case NSEventTypeKeyDown:
        {
          should_send = 0;
          // brt: key down & key up
          {
            WM_Event *event = mac_wm_push_event(release ? WM_EventKind_Release : WM_EventKind_Press, window);
            event->window = window_handle;
            event->key = mac_wm_os_key_from_vkey(ns_event.keyCode);
            event->is_repeat = ns_event.ARepeat != 0;
            if(event->key == WM_Key_Alt   && event->modifiers & WM_Modifier_Alt)   { event->modifiers &= ~WM_Modifier_Alt; }
            if(event->key == WM_Key_Ctrl  && event->modifiers & WM_Modifier_Ctrl)  { event->modifiers &= ~WM_Modifier_Ctrl; }
            if(event->key == WM_Key_Shift && event->modifiers & WM_Modifier_Shift) { event->modifiers &= ~WM_Modifier_Shift; }
            //if(event->key == WM_Key_Cmd && event->modifiers & WM_Modifier_Super) { event->modifiers &= ~WM_Modifier_Super; }
          }

          // brt: try text input
          if (release == 0 && ([ns_event modifierFlags] & (NSEventModifierFlagCommand|NSEventModifierFlagControl)) == 0)
          {
            NSString *chars = ns_event.characters;
            NSUInteger length = chars.length;
            unichar buffer[32];
            [chars getCharacters:buffer range:NSMakeRange(0, length)];
            for (NSUInteger idx = 0; idx < length; idx++)
            {
              unichar high = buffer[idx];
              UTF32Char codepoint = 0;
              // brt: surrogate pair?
              if (CFStringIsSurrogateHighCharacter(high) &&
                  idx + 1 < length &&
                  CFStringIsSurrogateLowCharacter(buffer[idx + 1]))
              {
                unichar low = buffer[idx + 1];
                codepoint = CFStringGetLongCharacterForSurrogatePair(high, low);
                idx++;
              }
              else
              {
                codepoint = high;
              }
              if (codepoint >= 32 && codepoint < 127)
              {
                WM_Event *event = mac_wm_push_event(WM_EventKind_Text, window);
                event->window = window_handle;
                event->character = codepoint;
              }
            }
          }
        } break;

        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        {
          if (ns_event.type == NSEventTypeLeftMouseDragged && window->dragging_window)
          {
            [window->nswindow performWindowDragWithEvent:ns_event];
            break;
          }
          WM_Event *event = mac_wm_push_event(WM_EventKind_Press, window);
          if (ns_event.type == NSEventTypeLeftMouseDragged) 
          {
            event->key = WM_Key_LeftMouseButton;
          } else if (ns_event.type == NSEventTypeRightMouseDragged) 
          {
            event->key = WM_Key_RightMouseButton;
          }
        } // fallthrough
        case NSEventTypeMouseMoved:
        {
          WM_Event *event = mac_wm_push_event(WM_EventKind_MouseMove, window);
          NSPoint pos = ns_event.locationInWindow;
          F32 scale_factor = ns_event.window.screen.backingScaleFactor;
          event->pos.x = (F32) pos.x*scale_factor;
          event->pos.y = (F32) (ns_event.window.contentView.frame.size.height - pos.y)*scale_factor;
        } break;

        case NSEventTypeScrollWheel:
        {
          WM_Event *event = mac_wm_push_event(WM_EventKind_Scroll, window);
          NSPoint pos = ns_event.locationInWindow;
          F32 scale_factor = ns_event.window.screen.backingScaleFactor;
          F32 wheel_x = -ns_event.scrollingDeltaX;
          F32 wheel_y = -ns_event.scrollingDeltaY;
          if (!ns_event.hasPreciseScrollingDeltas)
          {
            wheel_x *= 120.f;
            wheel_y *= 120.f;
          }
          event->pos.x = (F32) pos.x*scale_factor;
          event->pos.y = (F32) (ns_event.window.contentView.frame.size.height - pos.y)*scale_factor;
          event->delta = v2f32(wheel_x, wheel_y);
        } break;

        default:
        {
          // brt: debug log this?
          break;
        }
      }

      if (should_send)
      {
        [NSApp sendEvent:ns_event];
      }

      ns_event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                    untilDate:[NSDate distantPast]
                                       inMode:NSEventTrackingRunLoopMode
                                      dequeue:YES];
    }
  }

  return mac_wm_event_list;
}

internal WM_Modifiers
wm_get_modifiers(void)
{
  WM_Modifiers modifiers = 0;
  CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
  if (flags & kCGEventFlagMaskControl)
    modifiers |= WM_Modifier_Ctrl;
  if (flags & kCGEventFlagMaskShift)
    modifiers |= WM_Modifier_Shift;
  if (flags & kCGEventFlagMaskAlternate)
    modifiers |= WM_Modifier_Alt;
#if 0
  if (flags & kCGEventFlagMaskCommand)
    modifiers |= WM_Modifier_Super;
#endif
  return modifiers;
}

internal B32
wm_key_is_down(WM_Key key)
{
  // TODO(rjf)
  return 0;
}

internal Vec2F32
wm_mouse_from_window(WM_Window handle)
{
  Vec2F32 v = {0};
  {
    MAC_WM_Window *window = mac_wm_window_from_handle(handle);
    NSWindow *nswindow = mac_wm_nswindow_from_window(window);
    NSPoint p = [nswindow mouseLocationOutsideOfEventStream];
    p = [nswindow.contentView convertPoint:p
                                  fromView:0];
    F32 scale_factor = nswindow.screen.backingScaleFactor;
    v.x = (F32)p.x*scale_factor;
    v.y = (F32)(nswindow.contentView.frame.size.height - p.y)*scale_factor;
  }
  return v;
}

////////////////////////////////
//~ rjf: @wm_hooks Cursors (Implemented Per-OS)

internal void
wm_set_cursor(WM_Cursor cursor)
{
  NSCursor *nscursor = 0;
  mac_wm_state->last_set_cursor = cursor;
  switch (cursor)
  {
    case WM_Cursor_COUNT: break;
    case WM_Cursor_Pointer: nscursor = [NSCursor arrowCursor]; break;
    case WM_Cursor_IBar: nscursor = [NSCursor IBeamCursor]; break;
    case WM_Cursor_LeftRight: nscursor = [NSCursor resizeLeftRightCursor]; break;
    case WM_Cursor_UpDown: nscursor = [NSCursor resizeUpDownCursor]; break;
    case WM_Cursor_UpRight: nscursor = [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                                                  inDirections:NSCursorFrameResizeDirectionsAll]; break;
    case WM_Cursor_DownRight: nscursor = [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionBottomRight
                                                                  inDirections:NSCursorFrameResizeDirectionsAll]; break;
    case WM_Cursor_UpDownLeftRight: nscursor = [NSCursor openHandCursor]; break;
    case WM_Cursor_HandPoint: nscursor = [NSCursor openHandCursor]; break;
    case WM_Cursor_Disabled: nscursor = [NSCursor operationNotAllowedCursor]; break;
  }
  if (nscursor != 0)
  {
    [nscursor set];
  }
}

////////////////////////////////
//~ rjf: @wm_hooks Native User-Facing Graphical Messages (Implemented Per-OS)

internal void
wm_graphical_message(B32 error, String8 title, String8 message)
{
  @autoreleasepool
  {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSAlert *alert = [[NSAlert alloc] init];
    alert.alertStyle = error ? NSAlertStyleCritical : NSAlertStyleInformational;
    alert.messageText = mac_wm_nsstring_from_string(title);
    alert.informativeText = mac_wm_nsstring_from_string(message);
    [alert layout];
    [[alert window] center];
    [[alert window] makeKeyAndOrderFront:0];
    [alert runModal];
  }
}

internal String8
wm_graphical_pick_file(Arena *arena, String8 initial_path)
{
  String8 result = {0};

  NSURL *dir_url = [NSURL fileURLWithPath:mac_wm_nsstring_from_string(initial_path)];
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  [panel setDirectoryURL:dir_url];
  [panel setCanChooseFiles:YES];
  [panel setCanChooseDirectories:NO];
  [panel setAllowsMultipleSelection:NO];
  NSInteger response = [panel runModal];
  if (response == 1)
  {
    NSArray *urls = panel.URLs;
    NSURL *url = urls[0];
    NSString *path = url.path;
    char *cstr = path.UTF8String;
    result = str8_copy(arena, str8_cstring(cstr));
    [urls release];
    [path release];
  }

  return result;
}

////////////////////////////////
//~ rjf: @wm_hooks Shell Operations

internal void
wm_show_in_filesystem_ui(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 url_cstr = str8_copy(scratch.arena, path);
  CFStringRef s = CFStringCreateWithCString(0, url_cstr.str, kCFStringEncodingUTF8);
  CFURLRef cfurl = CFURLCreateWithFileSystemPath(0, s, kCFURLPOSIXPathStyle, false);
  if (cfurl)
  {
    CFArrayRef urls = CFArrayCreate(0, (const void **)&cfurl, 1, &kCFTypeArrayCallBacks);
    if (urls)
    {
      LSLaunchURLSpec spec = {0};
      spec.appURL = 0;
      spec.itemURLs = urls;
      spec.passThruParams = 0;
      spec.launchFlags = kLSLaunchDefaults | kLSLaunchAndDisplayErrors;
      spec.asyncRefCon = 0;
      LSOpenFromURLSpec(&spec, 0);
      CFRelease(urls);
    }
    LSOpenCFURLRef(cfurl, NULL);
  }
  CFRelease(cfurl);
  CFRelease(s);
  scratch_end(scratch);
}

internal void
wm_open_in_browser(String8 url)
{
  Temp scratch = scratch_begin(0, 0);
  String8 url_cstr = str8_copy(scratch.arena, url);
  CFStringRef s = CFStringCreateWithCString(NULL, url_cstr.str, kCFStringEncodingUTF8);
  CFURLRef cfurl = CFURLCreateWithString(NULL, s, NULL);
  LSOpenCFURLRef(cfurl, NULL);
  CFRelease(cfurl);
  CFRelease(s);
  scratch_end(scratch);
}
