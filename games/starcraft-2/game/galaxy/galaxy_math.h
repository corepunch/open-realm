/* galaxy_math.h — math natives */
static uint32_t sc2_AbsF(jass_t *j)  { float x = jass_checknumber(j, 1); return jass_pushnumber(j, x < 0 ? -x : x); }
static uint32_t sc2_MaxF(jass_t *j)  { float a = jass_checknumber(j,1), b = jass_checknumber(j,2); return jass_pushnumber(j, a > b ? a : b); }
static uint32_t sc2_MinF(jass_t *j)  { float a = jass_checknumber(j,1), b = jass_checknumber(j,2); return jass_pushnumber(j, a < b ? a : b); }
static uint32_t sc2_ModF(jass_t *j)  { float a = jass_checknumber(j,1), b = jass_checknumber(j,2); return jass_pushnumber(j, b ? fmodf(a, b) : 0.0f); }
static uint32_t sc2_Pow(jass_t *j)   { return jass_pushnumber(j, powf(jass_checknumber(j,1), jass_checknumber(j,2))); }
static uint32_t sc2_SquareRoot(jass_t *j) { float x = jass_checknumber(j,1); return jass_pushnumber(j, x > 0.0f ? sqrtf(x) : 0.0f); }
static uint32_t sc2_Sin(jass_t *j)   { return jass_pushnumber(j, sinf(jass_checknumber(j,1))); }
static uint32_t sc2_Cos(jass_t *j)   { return jass_pushnumber(j, cosf(jass_checknumber(j,1))); }
static uint32_t sc2_Tan(jass_t *j)   { return jass_pushnumber(j, tanf(jass_checknumber(j,1))); }
static uint32_t sc2_ASin(jass_t *j)  { return jass_pushnumber(j, asinf(jass_checknumber(j,1))); }
static uint32_t sc2_ACos(jass_t *j)  { return jass_pushnumber(j, acosf(jass_checknumber(j,1))); }
static uint32_t sc2_ATan(jass_t *j)  { return jass_pushnumber(j, atanf(jass_checknumber(j,1))); }
static uint32_t sc2_ATan2(jass_t *j) { return jass_pushnumber(j, atan2f(jass_checknumber(j,1), jass_checknumber(j,2))); }
static uint32_t sc2_RandomFixed(jass_t *j) {
    float lo = jass_checknumber(j, 1), hi = jass_checknumber(j, 2);
    float r = lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
    return jass_pushnumber(j, r);
}
static uint32_t sc2_RandomInt(jass_t *j) {
    int32_t lo = jass_checkinteger(j, 1), hi = jass_checkinteger(j, 2);
    int32_t r = hi > lo ? lo + rand() % (hi - lo + 1) : lo;
    return jass_pushinteger(j, r);
}
