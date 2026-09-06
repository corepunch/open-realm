#ifndef r_backend_h
#define r_backend_h

/*
 * GL state cache under an explicit draw API (#89).
 * Callers use RB_* instead of raw enable/blend/depth/cull.
 * R_Call(gl...) stays for resource create/upload and for the packets issued here.
 */

/* Packed blend/depth/color bits (Doom 3-style XOR delta). */
#define GLS_DEPTHTEST           (1u << 0)
#define GLS_DEPTHMASK           (1u << 1)
#define GLS_BLEND               (1u << 2)
#define GLS_CULL                (1u << 3)
#define GLS_SCISSOR             (1u << 4)
#define GLS_POLYGON_OFFSET      (1u << 5)
#define GLS_A2C                 (1u << 6)
#define GLS_DEPTHFUNC_SHIFT     8
#define GLS_DEPTHFUNC_MASK      (7u << GLS_DEPTHFUNC_SHIFT)
#define GLS_SRCBLEND_SHIFT      12
#define GLS_SRCBLEND_MASK       (15u << GLS_SRCBLEND_SHIFT)
#define GLS_DSTBLEND_SHIFT      16
#define GLS_DSTBLEND_MASK       (15u << GLS_DSTBLEND_SHIFT)
#define GLS_COLORMASK_SHIFT     20
#define GLS_COLORMASK_MASK      (15u << GLS_COLORMASK_SHIFT)
#define GLS_COLORMASK_RGBA      (15u << GLS_COLORMASK_SHIFT)

#define GLS_DEFAULT (GLS_DEPTHTEST | GLS_DEPTHMASK | GLS_CULL | GLS_COLORMASK_RGBA)

typedef enum {
    RB_DEPTH_NEVER = 0,
    RB_DEPTH_LESS,
    RB_DEPTH_EQUAL,
    RB_DEPTH_LEQUAL,
    RB_DEPTH_GREATER,
    RB_DEPTH_NOTEQUAL,
    RB_DEPTH_GEQUAL,
    RB_DEPTH_ALWAYS
} rbDepthFunc_t;

typedef enum {
    RB_BLEND_ZERO = 0,
    RB_BLEND_ONE,
    RB_BLEND_SRC_COLOR,
    RB_BLEND_ONE_MINUS_SRC_COLOR,
    RB_BLEND_DST_COLOR,
    RB_BLEND_ONE_MINUS_DST_COLOR,
    RB_BLEND_SRC_ALPHA,
    RB_BLEND_ONE_MINUS_SRC_ALPHA,
    RB_BLEND_DST_ALPHA,
    RB_BLEND_ONE_MINUS_DST_ALPHA,
    RB_BLEND_SRC_ALPHA_SATURATE
} rbBlendFactor_t;

typedef struct backEndState_s {
    uint32_t glStateBits;
    GLenum   faceCulling;
    GLenum   depthFunc;
    GLenum   blendSrc;
    GLenum   blendDst;
    GLenum   blendEquation;
    float    polygonOffsetScale;
    float    polygonOffsetBias;
    DWORD    activeTextureUnit;
    DWORD    currentPipeline;
    DWORD    currentVAO;
    DWORD    currentFBO;
    GLint    scissor[4];
    GLint    viewport[4];
    GLboolean colorMask[4];
    int      initialized;
} backEndState_t;

/* Thin pipeline surface (Phase 3). Raster blob lives here; data is the root struct. */
typedef enum {
    RB_CULL_NONE = 0,
    RB_CULL_BACK,
    RB_CULL_FRONT
} rbCull_t;

typedef struct pipelineDesc_s {
    void   *vs;
    void   *fs;
    DWORD   color_formats[4];
    DWORD   depth_format;
    int     depth_write;
    rbDepthFunc_t depth_test;
    struct {
        int enable;
        DWORD write_mask;
        rbBlendFactor_t src;
        rbBlendFactor_t dst;
    } blend;
    rbCull_t cull;
    DWORD   program;
} pipelineDesc_t;

typedef struct pipeline_s {
    pipelineDesc_t desc;
    DWORD          program;
} pipeline_t;

void RB_Init(void);
void RB_State(uint32_t bits);
void RB_Enable(GLenum cap);
void RB_Disable(GLenum cap);
void RB_Cull(GLenum mode);
void RB_PolygonOffset(float scale, float bias);
void RB_BlendEquation(GLenum eq);
void RB_BlendFunc(GLenum src, GLenum dst);
void RB_DepthMask(GLboolean mask);
void RB_DepthFunc(GLenum func);
void RB_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void RB_Scissor(GLint x, GLint y, GLsizei w, GLsizei h);
void RB_SetViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void RB_BindVAO(DWORD vao);
void RB_BindFBO(DWORD fbo);
void RB_BindPipeline(const pipeline_t *p);

extern backEndState_t rb;

#endif
