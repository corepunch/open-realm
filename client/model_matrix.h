/* client/model_matrix.h — shared model viewport projection for client HUD frames. */
#ifndef model_matrix_h
#define model_matrix_h

#include "client/tr_public.h"

/* Model positions are normalized viewport anchors; widening preserves authored vertical scale. */
static inline void M_ModelMatrix(LPCUIMODEL model, FLOAT aspect, LPMATRIX4 out) {
    MATRIX4 proj, view, local;
    VECTOR3 dir = Vector3_sub(&model->target, &model->eye);
    FLOAT model_aspect = (model->aspect > 0.0f) ? model->aspect : 1.0f;
    FLOAT half_y = tanf(model->fov * (FLOAT)M_PI / 360.0f) / model_aspect;
    FLOAT half_x = half_y * aspect;
    Matrix4_lookAt(&view, &model->eye, &dir, &(VECTOR3){ 0, 0, 1 });
    if (model->projection == UI_MODEL_ORTHOGRAPHIC)
        Matrix4_ortho(&proj, -half_x, half_x, -half_y, half_y, model->znear, model->zfar);
    else
        Matrix4_perspective(&proj, model->fov, aspect, model->znear, model->zfar);
    Matrix4_identity(&local);
    Matrix4_translate(&local, &(VECTOR3){ model->pos.x * half_x, model->pos.z, model->pos.y * half_y });
    Matrix4_scale(&local, &model->scale);
    Matrix4_multiply(&view, &local, out);
    Matrix4_multiply(&proj, out, &local);
    *out = local;
}

#endif
