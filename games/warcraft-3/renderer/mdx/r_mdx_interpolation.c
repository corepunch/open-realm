#include "renderer/r_local.h"
#include "r_mdx.h"

enum {
    KEY_VALUE,
    KEY_INTAN,
    KEY_OUTTAN,
};

DWORD GetModelKeyTrackDataTypeSize(MODELKEYTRACKDATATYPE dataType) {
    switch (dataType) {
        case TDATA_INT1: return 4;
        case TDATA_FLOAT1: return 4;
        case TDATA_FLOAT3: return 12;
        case TDATA_FLOAT4: return 16;
        default: return 0;
    }
}

DWORD GetModelKeyTrackTypeSize(MODELKEYTRACKTYPE keyTrackType) {
    switch (keyTrackType) {
        case TRACK_NO_INTERP: return 1;
        case TRACK_LINEAR: return 1;
        case TRACK_HERMITE: return 3;
        case TRACK_BEZIER: return 3;
        default: return 0;
    }
}

DWORD GetModelKeyFrameSize(MODELKEYTRACKDATATYPE dataType, MODELKEYTRACKTYPE keyTrackType) {
    return 4 + GetModelKeyTrackDataTypeSize(dataType) * GetModelKeyTrackTypeSize(keyTrackType);
}

static float lerp(float left, float right, float t) {
    return left * (1 - t) + right * t;
}

static float bezier(float left, float outTan, float inTan, float right, float t) {
    float const inverseFactor = 1 - t,
        inverseFactorTimesTwo = inverseFactor * inverseFactor,
        factorTimes2 = t * t,
        factor1 = inverseFactorTimesTwo * inverseFactor,
        factor2 = 3 * t * inverseFactorTimesTwo,
        factor3 = 3 * factorTimes2 * inverseFactor,
        factor4 = factorTimes2 * t;
    return left * factor1 + outTan * factor2 + inTan * factor3 + right * factor4;
}

static float hermite(float left, float outTan, float inTan, float right, float t) {
    float const factorTimes2 = t * t,
        factor1 = factorTimes2 * (2 * t - 3) + 1,
        factor2 = factorTimes2 * (t - 2) + t,
        factor3 = factorTimes2 * (t - 1),
        factor4 = factorTimes2 * (3 - 2 * t);
    return left * factor1 + outTan * factor2 + inTan * factor3 + right * factor4;
}

static int
interpInt(int const *left,
          int const *right,
          float t,
          MODELKEYTRACKTYPE lineType)
{
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return bezier(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        case TRACK_HERMITE: return hermite(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        default: return lerp(left[KEY_VALUE], right[KEY_VALUE], t);
    }
}

static float
interpFloat(float const *left,
            float const *right,
            float t,
            MODELKEYTRACKTYPE lineType)
{
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return bezier(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        case TRACK_HERMITE: return hermite(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        default: return lerp(left[KEY_VALUE], right[KEY_VALUE], t);
    }
}

static VECTOR3
interpVec3(LPCVECTOR3 left,
           LPCVECTOR3 right,
           float t,
           MODELKEYTRACKTYPE lineType)
{
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return Vector3_bezier(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        case TRACK_HERMITE: return Vector3_hermite(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        default: return Vector3_lerp(&left[KEY_VALUE], &right[KEY_VALUE], t);
    }
}

static QUATERNION
interpQuat(LPCQUATERNION left,
           LPCQUATERNION right,
           float t,
           MODELKEYTRACKTYPE lineType)
{
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER:
        case TRACK_HERMITE: return Quaternion_sqlerp(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        default: return Quaternion_slerp(&left[KEY_VALUE], &right[KEY_VALUE], t);
    }
}

void
R_EvalKeyframeValue(void const *left,
                    void const *right,
                    float t,
                    MODELKEYTRACKDATATYPE datatype,
                    MODELKEYTRACKTYPE linetype,
                    HANDLE out)
{
    switch (datatype) {
        case TDATA_INT1: *((int *)out) = interpInt(left, right, t, linetype); return;
        case TDATA_FLOAT1: *((float *)out) = interpFloat(left, right, t, linetype); return;
        case TDATA_FLOAT3: *((VECTOR3 *)out) = interpVec3(left, right, t, linetype); return;
        case TDATA_FLOAT4: *((QUATERNION *)out) = interpQuat(left, right, t, linetype); return;
    }
}

void
R_GetKeyframeValue(mdxKeyFrame_t const *left,
                   mdxKeyFrame_t const *right,
                   mdxKeyTrack_t const *keytrack,
                   DWORD time,
                   HANDLE out)
{
    if (right->time == left->time) {
        memcpy(out, left->data, GetModelKeyTrackDataTypeSize(keytrack->datatype));
        return;
    }
    float const t = (float)(time - left->time) / (float)(right->time - left->time);
    R_EvalKeyframeValue(left->data, right->data, t, keytrack->datatype, keytrack->linetype, out);
}
