// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

typedef void (*DEBUGPROC)(GLenum source,
          GLenum type,
          GLuint id,
          GLenum severity,
          GLsizei length,
          const GLchar *message,
          const void *userParam);

internal void
glDebugMessageCallbackStub(
  DEBUGPROC callback,
  const void * userParam
) {}

internal VoidProc *
r_ogl_os_load_procedure(char *name)
{
  if(strcmp(name, "glDebugMessageCallback") == 0)
  {
    return (VoidProc *)glDebugMessageCallbackStub;
  }
  return (VoidProc *)dlsym(RTLD_DEFAULT, name);
}

internal void
r_ogl_os_init(CmdLine *cmdln)
{
  U32 attr[] = {
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
    NSOpenGLPFANoRecovery,
    NSOpenGLPFAAccelerated,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize,     8,
    NSOpenGLPFADepthSize,     24,
    NSOpenGLPFASupersample,
    NSOpenGLPFASampleBuffers, 1,
    NSOpenGLPFASamples, 4,
    NSOpenGLPFAMultisample,
    0
  };
  
  NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attr];
  
  CGRect rect = {{0,0}, {800,600}};
  NSOpenGLView* view = [[NSOpenGLView alloc] initWithFrame:rect
                                  pixelFormat:format];

  r_ogl_mac_ctx = view.openGLContext;

  if(r_ogl_mac_ctx == 0)
  {
    printf("gl context is nil\n");
    os_abort(1);
  }

  [r_ogl_mac_ctx makeCurrentContext];
}

internal R_Handle
r_ogl_os_window_equip(OS_Handle window)
{
  // dummy
  R_Handle result = {0};
  return result;
}

internal void
r_ogl_os_window_unequip(OS_Handle os, R_Handle r)
{
  // dummy
}

internal void
r_ogl_os_select_window(OS_Handle window, R_Handle r)
{
  if(os_handle_match(window, os_handle_zero())) {return;}
  OS_MAC_Window *w = (OS_MAC_Window *)window.u64[0];

  NSOpenGLView* view =  w->win.contentView;

  if (view.class != NSOpenGLView.class)
  {
    view = [[NSOpenGLView alloc] initWithFrame:view.frame
                                    pixelFormat:[NSOpenGLView defaultPixelFormat]];
    w->win.contentView = view;

    [view clearGLContext];
    [view setOpenGLContext:r_ogl_mac_ctx];

    [view setLayerContentsPlacement:NSViewLayerContentsPlacementTopLeft];
    [view setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawDuringViewResize];
    
    CALayer* layer = view.layer;
    [layer setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [layer setNeedsDisplayOnBoundsChange:YES];
  }

  [r_ogl_mac_ctx setView:view];
  [r_ogl_mac_ctx makeCurrentContext];
}

internal void
r_ogl_os_window_swap(OS_Handle os, R_Handle r)
{
  glFlush();
  [r_ogl_mac_ctx flushBuffer];
}
