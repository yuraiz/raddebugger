///////////////////////////////////////////////////////////////////////////////
//~ brt: Generated Code

#include "generated/render_metal.meta.c"

///////////////////////////////////////////////////////////////////////////////
//~ brt: Helpers

internal R_METAL_Alloc
r_metal_push_aligned( U64 size, U64 align )
{
  if (r_metal_state->mtl_arena == 0)
  {
    r_metal_state->mtl_arena = push_array(r_metal_state->buffer_flush_arena, R_METAL_Arena, 1);
    r_metal_state->mtl_arena->current = r_metal_state->mtl_arena;
    r_metal_state->mtl_arena->mtl_buffer = r_metal_state->scratch_buffer_64k;
    r_metal_state->mtl_arena->res = KB(64);
  }

  R_METAL_Arena *current = r_metal_state->mtl_arena->current;
  U64 pos_pre = AlignPow2(current->pos, align);
  U64 pos_pst = pos_pre + size;

  //- brt: chain, if needed
  if (current->res < pos_pst)
  {
    //- brt: allocate new block
    R_METAL_Arena *new_block = push_array(r_metal_state->buffer_flush_arena, R_METAL_Arena, 1);
    {
      U64 flushed_buffer_size = size;
      flushed_buffer_size += MB(1) - 1;
      flushed_buffer_size -= flushed_buffer_size%MB(1);
      new_block->res = flushed_buffer_size;
      new_block->mtl_buffer = [r_metal_state->device newBufferWithLength:new_block->res
                                                                 options:MTLResourceStorageModeShared];
    }
    SLLStackPush_N(r_metal_state->mtl_arena->current, new_block, prev);
    current = new_block;
    pos_pre = AlignPow2(current->pos, align);
    pos_pst = pos_pre+size;

    //- brt: push buffer to flush list
    R_METAL_FlushBuffer *n = push_array(r_metal_state->buffer_flush_arena, R_METAL_FlushBuffer, 1);
    n->buffer = new_block->mtl_buffer;
    SLLQueuePush(r_metal_state->first_buffer_to_flush, r_metal_state->last_buffer_to_flush, n);
  }

  //- brt: push onto current block
  R_METAL_Alloc result = {0};
  if (current->res >= pos_pst)
  {
    result.mtl_buffer = current->mtl_buffer;
    result.mtl_buffer_offset = pos_pre;
    result.v = (U8 *)current->mtl_buffer.contents+pos_pre;
    current->pos = pos_pst;
  }

  //- brt: panic on failure
  if (Unlikely(result.v == 0))
  {
    sh_message(1, str8_lit("Fatal Allocation Failure"), str8_lit("Unexpected GPU memory allocation failure."));
    abort_self(1);
  }

  return result;
}

internal R_METAL_Window *
r_metal_window_from_handle( R_Handle handle )
{
  R_METAL_Window *window = (R_METAL_Window *)handle.u64[0];
  if (window == 0)
  {
    window = &r_metal_window_nil;
  }
  return window;
}

internal R_Handle 
r_metal_handle_from_window( R_METAL_Window *window )
{
  R_Handle handle = {0};
  handle.u64[0] = (U64)window;
  return handle;
}

internal R_METAL_Tex2D *
r_metal_tex2d_from_handle( R_Handle handle )
{
  R_METAL_Tex2D *texture = (R_METAL_Tex2D *)handle.u64[0];
  if (texture == 0)
  {
    texture = &r_metal_tex2d_nil;
  }
  return texture;
}

internal R_Handle 
r_metal_handle_from_tex2d( R_METAL_Tex2D *texture )
{
  R_Handle handle = {0};
  handle.u64[0] = (U64)texture;
  return handle;
}

internal R_METAL_Buffer *
r_metal_buffer_from_handle( R_Handle handle )
{
  R_METAL_Buffer *buffer = (R_METAL_Buffer *)handle.u64[0];
  if (buffer == 0)
  {
    buffer = &r_metal_buffer_nil;
  }
  return buffer;
}

internal R_Handle
r_metal_handle_from_buffer( R_METAL_Buffer *buffer )
{
  R_Handle handle = {0};
  handle.u64[0] = (U64)buffer;
  return handle;
}

internal id<MTLBuffer>
r_metal_instance_buffer_from_size( U64 size )
{
  id<MTLBuffer> buffer = r_metal_state->scratch_buffer_64k;
  if (size > KB(64))
  {
    //- brt: build buffer
    {
      U64 flushed_buffer_size = size;
      flushed_buffer_size += MB(1) - 1;
      flushed_buffer_size -= flushed_buffer_size%MB(1);
      buffer = [r_metal_state->device newBufferWithLength:flushed_buffer_size
                                                  options:MTLResourceStorageModeShared];
    }

    //- brt: push buffer to flush list
    R_METAL_FlushBuffer *n = push_array(r_metal_state->buffer_flush_arena, R_METAL_FlushBuffer, 1);
    n->buffer = buffer;
    SLLQueuePush(r_metal_state->first_buffer_to_flush, r_metal_state->last_buffer_to_flush, n);
  }
  return buffer;
}

///////////////////////////////////////////////////////////////////////////////
//~ brt: Backend Hook Implementations

//- brt: top-level layer initialization

r_hook void
r_init(CmdLine *cmdln)
{
  ProfBeginFunction();

  Arena *arena = arena_alloc();
  r_metal_state = push_array(arena, R_METAL_State, 1);
  r_metal_state->arena = arena;
  r_metal_state->device_rw_mutex = rw_mutex_alloc();

  //- brt: create base device
  @autoreleasepool
  {
    r_metal_state->device = MTLCreateSystemDefaultDevice();
    r_metal_state->command_queue = [r_metal_state->device newCommandQueue];

    //- brt: compile shaders
    {
      NSString *shaders = [mac_wm_nsstring_from_string(r_metal_g_shaders_src) autorelease];
      NSError *error = 0;
      MTLCompileOptions *options = [[[MTLCompileOptions alloc] init] autorelease];
      id<MTLLibrary> mtl_library = [r_metal_state->device newLibraryWithSource:shaders
                                                                       options:options
                                                                         error:&error];
      if (error)
      {
        String8 reason = str8_cstring([[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
        sh_message(1, str8_lit("Fatal Error Compiling Shaders"), reason);
        abort_self(1);
      }

      //- brt: create rect pipeline state
      {
        MTLRenderPipelineDescriptor *mtl_pipeline_state_desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        id<MTLFunction> vs = [mtl_library newFunctionWithName:@"vs_rect"];
        id<MTLFunction> fs = [mtl_library newFunctionWithName:@"fs_rect"];
        mtl_pipeline_state_desc.vertexFunction = vs;
        mtl_pipeline_state_desc.fragmentFunction = fs;
        mtl_pipeline_state_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        mtl_pipeline_state_desc.colorAttachments[0].blendingEnabled = YES;
        mtl_pipeline_state_desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        mtl_pipeline_state_desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        mtl_pipeline_state_desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        mtl_pipeline_state_desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        mtl_pipeline_state_desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
        mtl_pipeline_state_desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        mtl_pipeline_state_desc.vertexBuffers[0].mutability = MTLMutabilityImmutable;
        r_metal_state->rect_render_pipeline_state = [r_metal_state->device newRenderPipelineStateWithDescriptor:mtl_pipeline_state_desc
          error:&error];
      }

      if (error)
      {
        String8 reason = str8_cstring([[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
        sh_message(1, str8_lit("Error Creating Render Pipeline"), reason);
        abort_self(1);
      }

      //- brt: create geometry pipeline state
      {
        MTLRenderPipelineDescriptor *mtl_pipeline_state_desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        id<MTLFunction> vs = [mtl_library newFunctionWithName:@"vs_mesh"];
        id<MTLFunction> fs = [mtl_library newFunctionWithName:@"fs_mesh"];
        mtl_pipeline_state_desc.vertexFunction = vs;
        mtl_pipeline_state_desc.fragmentFunction = fs;
        mtl_pipeline_state_desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        mtl_pipeline_state_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        mtl_pipeline_state_desc.colorAttachments[0].blendingEnabled = NO;
        mtl_pipeline_state_desc.vertexBuffers[0].mutability = MTLMutabilityImmutable;
        r_metal_state->geo_render_pipeline_state = [r_metal_state->device newRenderPipelineStateWithDescriptor:mtl_pipeline_state_desc
          error:&error];
      }

      if (error)
      {
        String8 reason = str8_cstring([[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
        sh_message(1, str8_lit("Error Creating Render Pipeline"), reason);
        abort_self(1);
      }

      //- brt: create geometry depth stencil state
      {
        MTLDepthStencilDescriptor *mtl_depth_state_desc = [[[MTLDepthStencilDescriptor alloc] init] autorelease];
        mtl_depth_state_desc.depthCompareFunction = MTLCompareFunctionLessEqual;
        mtl_depth_state_desc.depthWriteEnabled = YES;
        r_metal_state->geo_depth_state = [r_metal_state->device newDepthStencilStateWithDescriptor:mtl_depth_state_desc];
      }

      //- brt: create downsample pipeline state
      {
        MTLRenderPipelineDescriptor *mtl_pipeline_state_desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        id<MTLFunction> vs = [mtl_library newFunctionWithName:@"vs_blur"];
        id<MTLFunction> fs = [mtl_library newFunctionWithName:@"fs_downsample_2x"];
        mtl_pipeline_state_desc.vertexFunction = vs;
        mtl_pipeline_state_desc.fragmentFunction = fs;
        mtl_pipeline_state_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        mtl_pipeline_state_desc.colorAttachments[0].blendingEnabled = NO;
        mtl_pipeline_state_desc.vertexBuffers[0].mutability = MTLMutabilityImmutable;
        r_metal_state->downsample_render_pipeline_state = [r_metal_state->device newRenderPipelineStateWithDescriptor:mtl_pipeline_state_desc
          error:&error];
      }

      if (error)
      {
        String8 reason = str8_cstring([[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
        sh_message(1, str8_lit("Error Creating Render Pipeline"), reason);
        abort_self(1);
      }

      //- brt: create blur pipeline state
      {
        MTLRenderPipelineDescriptor *mtl_pipeline_state_desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        id<MTLFunction> vs = [mtl_library newFunctionWithName:@"vs_blur"];
        id<MTLFunction> fs = [mtl_library newFunctionWithName:@"fs_blur"];
        mtl_pipeline_state_desc.vertexFunction = vs;
        mtl_pipeline_state_desc.fragmentFunction = fs;
        mtl_pipeline_state_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        mtl_pipeline_state_desc.colorAttachments[0].blendingEnabled = NO;
        mtl_pipeline_state_desc.vertexBuffers[0].mutability = MTLMutabilityImmutable;
        r_metal_state->blur_render_pipeline_state = [r_metal_state->device newRenderPipelineStateWithDescriptor:mtl_pipeline_state_desc
          error:&error];
      }

      if (error)
      {
        String8 reason = str8_cstring([[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
        sh_message(1, str8_lit("Error Creating Render Pipeline"), reason);
        abort_self(1);
      }

      //- brt: create blur composite pipeline state
      {
        MTLRenderPipelineDescriptor *mtl_pipeline_state_desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        id<MTLFunction> vs = [mtl_library newFunctionWithName:@"vs_blur"];
        id<MTLFunction> fs = [mtl_library newFunctionWithName:@"fs_blur_composite"];
        mtl_pipeline_state_desc.vertexFunction = vs;
        mtl_pipeline_state_desc.fragmentFunction = fs;
        mtl_pipeline_state_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        mtl_pipeline_state_desc.colorAttachments[0].blendingEnabled = NO;
        mtl_pipeline_state_desc.vertexBuffers[0].mutability = MTLMutabilityImmutable;
        r_metal_state->blur_composite_render_pipeline_state = [r_metal_state->device newRenderPipelineStateWithDescriptor:mtl_pipeline_state_desc
          error:&error];
      }

      if (error)
      {
        String8 reason = str8_cstring([[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
        sh_message(1, str8_lit("Error Creating Render Pipeline"), reason);
        abort_self(1);
      }

      //- brt: create finalize pipeline state
      {
        MTLRenderPipelineDescriptor *mtl_pipeline_state_desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
        id<MTLFunction> vs = [mtl_library newFunctionWithName:@"vs_finalize"];
        id<MTLFunction> fs = [mtl_library newFunctionWithName:@"fs_finalize"];
        mtl_pipeline_state_desc.vertexFunction = vs;
        mtl_pipeline_state_desc.fragmentFunction = fs;
        mtl_pipeline_state_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        mtl_pipeline_state_desc.colorAttachments[0].blendingEnabled = NO;
        mtl_pipeline_state_desc.vertexBuffers[0].mutability = MTLMutabilityImmutable;
        r_metal_state->finalize_render_pipeline_state = [r_metal_state->device newRenderPipelineStateWithDescriptor:mtl_pipeline_state_desc
          error:&error];
      }

      if (error)
      {
        String8 reason = str8_cstring([[error localizedDescription] cStringUsingEncoding:NSUTF8StringEncoding]);
        sh_message(1, str8_lit("Error Creating Render Pipeline"), reason);
        abort_self(1);
      }
    }

    //- brt: create nearest-neighbor sampler
    ProfScope("create nearest-neighbor sampler")
    {
      MTLSamplerDescriptor *desc = [[[MTLSamplerDescriptor alloc] init] autorelease];
      {
        desc.minFilter = MTLSamplerMinMagFilterNearest;
        desc.magFilter = MTLSamplerMinMagFilterNearest;
        desc.mipFilter = MTLSamplerMipFilterNearest;
        desc.sAddressMode = MTLSamplerAddressModeRepeat;
        desc.tAddressMode = MTLSamplerAddressModeRepeat;
        desc.rAddressMode = MTLSamplerAddressModeRepeat;
        desc.compareFunction = MTLCompareFunctionNever;
        r_metal_state->samplers[R_Tex2DSampleKind_Nearest] = [r_metal_state->device newSamplerStateWithDescriptor:desc];
      }
    }

    //- brt: create bilinear sampler
    ProfScope("create bilinear sampler")
    {
      MTLSamplerDescriptor *desc = [[[MTLSamplerDescriptor alloc] init] autorelease];
      {
        desc.minFilter = MTLSamplerMinMagFilterLinear;
        desc.magFilter = MTLSamplerMinMagFilterLinear;
        desc.mipFilter = MTLSamplerMipFilterLinear;
        desc.sAddressMode = MTLSamplerAddressModeRepeat;
        desc.tAddressMode = MTLSamplerAddressModeRepeat;
        desc.rAddressMode = MTLSamplerAddressModeRepeat;
        desc.compareFunction = MTLCompareFunctionNever;
        r_metal_state->samplers[R_Tex2DSampleKind_Linear] = [r_metal_state->device newSamplerStateWithDescriptor:desc];
      }
    }

    //- brt: create scratch buffer
    {
      r_metal_state->scratch_buffer_64k = [r_metal_state->device newBufferWithLength:KB(64) options:MTLResourceStorageModeShared];
    }

    //- brt: create backup texture
    {
      U32 backup_texture_data[] =
      {
        0xff00ffff, 0x330033ff,
        0x330033ff, 0xff00ffff,
      };
      r_metal_state->backup_texture = r_tex2d_alloc(R_ResourceKind_Static, v2s32(2, 2), R_Tex2DFormat_RGBA8, backup_texture_data);
    }

    //- brt: init buffer flush state
    {
      r_metal_state->buffer_flush_arena = arena_alloc();
    }
  }

  ProfEnd();
}

//- brt: window setup/teardown
r_hook R_Handle
r_window_equip(WM_Window handle)
{
  ProfBeginFunction();

  R_Handle result = {0};
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    //- brt: allocate per-window-state
    R_METAL_Window *window = r_metal_state->first_free_window;
    {
      if (window == 0)
      {
        window = push_array(r_metal_state->arena, R_METAL_Window, 1);
      }
      else
      {
        U64 gen = window->generation;
        SLLStackPop(r_metal_state->first_free_window);
        MemoryZeroStruct(window);
        window->generation = gen;
      }
      window->generation++;
    }

    //- brt: map os window handle -> NSWindow
    NSWindow *nswindow = 0;
    {
      MAC_WM_Window *mac_layer_window = mac_wm_window_from_handle(handle);
      nswindow = mac_wm_nswindow_from_window(mac_layer_window);
    }

    //- brt: create & equip CAMetalLayer
    {
      window->layer = [CAMetalLayer layer];
      window->layer.autoresizingMask = kCALayerHeightSizable | kCALayerWidthSizable;
      window->layer.needsDisplayOnBoundsChange = YES;
      window->layer.device = r_metal_state->device;
      window->layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
      window->layer.framebufferOnly = YES;
#if 0
      window->layer.allowsNextDrawableTimeout = NO;
      window->layer.displaySyncEnabled = NO;
#else
      window->layer.maximumDrawableCount = 2;
#endif
      F32 width_pixels = nswindow.contentView.frame.size.width * nswindow.backingScaleFactor;
      F32 height_pixels = nswindow.contentView.frame.size.height * nswindow.backingScaleFactor;
      window->layer.drawableSize = CGSizeMake(width_pixels, height_pixels);
      window->layer.contentsScale = nswindow.backingScaleFactor;
      nswindow.contentView.layer = window->layer;

      nswindow.contentView.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;
      nswindow.contentView.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    }

    result = r_metal_handle_from_window(window);
  }
  ProfEnd();
  return result;
}

r_hook void
r_window_unequip(WM_Window handle, R_Handle equip_handle)
{
  ProfBeginFunction();
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    //- brt: unequip CAMetalLayer from NSWindow
    MAC_WM_Window *mac_layer_window = mac_wm_window_from_handle(handle);
    NSWindow *nswindow = mac_wm_nswindow_from_window(mac_layer_window);
    nswindow.contentView.layer = 0;

    //- brt: release CAMetalLayer
    R_METAL_Window *window = r_metal_window_from_handle(equip_handle);
    window->layer = 0;
    window->generation++;
    SLLStackPush(r_metal_state->first_free_window, window);
  }
  ProfEnd();
}

//- brt: textures

r_hook R_Handle
r_tex2d_alloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void *data)
{
  ProfBeginFunction();

  //- brt: allocate
  R_METAL_Tex2D *texture = 0;
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    texture = r_metal_state->first_free_tex2d;
    if (texture == 0)
    {
      texture = push_array(r_metal_state->arena, R_METAL_Tex2D, 1);
    }
    else
    {
      U64 gen = texture->generation;
      SLLStackPop(r_metal_state->first_free_tex2d);
      MemoryZeroStruct(texture);
      texture->generation = gen;
    }
    texture->generation++;
  }

  if (kind == R_ResourceKind_Static)
  {
    Assert(data != 0 && "static texture must have initial data provided");
  }

  //- brt: format -> MTLPixelFormat
  MTLPixelFormat mtl_format = MTLPixelFormatRGBA8Unorm;
  switch (format)
  {
    default:{}break;
    case R_Tex2DFormat_R8:    {mtl_format = MTLPixelFormatR8Unorm;}break;
    case R_Tex2DFormat_RG8:   {mtl_format = MTLPixelFormatRG8Unorm;}break;
    case R_Tex2DFormat_RGBA8: {mtl_format = MTLPixelFormatRGBA8Unorm;}break;
    case R_Tex2DFormat_BGRA8: {mtl_format = MTLPixelFormatBGRA8Unorm;}break;
    case R_Tex2DFormat_R16:   {mtl_format = MTLPixelFormatR16Unorm;}break;
    case R_Tex2DFormat_RGBA16:{mtl_format = MTLPixelFormatRGBA16Unorm;}break;
    case R_Tex2DFormat_R32:   {mtl_format = MTLPixelFormatR32Float;}break;
    case R_Tex2DFormat_RG32:  {mtl_format = MTLPixelFormatRG32Float;}break;
    case R_Tex2DFormat_RGBA32:{mtl_format = MTLPixelFormatRGBA32Float;}break;
  }

  //- brt: create texture
  MTLTextureDescriptor *texture_desc =
    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_format
                                                       width:(NSUInteger)size.x
                                                      height:(NSUInteger)size.y
                                                      mipmapped:NO];
#if ARCH_ARM64
  texture_desc.storageMode = MTLStorageModeShared;
#else
  //- brt: intel CPUs cant use shared textures
  texture_desc.storageMode = MTLStorageModeManaged;
#endif
  texture->texture = [r_metal_state->device newTextureWithDescriptor:texture_desc];

  //- brt: upload initial data, if passed
  if (data != 0)
  {
    MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)size.x, (NSUInteger)size.y);
    NSUInteger bytes_per_row = r_tex2d_format_bytes_per_pixel_table[format] * size.x;
    [texture->texture replaceRegion:region
                        mipmapLevel:0
                          withBytes:data
                        bytesPerRow:bytes_per_row];
  }

  //- brt: fill basics
  {
    texture->kind = kind;
    texture->size = size;
    texture->format = format;
  }

  R_Handle result = r_metal_handle_from_tex2d(texture);
  ProfEnd();
  return result;
}

r_hook void
r_tex2d_release(R_Handle handle)
{
  ProfBeginFunction();
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(handle);
    if (texture != &r_metal_tex2d_nil)
    {
      texture->texture = 0;
      SLLStackPush(r_metal_state->first_to_free_tex2d, texture);
    }
  }
  ProfEnd();
}

r_hook R_ResourceKind
r_kind_from_tex2d(R_Handle handle)
{
  R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(handle);
  return texture->kind;
}

r_hook Vec2S32
r_size_from_tex2d(R_Handle handle)
{
  R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(handle);
  return texture->size;
}

r_hook R_Tex2DFormat
r_format_from_tex2d(R_Handle handle)
{
  R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(handle);
  return texture->format;
}

r_hook void
r_fill_tex2d_region(R_Handle handle, Rng2S32 subrect, void *data)
{
  ProfBeginFunction();
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(handle);
    if (texture != &r_metal_tex2d_nil)
    {
      Assert(texture->kind == R_ResourceKind_Dynamic && "only dynamic texture can update region");
      U64 bytes_per_pixel = r_tex2d_format_bytes_per_pixel_table[texture->format];
      Vec2S32 dim = v2s32(subrect.x1 - subrect.x0, subrect.y1 - subrect.y0);
      MTLRegion region = MTLRegionMake2D(subrect.x0, subrect.y0, dim.x, dim.y);
      [texture->texture replaceRegion:region
                          mipmapLevel:0
                            withBytes:data
                          bytesPerRow:bytes_per_pixel*dim.x];
    }
  }
  ProfEnd();
}

//- brt: buffers

r_hook R_Handle
r_buffer_alloc(R_ResourceKind kind, U64 size, void *data)
{
  //- brt: allocate
  R_METAL_Buffer *buffer = 0;
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    buffer = r_metal_state->first_free_buffer;
    if (buffer == 0)
    {
      buffer = push_array(r_metal_state->arena, R_METAL_Buffer, 1);
    }
    else
    {
      U64 gen = buffer->generation;
      SLLStackPop(r_metal_state->first_free_buffer);
      MemoryZeroStruct(buffer);
      buffer->generation = gen;
    }
    buffer->generation++;
  }

  buffer->size = size;
  if (data == 0)
  {
    buffer->buffer = [r_metal_state->device newBufferWithLength:buffer->size
                                                        options:MTLResourceStorageModeShared];
  }
  else
  {
    buffer->buffer = [r_metal_state->device newBufferWithBytes:data
                                                        length:buffer->size
                                                       options:MTLResourceStorageModeShared];
  }
  R_Handle result = r_metal_handle_from_buffer(buffer);
  return result;
}

r_hook void
r_buffer_release(R_Handle handle)
{
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    R_METAL_Buffer *buffer = r_metal_buffer_from_handle(handle);
    SLLStackPush(r_metal_state->first_to_free_buffer, buffer);
  }
}

//- brt: frame markers

r_hook void
r_begin_frame(void)
{
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    // NOTE(brt): no-op
  }
}

r_hook void
r_end_frame(void)
{
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    for (R_METAL_FlushBuffer *buffer = r_metal_state->first_buffer_to_flush;
         buffer != 0;
         buffer = buffer->next)
    {
      [buffer->buffer release];
      buffer->buffer = 0;
    }
    for (R_METAL_Tex2D *tex = r_metal_state->first_to_free_tex2d, *next = 0;
         tex != 0;
         tex = next)
    {
      next = tex->next;
      [tex->texture release];
      tex->texture = 0;
      tex->generation++;
      SLLStackPush(r_metal_state->first_free_tex2d, tex);
    }
    for (R_METAL_Buffer *buf = r_metal_state->first_to_free_buffer, *next = 0;
         buf != 0;
         buf = next)
    {
      next = buf->next;
      [buf->buffer setPurgeableState:MTLPurgeableStateEmpty];
      [buf->buffer release];
      buf->generation += 1;
      buf->buffer = 0;
      SLLStackPush(r_metal_state->first_free_buffer, buf);
    }
    arena_clear(r_metal_state->buffer_flush_arena);
    r_metal_state->mtl_arena = 0;
    r_metal_state->first_buffer_to_flush = r_metal_state->last_buffer_to_flush = 0;
    r_metal_state->first_to_free_tex2d = 0;
    r_metal_state->first_to_free_buffer = 0;
  }
}

r_hook void
r_window_begin_frame(WM_Window window, R_Handle window_equip)
{
  ProfBeginFunction();
  MutexScopeW(r_metal_state->device_rw_mutex)
  {
    R_METAL_Window *wnd = r_metal_window_from_handle(window_equip);
    NSWindow *nswnd = 0;
    {
      MAC_WM_Window *macwnd = mac_wm_window_from_handle(window);
      nswnd = mac_wm_nswindow_from_window(macwnd);
    }

    //- brt: get resolution & scale factor
    Rng2F32 client_rect = wm_client_rect_from_window(window);
    Vec2S32 resolution = {(S32)(client_rect.x1 - client_rect.x0), (S32)(client_rect.y1 - client_rect.y0)};

    //- brt: resolution change
    B32 resize_done = 0;
    if (wnd->last_resolution.x != resolution.x ||
        wnd->last_resolution.y != resolution.y) @autoreleasepool
    {
      resize_done = 1;
      wnd->last_resolution = resolution;

      F32 width = resolution.x;
      F32 height = resolution.y;
    
      //- brt: release screen-sized render target, if there
      if (wnd->stage_color != 0) {[wnd->stage_color release];};

      //- brt: create stage color targets
      {
        MTLTextureDescriptor *color_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                              width:(NSUInteger)width
                                                                                             height:(NSUInteger)height
                                                                                          mipmapped:NO];
        color_desc.textureType = MTLTextureType2D;
        color_desc.usage |= MTLTextureUsageRenderTarget | MTLTextureUsageShaderWrite;
        color_desc.storageMode = MTLStorageModePrivate;
        wnd->stage_color = [r_metal_state->device newTextureWithDescriptor:color_desc];
        wnd->stage_color.label = @"Staging Render Target";

        wnd->stage_blur = [r_metal_state->device newTextureWithDescriptor:color_desc];
        wnd->stage_blur.label = @"Staging Blur Render Target";

        MTLTextureDescriptor *depth_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                              width:(NSUInteger)width
                                                                                             height:(NSUInteger)height
                                                                                          mipmapped:NO];
        depth_desc.usage |= MTLTextureUsageRenderTarget;
        depth_desc.storageMode = MTLStorageModeMemoryless;
        wnd->stage_depth = [r_metal_state->device newTextureWithDescriptor:depth_desc];
        wnd->stage_depth.label = @"Staging Depth Target";
      }

      //- brt: resize drawable
      wnd->layer.drawableSize = CGSizeMake(width, height);
    }

    wnd->command_buffer = [r_metal_state->command_queue commandBuffer];
    wnd->command_buffer.label = [NSString stringWithFormat:@"Frame %llu", wnd->frame_idx];
  }
  ProfEnd();
}

r_hook void
r_window_end_frame(WM_Window window, R_Handle window_equip)
{
  ProfBeginFunction();
  MutexScopeW(r_metal_state->device_rw_mutex) @autoreleasepool
  {
    //- brt: unpack arguments
    R_METAL_Window *wnd = r_metal_window_from_handle(window_equip);
    id<CAMetalDrawable> mtl_drawable = [wnd->layer nextDrawable];

    ///////////////////////////////////////////////////////////////////////////////
    //~ brt: finalize, by writing staging buffer out to window framebuffer
    //
    {
      [wnd->command_buffer pushDebugGroup:@"Finalize"];
      //- brt: setup output merger
      MTLRenderPassDescriptor *mtl_pass_desc = [MTLRenderPassDescriptor renderPassDescriptor];
      mtl_pass_desc.colorAttachments[0].texture = mtl_drawable.texture;
      mtl_pass_desc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      mtl_pass_desc.colorAttachments[0].storeAction = MTLStoreActionStore;

      //- brt: setup rasterizer
      id<MTLRenderCommandEncoder> mtl_encoder = [wnd->command_buffer renderCommandEncoderWithDescriptor:mtl_pass_desc];
      mtl_encoder.label = @"Output Merger";
      Vec2S32 resolution = wnd->last_resolution;
      MTLViewport viewport = { 0.0, 0.0, resolution.x, resolution.y, 0.0, 1.0 };
      [mtl_encoder setViewport:viewport];
      [mtl_encoder setRenderPipelineState:r_metal_state->finalize_render_pipeline_state];

      //- brt: setup shaders
      [mtl_encoder setFragmentTexture:wnd->stage_color atIndex:0];

      //- brt: setup scissor rect
      {
        MTLScissorRect rect = {0};
        rect.x = 0;
        rect.y = 0;
        rect.width = (NSUInteger)(wnd->last_resolution.x);
        rect.height = (NSUInteger)(wnd->last_resolution.y);
        [mtl_encoder setScissorRect:rect];
      }

      //- brt: draw
      [mtl_encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                      vertexStart:0
                      vertexCount:4];
      [mtl_encoder endEncoding];
      [wnd->command_buffer popDebugGroup];
    }

    ///////////////////////////////////////////////////////////////////////////////
    //~ brt: present
    //
    //[mtl_command_buffer presentDrawable:mtl_drawable afterMinimumDuration:1.0/60.0];
    [wnd->command_buffer presentDrawable:mtl_drawable];
    [wnd->command_buffer commit];
    [wnd->command_buffer waitUntilCompleted];
    wnd->frame_idx += 1;
  }
  ProfEnd();
}

//- brt: render pass submission

r_hook void
r_window_submit(WM_Window window, R_Handle window_equip, R_PassList *passes)
{
  ProfBeginFunction();
  MutexScopeW(r_metal_state->device_rw_mutex) @autoreleasepool
  {
    //- brt: unpack arguments
    R_METAL_Window *wnd = r_metal_window_from_handle(window_equip);

    id<MTLDevice> mtl_device = r_metal_state->device;
    MTLRenderPassDescriptor *mtl_pass_desc = [MTLRenderPassDescriptor renderPassDescriptor];

    Vec2S32 resolution = wnd->last_resolution;
    MTLViewport viewport = { 0.0, 0.0, resolution.x, resolution.y, 0.0, 1.0 };

    B32 first_ui_pass = 1;

    [wnd->command_buffer pushDebugGroup:@"Offscreen Passes"];
    
    //- brt: do passes
    for (R_PassNode *pass_n = passes->first; pass_n != 0; pass_n = pass_n->next)
    {
      R_Pass *pass = &pass_n->v;
      switch (pass->kind)
      {
        default:{}break;

        ///////////////////////////
        //- brt: ui rendering pass
        //
        case R_PassKind_UI:
        {
          // brt: unpack params
          R_PassParams_UI *params = pass->params_ui;
          R_BatchGroup2DList *rect_batch_groups = &params->rects;

          //- brt: set up rasterizer
          mtl_pass_desc.colorAttachments[0].texture = wnd->stage_color;
          mtl_pass_desc.colorAttachments[0].loadAction = first_ui_pass ? MTLLoadActionClear : MTLLoadActionLoad;
          mtl_pass_desc.colorAttachments[0].storeAction = MTLStoreActionStore;
          mtl_pass_desc.depthAttachment.texture = 0;
          mtl_pass_desc.depthAttachment.loadAction = MTLLoadActionDontCare;
          mtl_pass_desc.depthAttachment.storeAction = MTLStoreActionDontCare;
          first_ui_pass = 0;
          id<MTLRenderCommandEncoder> mtl_encoder = [wnd->command_buffer renderCommandEncoderWithDescriptor:mtl_pass_desc];
          mtl_encoder.label = @"UI Pass";
          [mtl_encoder setViewport:viewport];
          [mtl_encoder setRenderPipelineState:r_metal_state->rect_render_pipeline_state]; 

          // brt: draw each batch group
          for (R_BatchGroup2DNode *group_n = rect_batch_groups->first; group_n != 0; group_n = group_n->next)
          {
            // brt: unpack info
            R_BatchList *batches = &group_n->batches;
            R_BatchGroup2DParams *group_params = &group_n->params;
            id<MTLSamplerState> sampler = r_metal_state->samplers[group_params->tex_sample_kind];

            // brt: get & fill buffer
            R_METAL_Alloc mtl_alloc = r_metal_push(batches->byte_count);
            {
              U8 *dst_ptr = mtl_alloc.v;
              U64 off = 0;
              for (R_BatchNode *batch_n = batches->first;
                   batch_n != 0;
                   batch_n = batch_n->next)
              {
                MemoryCopy(dst_ptr+off, batch_n->v.v, batch_n->v.byte_count);
                off += batch_n->v.byte_count;
              }
            }

            // brt: get texture
            R_Handle texture_handle = group_params->tex;
            if (r_handle_match(texture_handle, r_handle_zero()))
            {
              texture_handle = r_metal_state->backup_texture;
            }
            R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(texture_handle);

            // brt: get texture sample map matrix, based on format
            Mat4x4F32 texture_sample_channel_map = r_sample_channel_map_from_tex2dformat(texture->format);

            // brt: upload uniforms
            R_METAL_Uniforms_Rect uniforms = {0};
            {
              uniforms.viewport_size = v2f32(resolution.x, resolution.y);
              uniforms.opacity = 1-group_params->transparency;
              uniforms.texture_sample_channel_map = texture_sample_channel_map;
              uniforms.texture_t2d_size = v2f32(texture->size.x, texture->size.y);
              uniforms.xform[0] = v4f32(group_params->xform.v[0][0], group_params->xform.v[1][0], group_params->xform.v[2][0], 0);
              uniforms.xform[1] = v4f32(group_params->xform.v[0][1], group_params->xform.v[1][1], group_params->xform.v[2][1], 0);
              uniforms.xform[2] = v4f32(group_params->xform.v[0][2], group_params->xform.v[1][2], group_params->xform.v[2][2], 0);
              Vec2F32 xform_2x2_col0 = v2f32(uniforms.xform[0].x, uniforms.xform[1].x);
              Vec2F32 xform_2x2_col1 = v2f32(uniforms.xform[0].y, uniforms.xform[1].y);
              uniforms.xform_scale.x = length_2f32(xform_2x2_col0);
              uniforms.xform_scale.y = length_2f32(xform_2x2_col1);

              [mtl_encoder setVertexBytes:&uniforms
                                   length:sizeof(uniforms)
                          attributeStride:sizeof(uniforms)
                                  atIndex:0];
              [mtl_encoder setFragmentBytes:&uniforms
                                     length:sizeof(uniforms)
                                    atIndex:0];
            }

            // brt: bind instance buffer
            [mtl_encoder setVertexBuffer:mtl_alloc.mtl_buffer
                                  offset:mtl_alloc.mtl_buffer_offset
                                 atIndex:1];

            // brt: bind texture & sampler
            [mtl_encoder setFragmentTexture:texture->texture
                                    atIndex:0];
            [mtl_encoder setFragmentSamplerState:sampler
                                         atIndex:0];

            // brt: setup scissor rect
            {
              Rng2F32 clip = group_params->clip;
              clip.x1 = Min(wnd->last_resolution.x, clip.x1);
              clip.y1 = Min(wnd->last_resolution.y, clip.y1);
              MTLScissorRect rect = {0};

              if (clip.x0 == 0 && clip.y0 == 0 && clip.x1 == 0 && clip.y1 == 0)
              {
                rect.x = 0;
                rect.y = 0;
                rect.width = (NSUInteger)(wnd->last_resolution.x);
                rect.height = (NSUInteger)(wnd->last_resolution.y);
              }
              else if (clip.x0 > clip.x1 || clip.y0 > clip.y1)
              {
                rect.x = 0;
                rect.y = 0;
                rect.width = 0;
                rect.height = 0;
              }
              else
              {
                rect.x = (NSUInteger)(clip.x0);
                rect.y = (NSUInteger)(clip.y0);
                rect.width = (NSUInteger)((clip.x1 - clip.x0));
                rect.height = (NSUInteger)((clip.y1 - clip.y0));
              }
              [mtl_encoder setScissorRect:rect];
            }

            // brt: draw
            [mtl_encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:0
                            vertexCount:4
                          instanceCount:batches->byte_count / batches->bytes_per_inst];
          }
          [mtl_encoder endEncoding];
        } break;

        ////////////////////////////////////
        //- brt: 3d geometry rendering pass
        //
        case R_PassKind_Geo3D:
        {
#if 0
          //- brt: unpack params
          R_PassParams_Geo3D *params = pass->params_geo3d;
          R_BatchGroup3DMap *mesh_group_map = &params->mesh_batches;

          //- brt: set up rasterizer
          mtl_pass_desc.colorAttachments[0].texture = wnd->stage_color;
          mtl_pass_desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
          mtl_pass_desc.colorAttachments[0].storeAction = MTLStoreActionStore;
          mtl_pass_desc.depthAttachment.texture = wnd->stage_depth;
          mtl_pass_desc.depthAttachment.loadAction = MTLLoadActionClear;
          mtl_pass_desc.depthAttachment.storeAction = MTLStoreActionDontCare;
          first_ui_pass = 0;
          Vec2F32 viewport_dim = dim_2f32(params->viewport);
          MTLViewport viewport = { params->viewport.x0, params->viewport.y0, viewport_dim.x, viewport_dim.y, 0.0, 1.0 };
          id<MTLRenderCommandEncoder> mtl_encoder = [wnd->command_buffer renderCommandEncoderWithDescriptor:mtl_pass_desc];
          mtl_encoder.label = @"Geo3D Pass";
          [mtl_encoder setViewport:viewport];
          [mtl_encoder setRenderPipelineState:r_metal_state->geo_render_pipeline_state]; 
          [mtl_encoder setDepthStencilState:r_metal_state->geo_depth_state];

          //- brt: draw mesh batches
          for (U64 slot_idx = 0; slot_idx < mesh_group_map->slots_count; slot_idx += 1)
          {
            for (R_BatchGroup3DMapNode *n = mesh_group_map->slots[slot_idx]; n != 0; n = n->next)
            {
              // brt: unpack group params
              R_BatchList *batches = &n->batches;
              R_BatchGroup3DParams *group_params = &n->params;
              R_METAL_Buffer *mesh_vertices = r_metal_buffer_from_handle(group_params->mesh_vertices);
              R_METAL_Buffer *mesh_indices = r_metal_buffer_from_handle(group_params->mesh_indices);

              [mtl_encoder setVertexBuffer:mesh_vertices->buffer
                                    offset:0
                                   atIndex:0];

              Mat4x4F32 vp_matrix = mul_4x4f32(params->projection, params->view);

#if 0
              for (R_BatchNode *batch_n = batches->first; batch_n != 0; batch_n = batch_n->next)
              {
              }
#endif
              R_BatchNode *batch_node = batches->first;
              R_Mesh3DInst *instance = (R_Mesh3DInst *)batch_node->v.v;
              Mat4x4F32 model_matrix = instance->xform;
              Mat4x4F32 mvp_matrix = mul_4x4f32(vp_matrix, model_matrix);
              [mtl_encoder setVertexBytes:&mvp_matrix
                                    length:sizeof(mvp_matrix)
                           attributeStride:sizeof(mvp_matrix)
                                   atIndex:1];
              [mtl_encoder setVertexBytes:&model_matrix
                                    length:sizeof(model_matrix)
                           attributeStride:sizeof(model_matrix)
                                   atIndex:2];

              Vec3F32 light_dir = {0};
              light_dir.x = 0.3;
              light_dir.y = -0.5;
              light_dir.z = 1.0;
              light_dir = normalize_3f32(light_dir);
              [mtl_encoder setFragmentBytes:&light_dir length:sizeof(light_dir) atIndex:0];

              [mtl_encoder setFragmentBytes:&params->light_dir length:sizeof(params->light_dir) atIndex:1];
              Vec3F32 stddev_hover = {0};
              stddev_hover.x = ot_state->stddev_hovered.x;
              stddev_hover.y = ot_state->stddev_hovered.y;
              stddev_hover.z = ot_state->stddev_hovered_enabled * (cos_f32(radians_from_turns_f32(1)*ot_state->time_in_seconds)*0.5f+0.5f);
              [mtl_encoder setFragmentBytes:&stddev_hover length:sizeof(stddev_hover) atIndex:2];

              {
                R_Handle texture_handle = ot_state->roughness_texture;
                if (r_handle_match(texture_handle, r_handle_zero()))
                {
                  texture_handle = r_metal_state->backup_texture;
                }
                R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(texture_handle);
                [mtl_encoder setFragmentTexture:texture->texture
                  atIndex:0];
              }
              {
                R_Handle texture_handle = ot_state->normal_texture;
                if (r_handle_match(texture_handle, r_handle_zero()))
                {
                  texture_handle = r_metal_state->backup_texture;
                }
                R_METAL_Tex2D *texture = r_metal_tex2d_from_handle(texture_handle);
                [mtl_encoder setFragmentTexture:texture->texture
                  atIndex:1];
              }

              [mtl_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                      indexCount:mesh_indices->size/sizeof(U32)
                                       indexType:MTLIndexTypeUInt32
                                     indexBuffer:mesh_indices->buffer
                               indexBufferOffset:0];
            }
          }
          [mtl_encoder endEncoding];
#endif
        } break;

        /////////////////////////////
        //- brt: blur rendering pass
        case R_PassKind_Blur:
        {
          R_PassParams_Blur *params = pass->params_blur;
          F32 sigma = params->blur_size;
          MPSImageGaussianBlur *blur = [[MPSImageGaussianBlur alloc] initWithDevice:mtl_device
                                                                              sigma:sigma];
          Rng2F32 clip = params->clip;

          Vec2F32 dims = dim_2f32(params->rect);
          blur.clipRect = MTLRegionMake2D(params->rect.min.x, params->rect.min.y, dims.x, dims.y);
          blur.offset = (MPSOffset){params->rect.min.x, params->rect.min.y, 0};
#if 0
          [blur encodeToCommandBuffer:wnd->command_buffer
                        sourceTexture:wnd->stage_color
                   destinationTexture:wnd->stage_blur];
#else
          [blur encodeToCommandBuffer:wnd->command_buffer
                       inPlaceTexture:&wnd->stage_color
                fallbackCopyAllocator:0];
#endif
          [blur autorelease];
        } break;
      }
    }

    [wnd->command_buffer popDebugGroup];
  }
  ProfEnd();
}
