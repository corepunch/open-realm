#include "r_local.h"

backEndState_t rb;

static GLenum RB_DepthFuncToGL(rbDepthFunc_t f) {
    switch (f) {
    case RB_DEPTH_NEVER: return GL_NEVER;
    case RB_DEPTH_LESS: return GL_LESS;
    case RB_DEPTH_EQUAL: return GL_EQUAL;
    case RB_DEPTH_LEQUAL: return GL_LEQUAL;
    case RB_DEPTH_GREATER: return GL_GREATER;
    case RB_DEPTH_NOTEQUAL: return GL_NOTEQUAL;
    case RB_DEPTH_GEQUAL: return GL_GEQUAL;
    case RB_DEPTH_ALWAYS: return GL_ALWAYS;
    }
    return GL_LEQUAL;
}

static uint32_t RB_PackDepthFunc(GLenum func) {
    rbDepthFunc_t f = RB_DEPTH_LEQUAL;
    switch (func) {
    case GL_NEVER: f = RB_DEPTH_NEVER; break;
    case GL_LESS: f = RB_DEPTH_LESS; break;
    case GL_EQUAL: f = RB_DEPTH_EQUAL; break;
    case GL_LEQUAL: f = RB_DEPTH_LEQUAL; break;
    case GL_GREATER: f = RB_DEPTH_GREATER; break;
    case GL_NOTEQUAL: f = RB_DEPTH_NOTEQUAL; break;
    case GL_GEQUAL: f = RB_DEPTH_GEQUAL; break;
    case GL_ALWAYS: f = RB_DEPTH_ALWAYS; break;
    }
    return ((uint32_t)f) << GLS_DEPTHFUNC_SHIFT;
}

static uint32_t RB_PackBlend(GLenum factor) {
    rbBlendFactor_t f = RB_BLEND_ONE;
    switch (factor) {
    case GL_ZERO: f = RB_BLEND_ZERO; break;
    case GL_ONE: f = RB_BLEND_ONE; break;
    case GL_SRC_COLOR: f = RB_BLEND_SRC_COLOR; break;
    case GL_ONE_MINUS_SRC_COLOR: f = RB_BLEND_ONE_MINUS_SRC_COLOR; break;
    case GL_DST_COLOR: f = RB_BLEND_DST_COLOR; break;
    case GL_ONE_MINUS_DST_COLOR: f = RB_BLEND_ONE_MINUS_DST_COLOR; break;
    case GL_SRC_ALPHA: f = RB_BLEND_SRC_ALPHA; break;
    case GL_ONE_MINUS_SRC_ALPHA: f = RB_BLEND_ONE_MINUS_SRC_ALPHA; break;
    case GL_DST_ALPHA: f = RB_BLEND_DST_ALPHA; break;
    case GL_ONE_MINUS_DST_ALPHA: f = RB_BLEND_ONE_MINUS_DST_ALPHA; break;
    case GL_SRC_ALPHA_SATURATE: f = RB_BLEND_SRC_ALPHA_SATURATE; break;
    }
    return (uint32_t)f;
}

static void RB_Ensure(void) {
    if (rb.initialized)
        return;
    rb.glStateBits = 0xffffffffu;
    rb.faceCulling = (GLenum)0xffffffffu;
    rb.depthFunc = (GLenum)0xffffffffu;
    rb.blendSrc = (GLenum)0xffffffffu;
    rb.blendDst = (GLenum)0xffffffffu;
    rb.blendEquation = (GLenum)0xffffffffu;
    rb.polygonOffsetScale = 1e30f;
    rb.polygonOffsetBias = 1e30f;
    rb.currentVAO = 0xffffffffu;
    rb.currentFBO = 0xffffffffu;
    rb.currentPipeline = 0xffffffffu;
    rb.scissor[2] = -1;
    rb.viewport[2] = -1;
    rb.colorMask[0] = rb.colorMask[1] = rb.colorMask[2] = rb.colorMask[3] = (GLboolean)0xff;
    rb.initialized = 1;
}

void RB_Init(void) {
    rb.initialized = 0;
    RB_Ensure();
    RB_State(GLS_DEFAULT);
    RB_DepthFunc(GL_LEQUAL);
    RB_BlendFunc(GL_ONE, GL_ZERO);
    RB_BlendEquation(GL_FUNC_ADD);
    RB_Cull(GL_BACK);
    RB_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void RB_State(uint32_t bits) {
    uint32_t diff;

    RB_Ensure();
    diff = bits ^ rb.glStateBits;
    if (!diff)
        return;

    if (diff & GLS_DEPTHTEST) {
        if (bits & GLS_DEPTHTEST)
            R_Call(glEnable, GL_DEPTH_TEST);
        else
            R_Call(glDisable, GL_DEPTH_TEST);
    }
    if (diff & GLS_DEPTHMASK) {
        R_Call(glDepthMask, (bits & GLS_DEPTHMASK) ? GL_TRUE : GL_FALSE);
    }
    if (diff & GLS_BLEND) {
        if (bits & GLS_BLEND)
            R_Call(glEnable, GL_BLEND);
        else
            R_Call(glDisable, GL_BLEND);
    }
    if (diff & GLS_CULL) {
        if (bits & GLS_CULL)
            R_Call(glEnable, GL_CULL_FACE);
        else
            R_Call(glDisable, GL_CULL_FACE);
    }
    if (diff & GLS_SCISSOR) {
        if (bits & GLS_SCISSOR)
            R_Call(glEnable, GL_SCISSOR_TEST);
        else
            R_Call(glDisable, GL_SCISSOR_TEST);
    }
    if (diff & GLS_POLYGON_OFFSET) {
        if (bits & GLS_POLYGON_OFFSET)
            R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
        else
            R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    }
    if (diff & GLS_A2C) {
        if (bits & GLS_A2C)
            R_Call(glEnable, GL_SAMPLE_ALPHA_TO_COVERAGE);
        else
            R_Call(glDisable, GL_SAMPLE_ALPHA_TO_COVERAGE);
    }
    rb.glStateBits = bits;
}

void RB_Enable(GLenum cap) {
    RB_Ensure();
    switch (cap) {
    case GL_DEPTH_TEST: RB_State(rb.glStateBits | GLS_DEPTHTEST); break;
    case GL_BLEND: RB_State(rb.glStateBits | GLS_BLEND); break;
    case GL_CULL_FACE: RB_State(rb.glStateBits | GLS_CULL); break;
    case GL_SCISSOR_TEST: RB_State(rb.glStateBits | GLS_SCISSOR); break;
    case GL_POLYGON_OFFSET_FILL: RB_State(rb.glStateBits | GLS_POLYGON_OFFSET); break;
    case GL_SAMPLE_ALPHA_TO_COVERAGE: RB_State(rb.glStateBits | GLS_A2C); break;
    default: R_Call(glEnable, cap); break;
    }
}

void RB_Disable(GLenum cap) {
    RB_Ensure();
    switch (cap) {
    case GL_DEPTH_TEST: RB_State(rb.glStateBits & ~GLS_DEPTHTEST); break;
    case GL_BLEND: RB_State(rb.glStateBits & ~GLS_BLEND); break;
    case GL_CULL_FACE: RB_State(rb.glStateBits & ~GLS_CULL); break;
    case GL_SCISSOR_TEST: RB_State(rb.glStateBits & ~GLS_SCISSOR); break;
    case GL_POLYGON_OFFSET_FILL: RB_State(rb.glStateBits & ~GLS_POLYGON_OFFSET); break;
    case GL_SAMPLE_ALPHA_TO_COVERAGE: RB_State(rb.glStateBits & ~GLS_A2C); break;
    default: R_Call(glDisable, cap); break;
    }
}

void RB_Cull(GLenum mode) {
    RB_Ensure();
    if (mode == 0) {
        RB_Disable(GL_CULL_FACE);
        return;
    }
    RB_Enable(GL_CULL_FACE);
    if (rb.faceCulling != mode) {
        R_Call(glCullFace, mode);
        rb.faceCulling = mode;
    }
}

void RB_PolygonOffset(float scale, float bias) {
    RB_Ensure();
    if (rb.polygonOffsetScale != scale || rb.polygonOffsetBias != bias) {
        R_Call(glPolygonOffset, scale, bias);
        rb.polygonOffsetScale = scale;
        rb.polygonOffsetBias = bias;
    }
}

void RB_BlendEquation(GLenum eq) {
    RB_Ensure();
    if (rb.blendEquation != eq) {
        R_Call(glBlendEquation, eq);
        rb.blendEquation = eq;
    }
}

void RB_BlendFunc(GLenum src, GLenum dst) {
    uint32_t bits;
    RB_Ensure();
    if (rb.blendSrc == src && rb.blendDst == dst)
        return;
    R_Call(glBlendFunc, src, dst);
    rb.blendSrc = src;
    rb.blendDst = dst;
    bits = rb.glStateBits;
    bits &= ~(GLS_SRCBLEND_MASK | GLS_DSTBLEND_MASK);
    bits |= (RB_PackBlend(src) << GLS_SRCBLEND_SHIFT);
    bits |= (RB_PackBlend(dst) << GLS_DSTBLEND_SHIFT);
    rb.glStateBits = bits;
}

void RB_DepthMask(GLboolean mask) {
    RB_Ensure();
    if (mask)
        RB_State(rb.glStateBits | GLS_DEPTHMASK);
    else
        RB_State(rb.glStateBits & ~GLS_DEPTHMASK);
}

void RB_DepthFunc(GLenum func) {
    uint32_t bits;
    RB_Ensure();
    if (rb.depthFunc == func)
        return;
    R_Call(glDepthFunc, func);
    rb.depthFunc = func;
    bits = rb.glStateBits & ~GLS_DEPTHFUNC_MASK;
    bits |= RB_PackDepthFunc(func);
    rb.glStateBits = bits;
}

void RB_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    uint32_t bits;
    RB_Ensure();
    if (rb.colorMask[0] == r && rb.colorMask[1] == g && rb.colorMask[2] == b && rb.colorMask[3] == a)
        return;
    R_Call(glColorMask, r, g, b, a);
    rb.colorMask[0] = r;
    rb.colorMask[1] = g;
    rb.colorMask[2] = b;
    rb.colorMask[3] = a;
    bits = rb.glStateBits & ~GLS_COLORMASK_MASK;
    bits |= ((r ? 1u : 0u) | (g ? 2u : 0u) | (b ? 4u : 0u) | (a ? 8u : 0u)) << GLS_COLORMASK_SHIFT;
    rb.glStateBits = bits;
}

void RB_Scissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    RB_Ensure();
    if (rb.scissor[0] == x && rb.scissor[1] == y && rb.scissor[2] == w && rb.scissor[3] == h)
        return;
    R_Call(glScissor, x, y, w, h);
    rb.scissor[0] = x;
    rb.scissor[1] = y;
    rb.scissor[2] = w;
    rb.scissor[3] = h;
}

void RB_SetViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    RB_Ensure();
    if (rb.viewport[0] == x && rb.viewport[1] == y && rb.viewport[2] == w && rb.viewport[3] == h)
        return;
    R_Call(glViewport, x, y, w, h);
    rb.viewport[0] = x;
    rb.viewport[1] = y;
    rb.viewport[2] = w;
    rb.viewport[3] = h;
}

void RB_BindVAO(DWORD vao) {
    RB_Ensure();
    if (rb.currentVAO == vao)
        return;
    R_Call(glBindVertexArray, vao);
    rb.currentVAO = vao;
}

void RB_BindFBO(DWORD fbo) {
    RB_Ensure();
    if (rb.currentFBO == fbo)
        return;
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, fbo);
    rb.currentFBO = fbo;
}

void RB_BindPipeline(const pipeline_t *p) {
    RB_Ensure();
    if (!p)
        return;
    if (rb.currentPipeline != p->program) {
        R_Call(glUseProgram, p->program);
        rb.currentPipeline = p->program;
    }
    if (p->desc.depth_write)
        RB_DepthMask(GL_TRUE);
    else
        RB_DepthMask(GL_FALSE);
    RB_DepthFunc(RB_DepthFuncToGL(p->desc.depth_test));
    if (p->desc.blend.enable)
        RB_Enable(GL_BLEND);
    else
        RB_Disable(GL_BLEND);
    switch (p->desc.cull) {
    case RB_CULL_NONE: RB_Disable(GL_CULL_FACE); break;
    case RB_CULL_FRONT: RB_Cull(GL_FRONT); break;
    default: RB_Cull(GL_BACK); break;
    }
}
