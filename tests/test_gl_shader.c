#include "test.h"
#include "renderer/r_local.h"
#include "vendor/gl_shader/test_contract.h"

TEST(gl_shader, portable_contract) { T_ASSERT(gs_test_contract()); }
