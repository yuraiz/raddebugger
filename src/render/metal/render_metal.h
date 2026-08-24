#ifndef RENDER_METAL_H
#define RENDER_METAL_H

///////////////////////////////////////////////////////////////////////////////
//~ brt: Includes

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

///////////////////////////////////////////////////////////////////////////////
//~ brt: Generated Code

#include "generated/render_metal.meta.h"

///////////////////////////////////////////////////////////////////////////////
//~ brt: C-side Shader Types

typedef struct R_METAL_Uniforms_Rect R_METAL_Uniforms_Rect;
struct R_METAL_Uniforms_Rect
{
  Vec2F32 viewport_size;
  F32 opacity;
  F32 _padding0_;
  Mat4x4F32 texture_sample_channel_map;
  Vec2F32 texture_t2d_size;
  Vec2F32 translate;
  Vec4F32 xform[3];
  Vec2F32 xform_scale;
  F32 _padding1_;
  F32 _padding2_;
};

typedef struct R_METAL_Uniforms_BlurPass R_METAL_Uniforms_BlurPass;
struct R_METAL_Uniforms_BlurPass
{
  Rng2F32 rect;
  Vec4F32 corner_radii;
  Vec2F32 direction;
  Vec2F32 viewport_size;
  U32 blur_count;
  F32 tint_t;
  F32 vibrance;
  U8 _padding0_[196];
};
StaticAssert(sizeof(R_METAL_Uniforms_BlurPass) % 256 == 0, NotAligned);

typedef struct R_METAL_Uniforms_Blur R_METAL_Uniforms_Blur;
struct R_METAL_Uniforms_Blur
{
  R_METAL_Uniforms_BlurPass passes[Axis2_COUNT];
  Vec4F32 kernel[32];
};

///////////////////////////////////////////////////////////////////////////////
//~ brt: Main State Types

typedef struct R_METAL_Arena R_METAL_Arena;
struct R_METAL_Arena
{
  R_METAL_Arena *prev;
  R_METAL_Arena *current;
  U64 pos;
  U64 res;
  id<MTLBuffer> mtl_buffer;
};

typedef struct R_METAL_Alloc R_METAL_Alloc;
struct R_METAL_Alloc
{
  U64 mtl_buffer_offset;
  id<MTLBuffer> mtl_buffer;
  U8 *v;
};

typedef struct R_METAL_Tex2D R_METAL_Tex2D;
struct R_METAL_Tex2D
{
  R_METAL_Tex2D *next;
  U64 generation;
  id<MTLTexture> texture;
  R_ResourceKind kind;
  Vec2S32 size;
  R_Tex2DFormat format;
};


typedef struct R_METAL_Buffer R_METAL_Buffer;
struct R_METAL_Buffer
{
  R_METAL_Buffer *next;
  U64 generation;
  U64 size;
  id<MTLBuffer> buffer;
};


typedef struct R_METAL_Window R_METAL_Window;
struct R_METAL_Window
{
  R_METAL_Window *next;
  U64 generation;
  U64 frame_idx;

  //- brt: metal layer
  CAMetalLayer *layer;
  id<MTLTexture> stage_color;
  id<MTLTexture> stage_blur;
  id<MTLTexture> stage_depth;
  id<MTLCommandBuffer> command_buffer;

  //- brt: last state
  Vec2S32 last_resolution;
};

typedef struct R_METAL_FlushBuffer R_METAL_FlushBuffer;
struct R_METAL_FlushBuffer
{
  R_METAL_FlushBuffer *next;
  id<MTLBuffer> buffer;
};

typedef struct R_METAL_State R_METAL_State;
struct R_METAL_State
{
  //- brt: state
  Arena *arena;
  R_METAL_Arena *mtl_arena;
  R_METAL_Window *first_free_window;
  R_METAL_Tex2D *first_free_tex2d;
  R_METAL_Buffer *first_free_buffer;
  R_METAL_Arena *first_free_arena;
  R_METAL_Tex2D *first_to_free_tex2d;
  R_METAL_Buffer *first_to_free_buffer;
  RWMutex device_rw_mutex;

  //- brt: metal objects
  id<MTLDevice> device;
  id<MTLCommandQueue> command_queue;
  id<MTLRenderPipelineState> rect_render_pipeline_state;
  id<MTLRenderPipelineState> geo_render_pipeline_state;
  id<MTLRenderPipelineState> downsample_render_pipeline_state;
  id<MTLRenderPipelineState> blur_render_pipeline_state;
  id<MTLRenderPipelineState> blur_composite_render_pipeline_state;
  id<MTLRenderPipelineState> finalize_render_pipeline_state;
  id<MTLDepthStencilState> geo_depth_state;
  id<MTLSamplerState> samplers[R_Tex2DSampleKind_COUNT];
  id<MTLBuffer> scratch_buffer_64k;

  //- brt: backups
  R_Handle backup_texture;

  //- brt: buffers to flush at end of frame
  Arena *buffer_flush_arena;
  R_METAL_FlushBuffer *first_buffer_to_flush;
  R_METAL_FlushBuffer *last_buffer_to_flush;
};

///////////////////////////////////////////////////////////////////////////////
//~ brt: Globals

global R_METAL_State *r_metal_state = 0;
global read_only R_METAL_Window r_metal_window_nil = {&r_metal_window_nil};
global read_only R_METAL_Tex2D r_metal_tex2d_nil = {&r_metal_tex2d_nil};
global read_only R_METAL_Buffer r_metal_buffer_nil = {&r_metal_buffer_nil};

///////////////////////////////////////////////////////////////////////////////
//~ brt: Helpers

internal R_METAL_Alloc r_metal_push_aligned( U64 size, U64 align );
#define r_metal_push(size) r_metal_push_aligned((size), 64)

internal R_METAL_Window *r_metal_window_from_handle( R_Handle handle );
internal R_Handle r_metal_handle_from_window( R_METAL_Window *window );
internal R_METAL_Tex2D *r_metal_tex2d_from_handle( R_Handle handle );
internal R_Handle r_metal_handle_from_tex2d( R_METAL_Tex2D *texture );
internal R_METAL_Buffer *r_metal_buffer_from_handle( R_Handle handle );
internal R_Handle r_metal_handle_from_buffer( R_METAL_Buffer *buffer );
internal id<MTLBuffer> r_metal_instance_buffer_from_size( U64 size );

#endif // RENDER_METAL_H
