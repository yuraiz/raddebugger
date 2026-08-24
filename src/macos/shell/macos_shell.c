// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: @per_os_impl Shell Operations

internal void
sh_message(B32 error, String8 title, String8 message)
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
sh_pick_file(Arena *arena, String8 title, String8 initial_path)
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

internal void
sh_show_in_file_browser(String8 path)
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
sh_open_in_browser(String8 url)
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

internal B32
sh_install_or_uninstall_self(B32 write, B32 install)
{
  // TODO(rjf)
  return 0;
}
