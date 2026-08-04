#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "../tools/tool_common.h"

TEST(tool_common, normalize_slashes_to_backslashes) {
    char path[64] = "UI/FrameDef\\Glue/MainMenu.fdf";

    Tool_NormalizeSlashes(path, '\\');

    T_STREQ(path, "UI\\FrameDef\\Glue\\MainMenu.fdf");
}

TEST(tool_common, trim_edge_slashes_removes_both_sides) {
    char path[64] = "\\UI/FrameDef/Glue/MainMenu.fdf/";

    Tool_TrimEdgeSlashes(path);

    T_STREQ(path, "UI/FrameDef/Glue/MainMenu.fdf");
}

TEST(tool_common, path_join_uses_forward_slash) {
    char *joined = Tool_PathJoin("UI/FrameDef", "Glue/MainMenu.fdf");

    T_NOT_NULL(joined);
    if (joined) {
        T_STREQ(joined, "UI/FrameDef/Glue/MainMenu.fdf");
        free(joined);
    }
}

TEST(tool_common, path_parent_and_basename_follow_archive_paths) {
    char *parent = Tool_PathParent("UI\\FrameDef\\Glue\\MainMenu.fdf");

    T_NOT_NULL(parent);
    if (parent) {
        T_STREQ(parent, "UI/FrameDef/Glue");
        free(parent);
    }

    T_STREQ(Tool_PathBasename("UI\\FrameDef\\Glue\\MainMenu.fdf"), "MainMenu.fdf");
    T_STREQ(Tool_PathExt("UI\\FrameDef\\Glue\\MainMenu.fdf"), "fdf");
}
