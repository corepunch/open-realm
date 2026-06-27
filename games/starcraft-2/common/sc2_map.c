#include "sc2_map.h"
#include "sc2_utils.h"
#include "common/mpq.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stddef.h>
#include <stdlib.h>

#define SC2_MAX_CATALOG_MODELS       8192
#define SC2_MAX_CATALOG_ACTORS       8192
#define SC2_MAX_CATALOG_UNITS        8192
#define SC2_MAX_CATALOG_FOOTPRINTS   1024
#define SC2_MAX_CATALOG_TERRAIN_TEX  512
#define SC2_MAX_CATALOG_CLIFFS       256
#define SC2_MAX_CATALOG_PARENT_DEPTH 8
#define SC2_MAX_CATALOG_INCLUDE_DEPTH 8
#define SC2_ARRAY_LEN(x)             ((DWORD)(sizeof(x) / sizeof((x)[0])))
#define SC2_XML_STRING_FIELD(name, field) { name, offsetof(sc2MapObject_t, field), SC2_XML_FIELD_STRING, sizeof(((sc2MapObject_t *)0)->field) }
#define SC2_XML_FIELD(name, field, type)  { name, offsetof(sc2MapObject_t, field), type, 0 }
#define SC2_TERRAIN_XML_STRING_FIELD(name, field) { name, offsetof(sc2MapTerrain_t, field), SC2_XML_FIELD_STRING, sizeof(((sc2MapTerrain_t *)0)->field) }
#define SC2_TERRAIN_XML_FIELD(name, field, type)  { name, offsetof(sc2MapTerrain_t, field), type, 0 }
#define SC2_LIGHTING_XML_FIELD(name, field, type) { name, offsetof(sc2MapLighting_t, field), type, 0 }
#define SC2_DIRECTIONAL_LIGHT_XML_FIELD(name, field, type) { name, offsetof(sc2DirectionalLight_t, field), type, 0 }

typedef struct {
    HANDLE archive;
    HANDLE archive_data;
    char   base[MAX_PATHLEN];
} sc2MapSource_t;

typedef struct {
    char id[64];
    char parent[64];
    char race[64];
    char path[256];
} sc2CatalogModel_t;

typedef struct {
    char id[64];
    char model[64];
    char footprint[64];
} sc2CatalogActor_t;

typedef struct {
    char id[64];
    char actor[64];
    char footprint[64];
    char mover[64];
    DWORD flags;
    BOOL has_radius;
    FLOAT radius;
} sc2CatalogUnit_t;

typedef struct {
    char name[64];
    char model[256];
    char footprint[64];
    char mover[64];
    FLOAT radius;
    FLOAT footprint_width;
    FLOAT footprint_height;
    FLOAT footprint_radius;
    DWORD unit_flags;
} sc2ResolvedObjectModel_t;

typedef struct {
    char id[64];
    FLOAT width;
    FLOAT height;
    FLOAT radius;
} sc2CatalogFootprint_t;

typedef struct {
    char id[64];
    char diffuse[256];
    char normal[256];
} sc2CatalogTerrainTex_t;

typedef struct {
    char id[64];
    char mesh[64];
} sc2CatalogCliff_t;

typedef struct {
    DWORD models_count;
    DWORD actors_count;
    DWORD units_count;
    DWORD footprints_count;
    DWORD terrain_tex_count;
    DWORD cliffs_count;
    sc2CatalogModel_t models[SC2_MAX_CATALOG_MODELS];
    sc2CatalogActor_t actors[SC2_MAX_CATALOG_ACTORS];
    sc2CatalogUnit_t units[SC2_MAX_CATALOG_UNITS];
    sc2CatalogFootprint_t footprints[SC2_MAX_CATALOG_FOOTPRINTS];
    sc2CatalogTerrainTex_t terrain_tex[SC2_MAX_CATALOG_TERRAIN_TEX];
    sc2CatalogCliff_t cliffs[SC2_MAX_CATALOG_CLIFFS];
} sc2Catalog_t;

typedef enum {
    SC2_XML_FIELD_DWORD,
    SC2_XML_FIELD_FLOAT,
    SC2_XML_FIELD_STRING,
    SC2_XML_FIELD_VEC3,
    SC2_XML_FIELD_COLOR_ARGB,
    SC2_XML_FIELD_COLOR_RGBA,
} sc2XmlFieldType_t;

typedef struct {
    LPCSTR            name;
    size_t            offset;
    sc2XmlFieldType_t type;
    DWORD             size;
} sc2XmlField_t;

typedef struct {
    LPCSTR name;
    DWORD  flag;
} sc2XmlFlag_t;

typedef struct {
    LPCSTR               node_name;
    sc2XmlField_t const *fields;
    DWORD                num_fields;
} sc2XmlNodeFields_t;

static sc2MapHost_t sc2_host;
static sc2Map_t     sc2_map;

static LPCSTR const sc2_catalog_roots[] = {
    "Mods/Core.SC2Mod/Base.SC2Data",
    "Mods/Liberty.SC2Mod/Base.SC2Data",
    "Mods/LibertyMulti.SC2Mod/Base.SC2Data",
    "Campaigns/LibertyStory.SC2Campaign/Base.SC2Data",
    "Campaigns/Liberty.SC2Campaign/Base.SC2Data",
    NULL,
};

static LPCSTR const sc2_catalog_known_files[] = {
    "GameData\\UnitData.xml",
    "GameData\\ModelData.xml",
    "GameData\\ActorData.xml",
    "GameData\\FootprintData.xml",
    "GameData\\TerrainTexData.xml",
    "GameData\\CliffData.xml",
    NULL,
};

static BOOL sc2_mapinfo_fourcc(sc2MapInfo_t const *mapInfo);
static BOOL sc2_parse_xml_field(void *base, sc2XmlField_t const *fields, DWORD num_fields, LPCSTR name, LPCSTR value);

static HANDLE sc2_alloc(long size) {
    return sc2_host.mem_alloc ? sc2_host.mem_alloc(size) : NULL;
}

static void sc2_free(HANDLE mem) {
    if (!mem) return;
    if (sc2_host.mem_free) sc2_host.mem_free(mem);
}

static HANDLE sc2_read_file(LPCSTR filename, LPDWORD size) {
    return sc2_host.read_file ? sc2_host.read_file(filename, size) : NULL;
}

static void sc2_free_file(HANDLE file) {
    if (!file) return;
    if (sc2_host.free_file) sc2_host.free_file(file);
}

static BOOL sc2_file_exists(LPCSTR path) {
    DWORD size = 0;
    HANDLE data;

    if (!path || !*path) return false;
    data = sc2_read_file(path, &size);
    if (data) sc2_free_file(data);
    return data && size > 0;
}

static void sc2_map_clear(void) {
    SAFE_DELETE(sc2_map.t3CellFlags, sc2_free);
    SAFE_DELETE(sc2_map.t3SyncCliffLevel, sc2_free);
    SAFE_DELETE(sc2_map.t3HeightMap, sc2_free);
    SAFE_DELETE(sc2_map.t3SyncHeightMap, sc2_free);
    SAFE_DELETE(sc2_map.t3TextureMasks, sc2_free);
    memset(&sc2_map, 0, sizeof(sc2_map));
}

static sc2MapInfo_t *sc2_ensure_mapinfo(void) {
    if (!sc2_map.MapInfo.fourcc)
        sc2_map.MapInfo.fourcc = MAKEFOURCC('M','a','p','I');
    return &sc2_map.MapInfo;
}

static DWORD sc2_map_width(void) {
    return sc2_map.MapInfo.width;
}

static DWORD sc2_map_height(void) {
    return sc2_map.MapInfo.height;
}

static BOOL sc2_source_open(sc2MapSource_t *source, LPCSTR mapFilename) {
    DWORD size = 0;
    memset(source, 0, sizeof(*source));
    if (mapFilename && *mapFilename) {
        source->archive_data = sc2_read_file(mapFilename, &size);
        if (source->archive_data && size > 0 &&
            SFileOpenArchiveFromMemory(source->archive_data, size, 0, &source->archive)) {
            return true;
        }
        snprintf(source->base, sizeof(source->base), "%s", mapFilename);
    }
    return true;
}

static void sc2_source_close(sc2MapSource_t *source) {
    if (source->archive) SFileCloseArchive(source->archive);
    sc2_free_file(source->archive_data);
    memset(source, 0, sizeof(*source));
}

static HANDLE sc2_source_read(sc2MapSource_t *source, LPCSTR filename, LPDWORD size) {
    PATHSTR path;
    HANDLE file;
    DWORD file_size;
    LPBYTE data;

    if (size) *size = 0;
    if (!source || !filename || !*filename) return NULL;

    if (source->archive) {
        if (!SFileOpenFileEx(source->archive, filename, SFILE_OPEN_FROM_MPQ, &file)) {
            snprintf(path, sizeof(path), "%s", filename);
            for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
            if (!SFileOpenFileEx(source->archive, path, SFILE_OPEN_FROM_MPQ, &file))
                return NULL;
        }
        file_size = SFileGetFileSize(file, NULL);
        data = sc2_alloc(file_size + 1);
        if (!SFileReadFile(file, data, file_size, NULL, NULL)) {
            SFileCloseFile(file);
            sc2_free(data);
            return NULL;
        }
        SFileCloseFile(file);
        data[file_size] = 0;
        if (size) *size = file_size;
        return data;
    }

    if (!source->base[0])
        return sc2_read_file(filename, size);
    snprintf(path, sizeof(path), "%s/%s", source->base, filename);
    data = sc2_read_file(path, size);
    if (!data) {
        snprintf(path, sizeof(path), "%s\\%s", source->base, filename);
        data = sc2_read_file(path, size);
    }
    return data;
}

static xmlDocPtr sc2_read_xml(sc2MapSource_t *source, LPCSTR filename) {
    DWORD size = 0;
    LPBYTE data = sc2_source_read(source, filename, &size);
    xmlDocPtr doc = NULL;
    if (data && size > 0) {
        doc = xmlReadMemory((char const *)data, (int)size, filename, NULL,
                            XML_PARSE_RECOVER | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    }
    sc2_free_file(data);
    return doc;
}

static xmlDocPtr sc2_read_global_xml(LPCSTR filename) {
    DWORD size = 0;
    LPBYTE data = sc2_read_file(filename, &size);
    xmlDocPtr doc = NULL;

    if (!data && filename) {
        PATHSTR path;
        snprintf(path, sizeof(path), "%s", filename);
        for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
        data = sc2_read_file(path, &size);
    }
    if (data && size > 0) {
        doc = xmlReadMemory((char const *)data, (int)size, filename, NULL,
                            XML_PARSE_RECOVER | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    }
    sc2_free_file(data);
    return doc;
}

static LPBYTE sc2_read_disk_file(LPCSTR filename, LPDWORD size) {
    FILE *file;
    long file_size;
    LPBYTE data;

    if (size) *size = 0;
    if (!filename || !*filename)
        return NULL;
    file = fopen(filename, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = sc2_alloc(file_size + 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (file_size > 0 && fread(data, 1, file_size, file) != (size_t)file_size) {
        fclose(file);
        sc2_free(data);
        return NULL;
    }
    fclose(file);
    data[file_size] = 0;
    if (size) *size = (DWORD)file_size;
    return data;
}

static xmlDocPtr sc2_read_catalog_xml_from_archive(LPCSTR archive_name, LPCSTR filename) {
    DWORD archive_size = 0;
    DWORD file_size;
    LPBYTE archive_data;
    LPBYTE data;
    HANDLE archive;
    HANDLE file;
    xmlDocPtr doc = NULL;
    PATHSTR path;

    if (!archive_name || !*archive_name || !filename || !*filename)
        return NULL;
    archive_data = sc2_read_disk_file(archive_name, &archive_size);
    if (!archive_data || archive_size == 0 ||
        !SFileOpenArchiveFromMemory(archive_data, archive_size, 0, &archive)) {
        sc2_free(archive_data);
        return NULL;
    }
    if (!SFileOpenFileEx(archive, filename, SFILE_OPEN_FROM_MPQ, &file)) {
        snprintf(path, sizeof(path), "%s", filename);
        for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
        if (!SFileOpenFileEx(archive, path, SFILE_OPEN_FROM_MPQ, &file)) {
            SFileCloseArchive(archive);
            sc2_free(archive_data);
            return NULL;
        }
    }
    file_size = SFileGetFileSize(file, NULL);
    data = sc2_alloc(file_size + 1);
    if (data && SFileReadFile(file, data, file_size, NULL, NULL)) {
        data[file_size] = 0;
        doc = xmlReadMemory((char const *)data, (int)file_size, filename, NULL,
                            XML_PARSE_RECOVER | XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    }
    sc2_free(data);
    SFileCloseFile(file);
    SFileCloseArchive(archive);
    sc2_free(archive_data);
    return doc;
}

static xmlDocPtr sc2_read_catalog_xml(LPCSTR root, LPCSTR filename) {
    PATHSTR path;
    xmlDocPtr doc;
    LPCSTR data_dir;

    if (!root || !*root) return sc2_read_global_xml(filename);
    snprintf(path, sizeof(path), "%s/%s", root, filename);
    doc = sc2_read_global_xml(path);
    if (doc)
        return doc;
    data_dir = sc2_host.cvar_string ? sc2_host.cvar_string("data", "") : "";
    if (data_dir && *data_dir) {
        snprintf(path, sizeof(path), "%s/%s", data_dir, root);
        return sc2_read_catalog_xml_from_archive(path, filename);
    }
    return sc2_read_catalog_xml_from_archive(root, filename);
}

static xmlDocPtr sc2_read_map_catalog_xml(sc2MapSource_t *source, LPCSTR filename) {
    PATHSTR path;
    xmlDocPtr doc;

    snprintf(path, sizeof(path), "Base.SC2Data/%s", filename);
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    doc = sc2_read_xml(source, path);
    if (doc)
        return doc;
    snprintf(path, sizeof(path), "Base.SC2Data\\%s", filename);
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
    return sc2_read_xml(source, path);
}

static BOOL sc2_catalog_include_path(LPCSTR in, LPSTR out, DWORD out_size) {
    char path[MAX_PATHLEN];
    DWORD len;

    if (!in || !*in || !out || !out_size)
        return false;
    if (strlen(in) >= sizeof(path) || strlen(in) >= out_size)
        return false;
    if (in[0] == '/' || in[0] == '\\' || strchr(in, ':'))
        return false;
    snprintf(path, sizeof(path), "%s", in);
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    len = (DWORD)strlen(path);
    if (!len)
        return false;
    for (DWORD i = 0; i <= len; ) {
        DWORD start = i;

        while (i < len && path[i] != '/') i++;
        if (i == start || (i == start + 1 && path[start] == '.') ||
            (i == start + 2 && path[start] == '.' && path[start + 1] == '.'))
            return false;
        i++;
    }
    snprintf(out, out_size, "%s", path);
    for (char *p = out; *p; p++) if (*p == '/') *p = '\\';
    return true;
}

static xmlDocPtr sc2_read_layer_catalog_xml(sc2MapSource_t *source, LPCSTR root_name, LPCSTR filename) {
    return source ? sc2_read_map_catalog_xml(source, filename) : sc2_read_catalog_xml(root_name, filename);
}

static BOOL sc2_xml_attr(xmlNodePtr node, LPCSTR attr_name, LPSTR buffer, DWORD size) {
    if (!node || !attr_name || !buffer || !size) return false;
    for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
        xmlChar *text;

        if (!sc2_streqi((char const *)attr->name, attr_name))
            continue;
        text = xmlNodeListGetString(node->doc, attr->children, 1);
        if (!text) return false;
        snprintf(buffer, size, "%s", (char const *)text);
        xmlFree(text);
        return buffer[0] != '\0';
    }
    return false;
}

static LPCSTR sc2_xml_content(xmlNodePtr node, char *buffer, DWORD size) {
    xmlChar *text;
    if (!node || !buffer || size == 0) return NULL;
    text = xmlNodeGetContent(node);
    if (!text) return NULL;
    snprintf(buffer, size, "%s", (char const *)text);
    xmlFree(text);
    return buffer;
}

static void sc2_add_terrain_texture(LPCSTR path) {
    sc2TerrainTexture_t *texture;
    char buffer[256];
    char *ext;

    if (!path || !*path || sc2_map.t3Terrain.num_terrain_textures >= SC2_MAX_TERRAIN_TEXTURES)
        return;
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (char *p = buffer; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    if (sc2_path_has_dir(buffer)) {
        sc2_append_extension(buffer, sizeof(buffer), ".dds");
    }
    FOR_LOOP(i, sc2_map.t3Terrain.num_terrain_textures) {
        if (!strcasecmp(sc2_map.t3Terrain.terrain_textures[i].diffuse, buffer)) {
            return;
        }
    }

    texture = &sc2_map.t3Terrain.terrain_textures[sc2_map.t3Terrain.num_terrain_textures++];
    snprintf(texture->diffuse, sizeof(texture->diffuse), "%s", buffer);
    snprintf(texture->normal, sizeof(texture->normal), "%s", buffer);
    ext = sc2_path_has_dir(texture->normal) ? strrchr(texture->normal, '.') : NULL;
    if (ext && sc2_has_extension_i(texture->normal, ".dds")) {
        snprintf(ext, sizeof(texture->normal) - (DWORD)(ext - texture->normal), "_normal.dds");
    }
}

static void sc2_parse_terrain_value(LPCSTR key, LPCSTR value) {
    if (!key || !value || !*value)
        return;
    if (sc2_contains_i(key, "normal")) {
        DWORD index = sc2_map.t3Terrain.num_terrain_textures ? sc2_map.t3Terrain.num_terrain_textures - 1 : 0;
        if (index < SC2_MAX_TERRAIN_TEXTURES) {
            snprintf(sc2_map.t3Terrain.terrain_textures[index].normal,
                     sizeof(sc2_map.t3Terrain.terrain_textures[index].normal),
                     "%s",
                     value);
        }
    } else if (sc2_contains_i(key, "texture") ||
               sc2_contains_i(key, "diffuse") ||
               sc2_contains_i(key, "blend") ||
               sc2_contains_i(key, "file") ||
               strstr(value, ".dds") ||
               strstr(value, ".DDS")) {
        sc2_add_terrain_texture(value);
    }
}

static BOOL sc2_parse_argb_color(LPCSTR text, LPCOLOR32 color) {
    DWORD a, r, g, b;

    if (!text || !color)
        return false;
    if (sscanf(text, "%u,%u,%u,%u", &a, &r, &g, &b) == 4) {
        *color = (COLOR32){ (BYTE)r, (BYTE)g, (BYTE)b, (BYTE)a };
        return true;
    }
    if (sscanf(text, "%u,%u,%u", &r, &g, &b) == 3) {
        *color = (COLOR32){ (BYTE)r, (BYTE)g, (BYTE)b, 255 };
        return true;
    }
    return false;
}

static BOOL sc2_parse_rgba_color(LPCSTR text, LPCOLOR32 color) {
    DWORD r, g, b, a;

    if (!text || !color)
        return false;
    if (sscanf(text, "%u,%u,%u,%u", &r, &g, &b, &a) == 4) {
        *color = (COLOR32){ (BYTE)r, (BYTE)g, (BYTE)b, (BYTE)a };
        return true;
    }
    if (sscanf(text, "%u,%u,%u", &r, &g, &b) == 3) {
        *color = (COLOR32){ (BYTE)r, (BYTE)g, (BYTE)b, 255 };
        return true;
    }
    return false;
}

static FLOAT sc2_abs_float(FLOAT value) {
    return value < 0.0f ? -value : value;
}

static BOOL sc2_parse_float_pair(LPCSTR text, FLOAT *x, FLOAT *y) {
    FLOAT a, b;

    if (!text || !x || !y) return false;
    if (sscanf(text, "%f,%f", &a, &b) != 2) return false;
    *x = a;
    *y = b;
    return true;
}

static BOOL sc2_parse_footprint_area(LPCSTR text, FLOAT *width, FLOAT *height) {
    FLOAT x0, y0, x1, y1;

    if (!text || !width || !height) return false;
    if (sscanf(text, "%f,%f,%f,%f", &x0, &y0, &x1, &y1) != 4) return false;
    *width = sc2_abs_float(x1 - x0);
    *height = sc2_abs_float(y1 - y0);
    return *width > 0.0f && *height > 0.0f;
}

static sc2XmlField_t const sc2_terrain_data_fields[] = {
    SC2_TERRAIN_XML_FIELD("FogDensity", fog_density, SC2_XML_FIELD_FLOAT),
    SC2_TERRAIN_XML_FIELD("FogFalloff", fog_falloff, SC2_XML_FIELD_FLOAT),
    SC2_TERRAIN_XML_FIELD("FogStartHeight", fog_start_height, SC2_XML_FIELD_FLOAT),
    SC2_TERRAIN_XML_FIELD("FogStartingHeight", fog_start_height, SC2_XML_FIELD_FLOAT),
    SC2_TERRAIN_XML_FIELD("FogColor", fog_color, SC2_XML_FIELD_COLOR_ARGB),
};

static sc2XmlField_t const sc2_light_data_fields[] = {
    SC2_LIGHTING_XML_FIELD("AmbientColor", ambient_color, SC2_XML_FIELD_VEC3),
};

static sc2XmlField_t const sc2_directional_light_fields[] = {
    SC2_DIRECTIONAL_LIGHT_XML_FIELD("Color", color, SC2_XML_FIELD_VEC3),
    SC2_DIRECTIONAL_LIGHT_XML_FIELD("ColorMultiplier", color_multiplier, SC2_XML_FIELD_FLOAT),
    SC2_DIRECTIONAL_LIGHT_XML_FIELD("SpecColorMultiplier", spec_color_multiplier, SC2_XML_FIELD_FLOAT),
    SC2_DIRECTIONAL_LIGHT_XML_FIELD("Direction", direction, SC2_XML_FIELD_VEC3),
};

static void sc2_parse_terrain_data_node(xmlNodePtr node, LPCSTR terrain_id) {
    char id[64];
    char value[256];

    if (!node || node->type != XML_ELEMENT_NODE)
        return;
    if (sc2_streqi((char const *)node->name, "CTerrain") && sc2_xml_attr(node, "id", id, sizeof(id)))
        terrain_id = id;
    if (terrain_id && sc2_streqi(terrain_id, sc2_map.t3Terrain.tile_set) &&
        sc2_xml_attr(node, "value", value, sizeof(value)) &&
        sc2_parse_xml_field(&sc2_map.t3Terrain,
                            sc2_terrain_data_fields,
                            SC2_ARRAY_LEN(sc2_terrain_data_fields),
                            (char const *)node->name,
                            value)) {
        sc2_map.t3Terrain.fog_enabled = true;
    }
    for (xmlNodePtr child = node->children; child; child = child->next)
        sc2_parse_terrain_data_node(child, terrain_id);
}

static int sc2_light_index(LPCSTR index) {
    if (sc2_streqi(index, "Key")) return SC2_LIGHT_KEY;
    if (sc2_streqi(index, "Fill")) return SC2_LIGHT_FILL;
    if (sc2_streqi(index, "Back")) return SC2_LIGHT_BACK;
    return -1;
}

static void sc2_init_directional_light(sc2DirectionalLight_t *light) {
    if (!light || light->enabled)
        return;
    light->enabled = true;
    light->color = (VECTOR3){ 1.0f, 1.0f, 1.0f };
    light->color_multiplier = 1.0f;
    light->spec_color_multiplier = 1.0f;
    light->direction = (VECTOR3){ 0.0f, 0.0f, -1.0f };
}

static void sc2_parse_light_data_value(xmlNodePtr node, int light_index, LPCSTR name, LPCSTR value) {
    sc2DirectionalLight_t *light;

    if (!node || !name || !value || !*value)
        return;
    if (light_index >= 0 && light_index < SC2_MAX_DIRECTIONAL_LIGHTS) {
        light = &sc2_map.lighting.directional[light_index];
        sc2_init_directional_light(light);
        sc2_parse_xml_field(light,
                            sc2_directional_light_fields,
                            SC2_ARRAY_LEN(sc2_directional_light_fields),
                            name,
                            value);
        return;
    }
    sc2_parse_xml_field(&sc2_map.lighting,
                        sc2_light_data_fields,
                        SC2_ARRAY_LEN(sc2_light_data_fields),
                        name,
                        value);
}

static void sc2_parse_light_data_node(xmlNodePtr node, LPCSTR light_id, int light_index) {
    char id[64];
    char value[256];

    if (!node || node->type != XML_ELEMENT_NODE)
        return;
    if (sc2_streqi((char const *)node->name, "CLight")) {
        if (!sc2_xml_attr(node, "id", id, sizeof(id)) || !sc2_streqi(id, sc2_map.t3Terrain.tile_set))
            return;
        snprintf(sc2_map.lighting.id, sizeof(sc2_map.lighting.id), "%s", id);
        sc2_map.lighting.enabled = true;
        light_id = sc2_map.lighting.id;
        light_index = -1;
    }
    if (light_id && sc2_streqi(light_id, sc2_map.t3Terrain.tile_set)) {
        if (sc2_streqi((char const *)node->name, "DirectionalLight") &&
            sc2_xml_attr(node, "index", id, sizeof(id))) {
            light_index = sc2_light_index(id);
        }
        for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
            xmlChar *text = xmlNodeListGetString(node->doc, attr->children, 1);
            if (text) {
                sc2_parse_light_data_value(node, light_index, (char const *)attr->name, (char const *)text);
                xmlFree(text);
            }
        }
        if (sc2_xml_attr(node, "value", value, sizeof(value)))
            sc2_parse_light_data_value(node, light_index, (char const *)node->name, value);
    }
    for (xmlNodePtr child = node->children; child; child = child->next)
        sc2_parse_light_data_node(child, light_id, light_index);
}

static void sc2_parse_cliff_set_node(xmlNodePtr node) {
    char value[64];
    DWORD index;

    if (!node || !sc2_streqi((char const *)node->name, "cliffSet"))
        return;
    if (!sc2_xml_attr(node, "i", value, sizeof(value)))
        return;
    index = (DWORD)strtoul(value, NULL, 10);
    if (index >= SC2_MAX_CLIFF_SETS)
        return;
    if (!sc2_xml_attr(node, "name", sc2_map.t3Terrain.cliff_sets[index].name, sizeof(sc2_map.t3Terrain.cliff_sets[index].name)))
        return;
    sc2_map.t3Terrain.num_cliff_sets = MAX(sc2_map.t3Terrain.num_cliff_sets, index + 1);
}

static void sc2_parse_cliff_cell_node(xmlNodePtr node) {
    sc2CliffCell_t *cell;
    char value[64];

    if (!node || !sc2_streqi((char const *)node->name, "cc") || sc2_map.t3Terrain.num_cliff_cells >= SC2_MAX_CLIFF_CELLS)
        return;
    cell = &sc2_map.t3Terrain.cliff_cells[sc2_map.t3Terrain.num_cliff_cells];
    memset(cell, 0, sizeof(*cell));
    if (!sc2_xml_attr(node, "i", value, sizeof(value)))
        return;
    cell->index = (DWORD)strtoul(value, NULL, 10);
    if (sc2_xml_attr(node, "f", value, sizeof(value))) cell->flags = (DWORD)strtoul(value, NULL, 10);
    if (sc2_xml_attr(node, "cid", value, sizeof(value))) cell->cliff_set = (DWORD)strtoul(value, NULL, 10);
    if (sc2_xml_attr(node, "cvar", value, sizeof(value))) cell->variant = (DWORD)strtoul(value, NULL, 10);
    sc2_map.t3Terrain.num_cliff_cells++;
}

static void sc2_map_try_size_field(LPCSTR key, LPCSTR value) {
    sc2MapInfo_t *mapInfo = sc2_ensure_mapinfo();
    VECTOR3 v;
    if (!mapInfo || !key || !value || !*value) return;
    if ((sc2_contains_i(key, "width") || sc2_streqi(key, "x")) && atoi(value) > 0)
        mapInfo->width = (DWORD)atoi(value);
    else if ((sc2_contains_i(key, "height") || sc2_streqi(key, "y")) && atoi(value) > 0)
        mapInfo->height = (DWORD)atoi(value);
    else if ((sc2_contains_i(key, "size") || sc2_contains_i(key, "bounds")) && sc2_parse_vec3(value, &v)) {
        if (v.x > 0) mapInfo->width = (DWORD)v.x;
        if (v.y > 0) mapInfo->height = (DWORD)v.y;
    } else if ((sc2_contains_i(key, "name") || sc2_contains_i(key, "title")) && !sc2_map.map_name[0]) {
        snprintf(sc2_map.map_name, sizeof(sc2_map.map_name), "%s", value);
    }
}

static void sc2_parse_mapinfo_node(xmlNodePtr node) {
    char value[256];
    if (!node || node->type != XML_ELEMENT_NODE) return;
    for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
        xmlChar *text = xmlNodeListGetString(node->doc, attr->children, 1);
        if (text) {
            LPCSTR key = sc2_streqi((char const *)attr->name, "value") ?
                         (char const *)node->name : (char const *)attr->name;
            sc2_map_try_size_field(key, (char const *)text);
            xmlFree(text);
        }
    }
    if (sc2_xml_content(node, value, sizeof(value)) && sc2_has_nonspace(value))
        sc2_map_try_size_field((char const *)node->name, value);
    for (xmlNodePtr child = node->children; child; child = child->next)
        sc2_parse_mapinfo_node(child);
}

static BOOL sc2_parse_mapinfo_binary(sc2MapSource_t *source) {
    DWORD size = 0;
    LPBYTE data = sc2_source_read(source, "MapInfo", &size);
    DWORD header_size = (DWORD)(sizeof(sc2_map.MapInfo) - sizeof(sc2_map.MapInfo.data));

    if (!data || size < header_size) {
        sc2_free_file(data);
        return false;
    }
    memset(&sc2_map.MapInfo, 0, sizeof(sc2_map.MapInfo));
    memcpy(&sc2_map.MapInfo, data, MIN(size, (DWORD)sizeof(sc2_map.MapInfo)));
    sc2_free_file(data);
    if (!sc2_mapinfo_fourcc(&sc2_map.MapInfo)) {
        memset(&sc2_map.MapInfo, 0, sizeof(sc2_map.MapInfo));
        return false;
    }
    if (!sc2_map.map_name[0] && sc2_map.MapInfo.data[0])
        snprintf(sc2_map.map_name, sizeof(sc2_map.map_name), "%.*s",
                 (int)sizeof(sc2_map.MapInfo.data), (char const *)sc2_map.MapInfo.data);
    return true;
}

static void sc2_parse_mapinfo(sc2MapSource_t *source) {
    xmlDocPtr doc;

    if (sc2_parse_mapinfo_binary(source))
        return;
    doc = sc2_read_xml(source, "MapInfo");
    if (!doc) doc = sc2_read_xml(source, "MapInfo.xml");
    if (!doc) return;
    sc2_ensure_mapinfo();
    sc2_parse_mapinfo_node(xmlDocGetRootElement(doc));
    xmlFreeDoc(doc);
}

static sc2XmlField_t const sc2_terrain_height_map_fields[] = {
    SC2_TERRAIN_XML_STRING_FIELD("tileSet", tile_set),
};

static sc2XmlField_t const sc2_terrain_vert_data_fields[] = {
    SC2_TERRAIN_XML_FIELD("quantizeBias", height_quantize_bias, SC2_XML_FIELD_FLOAT),
    SC2_TERRAIN_XML_FIELD("quantizeScale", height_quantize_scale, SC2_XML_FIELD_FLOAT),
    SC2_TERRAIN_XML_FIELD("standardHeight", standard_height, SC2_XML_FIELD_FLOAT),
};

static sc2XmlNodeFields_t const sc2_terrain_node_fields[] = {
    { "heightMap", sc2_terrain_height_map_fields, SC2_ARRAY_LEN(sc2_terrain_height_map_fields) },
    { "vertData",  sc2_terrain_vert_data_fields,  SC2_ARRAY_LEN(sc2_terrain_vert_data_fields) },
};

static void sc2_parse_terrain_field(xmlNodePtr node, LPCSTR name, LPCSTR value) {
    FOR_LOOP(i, SC2_ARRAY_LEN(sc2_terrain_node_fields)) {
        sc2XmlNodeFields_t const *mapping = &sc2_terrain_node_fields[i];
        if (sc2_streqi((char const *)node->name, mapping->node_name)) {
            sc2_parse_xml_field(&sc2_map.t3Terrain, mapping->fields, mapping->num_fields, name, value);
            return;
        }
    }
}

static void sc2_parse_terrain_node(xmlNodePtr node) {
    char value[256];

    if (!node || node->type != XML_ELEMENT_NODE)
        return;
    sc2_parse_cliff_set_node(node);
    sc2_parse_cliff_cell_node(node);
    for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
        xmlChar *text = xmlNodeListGetString(node->doc, attr->children, 1);
        if (text) {
            LPCSTR key = (sc2_contains_i((char const *)node->name, "texture") &&
                          sc2_streqi((char const *)attr->name, "name")) ?
                         (char const *)node->name : (char const *)attr->name;
            sc2_parse_terrain_field(node, (char const *)attr->name, (char const *)text);
            sc2_parse_terrain_value(key, (char const *)text);
            xmlFree(text);
        }
    }
    if (sc2_xml_content(node, value, sizeof(value)) && sc2_has_nonspace(value)) {
        sc2_parse_terrain_value((char const *)node->name, value);
    }
    for (xmlNodePtr child = node->children; child; child = child->next) {
        sc2_parse_terrain_node(child);
    }
}

static void sc2_parse_terrain(sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_xml(source, "t3Terrain.xml");
    if (!doc) doc = sc2_read_xml(source, "t3Terrain");
    if (!doc) return;
    sc2_parse_terrain_node(xmlDocGetRootElement(doc));
    xmlFreeDoc(doc);
}

static void sc2_parse_terrain_data(sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_xml(source, "Base.SC2Data/GameData/TerrainData.xml");
    if (!doc) doc = sc2_read_xml(source, "Base.SC2Data\\GameData\\TerrainData.xml");
    if (!doc) return;
    sc2_parse_terrain_data_node(xmlDocGetRootElement(doc), NULL);
    xmlFreeDoc(doc);
}

static void sc2_parse_terrain_data_catalog_file(LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\TerrainData.xml");
    if (!doc) return;
    sc2_parse_terrain_data_node(xmlDocGetRootElement(doc), NULL);
    xmlFreeDoc(doc);
}

static void sc2_parse_terrain_data_catalogs(void) {
    for (DWORD i = 0; sc2_catalog_roots[i]; i++)
        sc2_parse_terrain_data_catalog_file(sc2_catalog_roots[i]);
    sc2_parse_terrain_data_catalog_file("");
}

static void sc2_parse_light_data(sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_xml(source, "Base.SC2Data/GameData/LightData.xml");
    if (!doc) doc = sc2_read_xml(source, "Base.SC2Data\\GameData\\LightData.xml");
    if (!doc) return;
    sc2_parse_light_data_node(xmlDocGetRootElement(doc), NULL, -1);
    xmlFreeDoc(doc);
}

static void sc2_parse_light_data_catalog_file(LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\LightData.xml");
    if (!doc) return;
    sc2_parse_light_data_node(xmlDocGetRootElement(doc), NULL, -1);
    xmlFreeDoc(doc);
}

static void sc2_parse_light_data_catalogs(void) {
    for (DWORD i = 0; sc2_catalog_roots[i]; i++)
        sc2_parse_light_data_catalog_file(sc2_catalog_roots[i]);
    sc2_parse_light_data_catalog_file("");
}

static void sc2_set_object_model(sc2MapObject_t *object) {
    if (!object->model[0] && object->name[0] && object->type == SC2_OBJECT_UNIT)
        snprintf(object->model, sizeof(object->model), "Assets\\Units\\Terran\\%s\\%s.m3", object->name, object->name);
}

static sc2XmlField_t const sc2_object_fields[] = {
    SC2_XML_FIELD("Id", id, SC2_XML_FIELD_DWORD),
    SC2_XML_STRING_FIELD("Name", name),
    SC2_XML_STRING_FIELD("Model", model),
    SC2_XML_STRING_FIELD("File", model),
    SC2_XML_FIELD("Position", position, SC2_XML_FIELD_VEC3),
    SC2_XML_FIELD("x", position.x, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("y", position.y, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("z", position.z, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("Rotation", angle, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("Angle", angle, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("Yaw", angle, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("Scale", scale, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("Variation", variation, SC2_XML_FIELD_DWORD),
    SC2_XML_FIELD("Player", player, SC2_XML_FIELD_DWORD),
    SC2_XML_FIELD("Owner", player, SC2_XML_FIELD_DWORD),
    SC2_XML_FIELD("Section", section, SC2_XML_FIELD_DWORD),
    SC2_XML_FIELD("Resources", resources, SC2_XML_FIELD_DWORD),
    SC2_XML_FIELD("TintColor", tint_color, SC2_XML_FIELD_COLOR_RGBA),
    SC2_XML_FIELD("Color", color, SC2_XML_FIELD_COLOR_RGBA),
};

static sc2XmlField_t const sc2_unit_fields[] = {
    SC2_XML_STRING_FIELD("UnitType", name),
};

static sc2XmlField_t const sc2_doodad_fields[] = {
    SC2_XML_STRING_FIELD("Type", name),
};

static sc2XmlField_t const sc2_point_fields[] = {
    SC2_XML_STRING_FIELD("Type", type_name),
    SC2_XML_STRING_FIELD("AnimProps", anim_props),
    SC2_XML_STRING_FIELD("Sound", sound),
    SC2_XML_STRING_FIELD("AttachID", attach_id),
    SC2_XML_FIELD("ObjectID", object_id, SC2_XML_FIELD_DWORD),
    SC2_XML_STRING_FIELD("ObjectType", object_type),
    SC2_XML_FIELD("PathingSoftRadius", pathing_soft_radius, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("PathingHardRadius", pathing_hard_radius, SC2_XML_FIELD_FLOAT),
};

static sc2XmlField_t const sc2_camera_fields[] = {
    SC2_XML_FIELD("CameraTarget", camera.target, SC2_XML_FIELD_VEC3),
    SC2_XML_FIELD("Target", camera.target, SC2_XML_FIELD_VEC3),
};

static sc2XmlField_t const sc2_camera_value_fields[] = {
    SC2_XML_FIELD("Distance", camera.distance, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("Pitch", camera.pitch, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("Yaw", camera.yaw, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("FieldOfView", camera.fov, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("NearClip", camera.znear, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("FarClip", camera.zfar, SC2_XML_FIELD_FLOAT),
    SC2_XML_FIELD("HeightOffset", camera.height_offset, SC2_XML_FIELD_FLOAT),
};

static sc2XmlFlag_t const sc2_object_flags[] = {
    { "HeightAbsolute", SC2_OBJECT_HEIGHT_ABSOLUTE },
    { "HeightOffset",   SC2_OBJECT_HEIGHT_OFFSET },
    { "ForcePlacement", SC2_OBJECT_FORCE_PLACEMENT },
};

static BOOL sc2_parse_xml_field(void *base, sc2XmlField_t const *fields, DWORD num_fields, LPCSTR name, LPCSTR value) {
    if (!base || !fields || !name || !value || !*value)
        return false;
    FOR_LOOP(i, num_fields) {
        sc2XmlField_t const *field = &fields[i];
        char *out = (char *)base + field->offset;
        if (!sc2_streqi(name, field->name))
            continue;
        switch (field->type) {
            case SC2_XML_FIELD_DWORD:
                *(DWORD *)out = (DWORD)strtoul(value, NULL, 10);
                return true;
            case SC2_XML_FIELD_FLOAT:
                *(FLOAT *)out = strtof(value, NULL);
                return true;
            case SC2_XML_FIELD_STRING:
                snprintf(out, field->size, "%s", value);
                return true;
            case SC2_XML_FIELD_VEC3:
                return sc2_parse_vec3(value, (LPVECTOR3)out);
            case SC2_XML_FIELD_COLOR_ARGB:
                return sc2_parse_argb_color(value, (LPCOLOR32)out);
            case SC2_XML_FIELD_COLOR_RGBA:
                return sc2_parse_rgba_color(value, (LPCOLOR32)out);
        }
    }
    return false;
}

static void sc2_parse_object_field(sc2MapObject_t *object, sc2XmlField_t const *fields, DWORD num_fields, LPCSTR key, LPCSTR value, BOOL *has_position) {
    if (sc2_parse_xml_field(object, fields, num_fields, key, value) &&
        (sc2_streqi(key, "Position") || sc2_streqi(key, "CameraTarget") ||
         sc2_streqi(key, "x") || sc2_streqi(key, "y")) &&
        has_position)
        *has_position = true;
}

static void sc2_parse_object_fields(sc2MapObject_t *object, sc2ObjectType_t type, LPCSTR key, LPCSTR value, BOOL *has_position) {
    sc2_parse_object_field(object, sc2_object_fields, SC2_ARRAY_LEN(sc2_object_fields), key, value, has_position);
    switch (type) {
        case SC2_OBJECT_UNIT:
            sc2_parse_object_field(object, sc2_unit_fields, SC2_ARRAY_LEN(sc2_unit_fields), key, value, has_position);
            break;
        case SC2_OBJECT_DOODAD:
            sc2_parse_object_field(object, sc2_doodad_fields, SC2_ARRAY_LEN(sc2_doodad_fields), key, value, has_position);
            break;
        case SC2_OBJECT_POINT:
            sc2_parse_object_field(object, sc2_point_fields, SC2_ARRAY_LEN(sc2_point_fields), key, value, has_position);
            break;
        case SC2_OBJECT_CAMERA:
            sc2_parse_object_field(object, sc2_camera_fields, SC2_ARRAY_LEN(sc2_camera_fields), key, value, has_position);
            break;
    }
}

static BOOL sc2_object_type(xmlNodePtr node, sc2ObjectType_t *type) {
    LPCSTR name = (char const *)node->name;

    if (sc2_contains_i(name, "ObjectUnit") || sc2_streqi(name, "Unit")) *type = SC2_OBJECT_UNIT;
    else if (sc2_contains_i(name, "ObjectDoodad") || sc2_streqi(name, "Doodad")) *type = SC2_OBJECT_DOODAD;
    else if (sc2_contains_i(name, "ObjectPoint") || sc2_streqi(name, "Point")) *type = SC2_OBJECT_POINT;
    else if (sc2_contains_i(name, "ObjectCamera") || sc2_streqi(name, "Camera")) *type = SC2_OBJECT_CAMERA;
    else return false;
    return true;
}

static void sc2_object_flag(sc2MapObject_t *object, xmlNodePtr node) {
    char index[64];
    char value[64];

    if (!object || !sc2_contains_i((char const *)node->name, "Flag"))
        return;
    if (!sc2_xml_attr(node, "Index", index, sizeof(index)) ||
        !sc2_xml_attr(node, "Value", value, sizeof(value)) ||
        !atoi(value))
        return;
    FOR_LOOP(i, SC2_ARRAY_LEN(sc2_object_flags)) {
        if (sc2_streqi(index, sc2_object_flags[i].name)) {
            object->flags |= sc2_object_flags[i].flag;
            return;
        }
    }
}

static void sc2_parse_object_node(xmlNodePtr node) {
    sc2MapObject_t object;
    BOOL has_position = false;
    sc2ObjectType_t type;
    char value[256];

    if (!node || node->type != XML_ELEMENT_NODE) return;
    if (!sc2_object_type(node, &type)) {
        for (xmlNodePtr child = node->children; child; child = child->next)
            sc2_parse_object_node(child);
        return;
    }
    memset(&object, 0, sizeof(object));
    object.scale = 1.0f;
    object.type = type;

    for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
        xmlChar *text = xmlNodeListGetString(node->doc, attr->children, 1);
        if (text) {
            sc2_parse_object_fields(&object, type, (char const *)attr->name, (char const *)text, &has_position);
            xmlFree(text);
        }
    }
    for (xmlNodePtr child = node->children; child; child = child->next) {
        char index[64];
        if (child->type != XML_ELEMENT_NODE)
            continue;
        sc2_object_flag(&object, child);
        if (type == SC2_OBJECT_CAMERA &&
            sc2_contains_i((char const *)child->name, "CameraValue") &&
            sc2_xml_attr(child, "Index", index, sizeof(index)) &&
            sc2_xml_attr(child, "Value", value, sizeof(value))) {
            sc2_parse_object_field(&object,
                                   sc2_camera_value_fields,
                                   SC2_ARRAY_LEN(sc2_camera_value_fields),
                                   index,
                                   value,
                                   NULL);
            continue;
        }
        if (sc2_xml_content(child, value, sizeof(value))) {
            sc2_parse_object_fields(&object, type, (char const *)child->name, value, &has_position);
        }
    }

    if (has_position && (object.name[0] || object.model[0])) {
        if (sc2_map.num_objects < SC2_MAX_MAP_OBJECTS) {
            sc2_set_object_model(&object);
            sc2_map.objects[sc2_map.num_objects++] = object;
        }
    }
}

static void sc2_parse_objects(sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_xml(source, "Objects");
    if (!doc) doc = sc2_read_xml(source, "Objects.xml");
    if (!doc) return;
    sc2_parse_object_node(xmlDocGetRootElement(doc));
    xmlFreeDoc(doc);
}

static void sc2_catalog_add_model(sc2Catalog_t *catalog, LPCSTR id, LPCSTR parent, LPCSTR race, LPCSTR path) {
    sc2CatalogModel_t *model;

    if (!catalog || !id || !*id) return;
    FOR_LOOP(i, catalog->models_count) {
        if (!strcasecmp(catalog->models[i].id, id)) {
            snprintf(catalog->models[i].parent, sizeof(catalog->models[i].parent), "%s", parent ? parent : "");
            snprintf(catalog->models[i].race, sizeof(catalog->models[i].race), "%s", race ? race : "");
            snprintf(catalog->models[i].path, sizeof(catalog->models[i].path), "%s", path ? path : "");
            sc2_normalize_slashes(catalog->models[i].path);
            return;
        }
    }
    if (catalog->models_count >= SC2_MAX_CATALOG_MODELS) return;
    model = &catalog->models[catalog->models_count++];
    snprintf(model->id, sizeof(model->id), "%s", id);
    snprintf(model->parent, sizeof(model->parent), "%s", parent ? parent : "");
    snprintf(model->race, sizeof(model->race), "%s", race ? race : "");
    snprintf(model->path, sizeof(model->path), "%s", path ? path : "");
    sc2_normalize_slashes(model->path);
}

static void sc2_catalog_add_actor(sc2Catalog_t *catalog, LPCSTR id, LPCSTR model_id, LPCSTR footprint) {
    sc2CatalogActor_t *actor;

    if (!catalog || !id || !*id || ((!model_id || !*model_id) && (!footprint || !*footprint))) return;
    FOR_LOOP(i, catalog->actors_count) {
        if (!strcasecmp(catalog->actors[i].id, id)) {
            if (model_id && *model_id)
                snprintf(catalog->actors[i].model, sizeof(catalog->actors[i].model), "%s", model_id);
            if (footprint && *footprint)
                snprintf(catalog->actors[i].footprint, sizeof(catalog->actors[i].footprint), "%s", footprint);
            return;
        }
    }
    if (catalog->actors_count >= SC2_MAX_CATALOG_ACTORS) return;
    actor = &catalog->actors[catalog->actors_count++];
    snprintf(actor->id, sizeof(actor->id), "%s", id);
    snprintf(actor->model, sizeof(actor->model), "%s", model_id ? model_id : "");
    snprintf(actor->footprint, sizeof(actor->footprint), "%s", footprint ? footprint : "");
}

static DWORD sc2_unit_flag(LPCSTR name) {
    if (!name || !*name) return 0;
    if (sc2_streqi(name, "Movable")) return SC2_UNIT_FLAG_MOVABLE;
    if (sc2_streqi(name, "Worker")) return SC2_UNIT_FLAG_WORKER;
    if (sc2_streqi(name, "Resource")) return SC2_UNIT_FLAG_RESOURCE;
    if (sc2_streqi(name, "Structure")) return SC2_UNIT_FLAG_STRUCTURE;
    return 0;
}

static void sc2_catalog_add_unit(sc2Catalog_t *catalog,
                                 LPCSTR id,
                                 LPCSTR actor_id,
                                 LPCSTR footprint,
                                 LPCSTR mover,
                                 DWORD flags,
                                 FLOAT radius,
                                 BOOL has_radius) {
    sc2CatalogUnit_t *unit;

    if (!catalog || !id || !*id ||
        ((!actor_id || !*actor_id) && (!footprint || !*footprint) && (!mover || !*mover) && !flags && !has_radius))
        return;
    FOR_LOOP(i, catalog->units_count) {
        if (!strcasecmp(catalog->units[i].id, id)) {
            if (actor_id && *actor_id)
                snprintf(catalog->units[i].actor, sizeof(catalog->units[i].actor), "%s", actor_id);
            if (footprint && *footprint)
                snprintf(catalog->units[i].footprint, sizeof(catalog->units[i].footprint), "%s", footprint);
            if (mover && *mover)
                snprintf(catalog->units[i].mover, sizeof(catalog->units[i].mover), "%s", mover);
            catalog->units[i].flags |= flags;
            if (has_radius) {
                catalog->units[i].has_radius = true;
                catalog->units[i].radius = radius;
            }
            return;
        }
    }
    if (catalog->units_count >= SC2_MAX_CATALOG_UNITS) return;
    unit = &catalog->units[catalog->units_count++];
    snprintf(unit->id, sizeof(unit->id), "%s", id);
    snprintf(unit->actor, sizeof(unit->actor), "%s", actor_id ? actor_id : "");
    snprintf(unit->footprint, sizeof(unit->footprint), "%s", footprint ? footprint : "");
    snprintf(unit->mover, sizeof(unit->mover), "%s", mover ? mover : "");
    unit->flags = flags;
    unit->has_radius = has_radius;
    unit->radius = has_radius ? radius : 0.0f;
}

static void sc2_catalog_add_terrain_tex(sc2Catalog_t *catalog, LPCSTR id, LPCSTR diffuse, LPCSTR normal) {
    sc2CatalogTerrainTex_t *tex;

    if (!catalog || !id || !*id || !diffuse || !*diffuse) return;
    FOR_LOOP(i, catalog->terrain_tex_count) {
        if (!strcasecmp(catalog->terrain_tex[i].id, id)) {
            snprintf(catalog->terrain_tex[i].diffuse, sizeof(catalog->terrain_tex[i].diffuse), "%s", diffuse);
            snprintf(catalog->terrain_tex[i].normal, sizeof(catalog->terrain_tex[i].normal), "%s", normal ? normal : "");
            sc2_normalize_slashes(catalog->terrain_tex[i].diffuse);
            sc2_normalize_slashes(catalog->terrain_tex[i].normal);
            return;
        }
    }
    if (catalog->terrain_tex_count >= SC2_MAX_CATALOG_TERRAIN_TEX) return;
    tex = &catalog->terrain_tex[catalog->terrain_tex_count++];
    snprintf(tex->id, sizeof(tex->id), "%s", id);
    snprintf(tex->diffuse, sizeof(tex->diffuse), "%s", diffuse);
    snprintf(tex->normal, sizeof(tex->normal), "%s", normal ? normal : "");
    sc2_normalize_slashes(tex->diffuse);
    sc2_normalize_slashes(tex->normal);
}

static void sc2_catalog_add_cliff(sc2Catalog_t *catalog, LPCSTR id, LPCSTR mesh) {
    sc2CatalogCliff_t *cliff;

    if (!catalog || !id || !*id || !mesh || !*mesh) return;
    FOR_LOOP(i, catalog->cliffs_count) {
        if (!strcasecmp(catalog->cliffs[i].id, id)) {
            snprintf(catalog->cliffs[i].mesh, sizeof(catalog->cliffs[i].mesh), "%s", mesh);
            return;
        }
    }
    if (catalog->cliffs_count >= SC2_MAX_CATALOG_CLIFFS) return;
    cliff = &catalog->cliffs[catalog->cliffs_count++];
    snprintf(cliff->id, sizeof(cliff->id), "%s", id);
    snprintf(cliff->mesh, sizeof(cliff->mesh), "%s", mesh);
}

static void sc2_catalog_add_footprint(sc2Catalog_t *catalog,
                                      LPCSTR id,
                                      FLOAT width,
                                      FLOAT height,
                                      FLOAT radius) {
    sc2CatalogFootprint_t *footprint;

    if (!catalog || !id || !*id || (width <= 0.0f && height <= 0.0f && radius <= 0.0f))
        return;
    if (radius <= 0.0f)
        radius = MAX(width, height) * 0.5f;
    if (width <= 0.0f)
        width = radius * 2.0f;
    if (height <= 0.0f)
        height = radius * 2.0f;
    FOR_LOOP(i, catalog->footprints_count) {
        if (!strcasecmp(catalog->footprints[i].id, id)) {
            catalog->footprints[i].width = width;
            catalog->footprints[i].height = height;
            catalog->footprints[i].radius = radius;
            return;
        }
    }
    if (catalog->footprints_count >= SC2_MAX_CATALOG_FOOTPRINTS) return;
    footprint = &catalog->footprints[catalog->footprints_count++];
    snprintf(footprint->id, sizeof(footprint->id), "%s", id);
    footprint->width = width;
    footprint->height = height;
    footprint->radius = radius;
}

static sc2CatalogModel_t const *sc2_catalog_model(sc2Catalog_t const *catalog, LPCSTR id) {
    if (!catalog || !id || !*id) return NULL;
    FOR_LOOP(i, catalog->models_count) {
        if (!strcasecmp(catalog->models[i].id, id)) return &catalog->models[i];
    }
    return NULL;
}

static BOOL sc2_append_text(LPSTR out, DWORD out_size, DWORD *pos, LPCSTR text) {
    DWORD len;

    if (!out || !out_size || !pos || !text) return false;
    len = (DWORD)strlen(text);
    if (*pos + len >= out_size) return false;
    memcpy(out + *pos, text, len);
    *pos += len;
    out[*pos] = '\0';
    return true;
}

static BOOL sc2_catalog_expand_model_path(LPCSTR path, LPCSTR id, LPCSTR race, LPSTR out, DWORD out_size) {
    DWORD pos = 0;

    if (!path || !*path || !id || !*id || !out || !out_size) return false;
    out[0] = '\0';
    for (LPCSTR p = path; *p; ) {
        if (!strncmp(p, "##id##", 6)) {
            if (!sc2_append_text(out, out_size, &pos, id)) return false;
            p += 6;
        } else if (!strncmp(p, "##Race##", 8)) {
            if (!race || !*race || !sc2_append_text(out, out_size, &pos, race)) return false;
            p += 8;
        } else if (!strncmp(p, "##", 2)) {
            return false;
        } else {
            if (pos + 1 >= out_size) return false;
            out[pos++] = *p++;
            out[pos] = '\0';
        }
    }
    sc2_normalize_slashes(out);
    return out[0] != '\0';
}

static BOOL sc2_catalog_resolve_model_path_r(sc2Catalog_t const *catalog,
                                             sc2CatalogModel_t const *model,
                                             LPCSTR id,
                                             LPCSTR race,
                                             LPSTR out,
                                             DWORD out_size,
                                             DWORD depth) {
    if (!catalog || !model || !out || !out_size || depth > SC2_MAX_CATALOG_PARENT_DEPTH) return false;
    if (model->path[0]) {
        return sc2_catalog_expand_model_path(model->path,
                                             id && *id ? id : model->id,
                                             race && *race ? race : model->race,
                                             out,
                                             out_size);
    }
    if (model->parent[0]) {
        return sc2_catalog_resolve_model_path_r(catalog,
                                                sc2_catalog_model(catalog, model->parent),
                                                id && *id ? id : model->id,
                                                race && *race ? race : model->race,
                                                out,
                                                out_size,
                                                depth + 1);
    }
    return false;
}

static BOOL sc2_catalog_model_path(sc2Catalog_t const *catalog, LPCSTR id, LPSTR out, DWORD out_size) {
    sc2CatalogModel_t const *model = sc2_catalog_model(catalog, id);

    return sc2_catalog_resolve_model_path_r(catalog, model, id, model ? model->race : NULL, out, out_size, 0);
}

static LPCSTR sc2_catalog_cliff_mesh(sc2Catalog_t const *catalog, LPCSTR id) {
    if (!catalog || !id || !*id) return NULL;
    FOR_LOOP(i, catalog->cliffs_count) {
        if (!strcasecmp(catalog->cliffs[i].id, id)) return catalog->cliffs[i].mesh;
    }
    return NULL;
}

static BOOL sc2_cliff_mesh_exists(LPCSTR mesh) {
    char path[256];

    if (!mesh || !*mesh) return false;
    snprintf(path, sizeof(path), "Assets\\Cliffs\\%s\\%s_ABBB_00.m3", mesh, mesh);
    return sc2_file_exists(path);
}

static LPCSTR sc2_cliff_mesh_fallback(LPCSTR name) {
    DWORD len;

    if (!name || !*name) return NULL;
    if (sc2_cliff_mesh_exists(name)) return name;
    len = (DWORD)strlen(name);
    if (len >= 6 && !strcasecmp(name + len - 6, "Cliff0") && sc2_cliff_mesh_exists("CliffNatural0"))
        return "CliffNatural0";
    if (len >= 6 && !strcasecmp(name + len - 6, "Cliff1") && sc2_cliff_mesh_exists("CliffMade0"))
        return "CliffMade0";
    return NULL;
}

static sc2CatalogActor_t const *sc2_catalog_actor(sc2Catalog_t const *catalog, LPCSTR id) {
    if (!catalog || !id || !*id) return NULL;
    FOR_LOOP(i, catalog->actors_count) {
        if (!strcasecmp(catalog->actors[i].id, id)) return &catalog->actors[i];
    }
    return NULL;
}

static LPCSTR sc2_catalog_actor_model(sc2Catalog_t const *catalog, LPCSTR id) {
    sc2CatalogActor_t const *actor = sc2_catalog_actor(catalog, id);
    return actor && actor->model[0] ? actor->model : NULL;
}

static LPCSTR sc2_catalog_actor_footprint(sc2Catalog_t const *catalog, LPCSTR id) {
    sc2CatalogActor_t const *actor = sc2_catalog_actor(catalog, id);
    return actor && actor->footprint[0] ? actor->footprint : NULL;
}

static sc2CatalogUnit_t const *sc2_catalog_unit(sc2Catalog_t const *catalog, LPCSTR id) {
    if (!catalog || !id || !*id) return NULL;
    FOR_LOOP(i, catalog->units_count) {
        if (!strcasecmp(catalog->units[i].id, id)) return &catalog->units[i];
    }
    return NULL;
}

static sc2CatalogFootprint_t const *sc2_catalog_footprint(sc2Catalog_t const *catalog, LPCSTR id) {
    if (!catalog || !id || !*id) return NULL;
    FOR_LOOP(i, catalog->footprints_count) {
        if (!strcasecmp(catalog->footprints[i].id, id)) return &catalog->footprints[i];
    }
    return NULL;
}

static LPCSTR sc2_catalog_terrain_diffuse(sc2Catalog_t const *catalog, LPCSTR id, LPCSTR *normal) {
    char key[64];

    if (normal) *normal = NULL;
    if (!catalog || !id || !*id) return NULL;
    snprintf(key, sizeof(key), "%s", id);
    if (sc2_has_extension_i(key, ".dds")) {
        char *ext = strrchr(key, '.');
        if (ext) *ext = '\0';
    }
    FOR_LOOP(i, catalog->terrain_tex_count) {
        if (!strcasecmp(catalog->terrain_tex[i].id, key)) {
            if (normal) *normal = catalog->terrain_tex[i].normal;
            return catalog->terrain_tex[i].diffuse;
        }
    }
    return NULL;
}

static BOOL sc2_terrain_texture_path_from_tileset(LPCSTR id,
                                                  LPSTR diffuse,
                                                  DWORD diffuse_size,
                                                  LPSTR normal,
                                                  DWORD normal_size) {
    char suffix[128];
    char path[256];
    DWORD tile_len;

    if (!id || !*id || !sc2_map.t3Terrain.tile_set[0] || !diffuse || !normal)
        return false;
    tile_len = (DWORD)strlen(sc2_map.t3Terrain.tile_set);
    if (strncasecmp(id, sc2_map.t3Terrain.tile_set, tile_len) || !id[tile_len])
        return false;
    sc2_camel_to_underscore(id + tile_len, suffix, sizeof(suffix));
    snprintf(path, sizeof(path), "Assets\\Textures\\%s_%s.dds", sc2_map.t3Terrain.tile_set, suffix);
    if (!sc2_file_exists(path))
        return false;
    snprintf(diffuse, diffuse_size, "%s", path);
    snprintf(path, sizeof(path), "Assets\\Textures\\%s_%sNormal.dds", sc2_map.t3Terrain.tile_set, suffix);
    snprintf(normal, normal_size, "%s", sc2_file_exists(path) ? path : diffuse);
    return true;
}

static void sc2_parse_model_catalog_doc(sc2Catalog_t *catalog, xmlDocPtr doc) {
    xmlNodePtr root;

    if (!doc) return;
    root = xmlDocGetRootElement(doc);
    for (xmlNodePtr node = root ? root->children : NULL; node; node = node->next) {
        char id[64];
        char parent[64] = "";
        char race[64] = "";
        char path[256] = "";

        if (node->type != XML_ELEMENT_NODE || !sc2_contains_i((char const *)node->name, "CModel"))
            continue;
        if (!sc2_xml_attr(node, "id", id, sizeof(id))) continue;
        sc2_xml_attr(node, "parent", parent, sizeof(parent));
        sc2_xml_attr(node, "Race", race, sizeof(race));
        for (xmlNodePtr child = node->children; child; child = child->next) {
            if (child->type == XML_ELEMENT_NODE && sc2_streqi((char const *)child->name, "Model")) {
                sc2_xml_attr(child, "value", path, sizeof(path));
                break;
            }
        }
        sc2_catalog_add_model(catalog, id, parent, race, path);
    }
}

static void sc2_parse_model_catalog_file(sc2Catalog_t *catalog, LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\ModelData.xml");

    sc2_parse_model_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_model_catalog_source(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_map_catalog_xml(source, "GameData\\ModelData.xml");

    sc2_parse_model_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_actor_catalog_doc(sc2Catalog_t *catalog, xmlDocPtr doc) {
    xmlNodePtr root;

    if (!doc) return;
    root = xmlDocGetRootElement(doc);
    for (xmlNodePtr node = root ? root->children : NULL; node; node = node->next) {
        char id[64];
        char unit_name[64] = "";
        char model_id[64] = "";
        char footprint[64] = "";
        BOOL actor_node;

        if (node->type != XML_ELEMENT_NODE) continue;
        actor_node = sc2_contains_i((char const *)node->name, "CActorUnit") ||
                     sc2_contains_i((char const *)node->name, "CActorDoodad");
        if (!actor_node || !sc2_xml_attr(node, "id", id, sizeof(id))) continue;
        sc2_xml_attr(node, "unitName", unit_name, sizeof(unit_name));
        for (xmlNodePtr child = node->children; child; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            if (sc2_streqi((char const *)child->name, "Model")) {
                sc2_xml_attr(child, "value", model_id, sizeof(model_id));
            } else if (sc2_streqi((char const *)child->name, "Footprint")) {
                sc2_xml_attr(child, "value", footprint, sizeof(footprint));
            }
        }
        if (!model_id[0]) snprintf(model_id, sizeof(model_id), "%s", id);
        sc2_catalog_add_actor(catalog, id, model_id, footprint);
        if (unit_name[0]) sc2_catalog_add_actor(catalog, unit_name, model_id, footprint);
    }
}

static void sc2_parse_actor_catalog_file(sc2Catalog_t *catalog, LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\ActorData.xml");

    sc2_parse_actor_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_actor_catalog_source(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_map_catalog_xml(source, "GameData\\ActorData.xml");

    sc2_parse_actor_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_unit_catalog_doc(sc2Catalog_t *catalog, xmlDocPtr doc) {
    xmlNodePtr root;

    if (!doc) return;
    root = xmlDocGetRootElement(doc);
    for (xmlNodePtr node = root ? root->children : NULL; node; node = node->next) {
        char id[64];
        char actor_id[64] = "";
        char footprint[64] = "";
        char mover[64] = "";
        DWORD flags = 0;
        FLOAT radius = 0.0f;
        BOOL has_radius = false;

        if (node->type != XML_ELEMENT_NODE || !sc2_contains_i((char const *)node->name, "CUnit"))
            continue;
        if (!sc2_xml_attr(node, "id", id, sizeof(id))) continue;
        for (xmlNodePtr child = node->children; child; child = child->next) {
            char value[64];

            if (child->type != XML_ELEMENT_NODE)
                continue;
            if (sc2_streqi((char const *)child->name, "Actor")) {
                sc2_xml_attr(child, "value", actor_id, sizeof(actor_id));
            } else if (sc2_streqi((char const *)child->name, "Footprint")) {
                sc2_xml_attr(child, "value", footprint, sizeof(footprint));
            } else if (sc2_streqi((char const *)child->name, "Mover")) {
                sc2_xml_attr(child, "value", mover, sizeof(mover));
            } else if (sc2_contains_i((char const *)child->name, "Flag")) {
                char index[64];
                if ((sc2_xml_attr(child, "index", index, sizeof(index)) ||
                     sc2_xml_attr(child, "Index", index, sizeof(index))) &&
                    (sc2_xml_attr(child, "value", value, sizeof(value)) ||
                     sc2_xml_attr(child, "Value", value, sizeof(value))) &&
                    atoi(value)) {
                    flags |= sc2_unit_flag(index);
                }
            } else if (sc2_streqi((char const *)child->name, "Radius") &&
                       sc2_xml_attr(child, "value", value, sizeof(value)) &&
                       sscanf(value, "%f", &radius) == 1 && radius > 0.0f) {
                has_radius = true;
            }
        }
        sc2_catalog_add_unit(catalog, id, actor_id, footprint, mover, flags, radius, has_radius);
    }
}

static void sc2_parse_unit_catalog_file(sc2Catalog_t *catalog, LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\UnitData.xml");

    sc2_parse_unit_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_unit_catalog_source(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_map_catalog_xml(source, "GameData\\UnitData.xml");

    sc2_parse_unit_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_footprint_catalog_doc(sc2Catalog_t *catalog, xmlDocPtr doc) {
    xmlNodePtr root;

    if (!doc) return;
    root = xmlDocGetRootElement(doc);
    for (xmlNodePtr node = root ? root->children : NULL; node; node = node->next) {
        char id[64];
        FLOAT width = 0.0f;
        FLOAT height = 0.0f;
        FLOAT radius = 0.0f;

        if (node->type != XML_ELEMENT_NODE || !sc2_contains_i((char const *)node->name, "CFootprint"))
            continue;
        if (!sc2_xml_attr(node, "id", id, sizeof(id))) continue;
        for (xmlNodePtr child = node->children; child; child = child->next) {
            char value[64];
            FLOAT child_width;
            FLOAT child_height;

            if (child->type != XML_ELEMENT_NODE) continue;
            if (sc2_streqi((char const *)child->name, "Layers") &&
                sc2_xml_attr(child, "Area", value, sizeof(value)) &&
                sc2_parse_footprint_area(value, &child_width, &child_height) &&
                child_width * child_height >= width * height) {
                width = child_width;
                height = child_height;
            } else if (sc2_streqi((char const *)child->name, "Size") &&
                       sc2_xml_attr(child, "value", value, sizeof(value)) &&
                       sc2_parse_float_pair(value, &child_width, &child_height) &&
                       child_width > 0.0f && child_height > 0.0f) {
                width = child_width;
                height = child_height;
            } else if (sc2_streqi((char const *)child->name, "Shape")) {
                if (sc2_xml_attr(child, "Radius", value, sizeof(value)) ||
                    sc2_xml_attr(child, "radius", value, sizeof(value))) {
                    sscanf(value, "%f", &radius);
                }
                for (xmlNodePtr shape = child->children; shape; shape = shape->next) {
                    if (shape->type == XML_ELEMENT_NODE &&
                        sc2_streqi((char const *)shape->name, "Radius") &&
                        sc2_xml_attr(shape, "value", value, sizeof(value))) {
                        sscanf(value, "%f", &radius);
                    }
                }
            }
        }
        sc2_catalog_add_footprint(catalog, id, width, height, radius);
    }
}

static void sc2_parse_footprint_catalog_file(sc2Catalog_t *catalog, LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\FootprintData.xml");

    sc2_parse_footprint_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_footprint_catalog_source(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_map_catalog_xml(source, "GameData\\FootprintData.xml");

    sc2_parse_footprint_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_terrain_tex_catalog_doc(sc2Catalog_t *catalog, xmlDocPtr doc) {
    xmlNodePtr root;

    if (!doc) return;
    root = xmlDocGetRootElement(doc);
    for (xmlNodePtr node = root ? root->children : NULL; node; node = node->next) {
        char id[64];
        char diffuse[256] = "";
        char normal[256] = "";

        if (node->type != XML_ELEMENT_NODE || !sc2_contains_i((char const *)node->name, "CTerrainTex"))
            continue;
        if (!sc2_xml_attr(node, "id", id, sizeof(id))) continue;
        for (xmlNodePtr child = node->children; child; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            if (sc2_streqi((char const *)child->name, "Texture"))
                sc2_xml_attr(child, "value", diffuse, sizeof(diffuse));
            else if (sc2_streqi((char const *)child->name, "Normalmap"))
                sc2_xml_attr(child, "value", normal, sizeof(normal));
        }
        sc2_catalog_add_terrain_tex(catalog, id, diffuse, normal);
    }
}

static void sc2_parse_terrain_tex_catalog_file(sc2Catalog_t *catalog, LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\TerrainTexData.xml");

    sc2_parse_terrain_tex_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_terrain_tex_catalog_source(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_map_catalog_xml(source, "GameData\\TerrainTexData.xml");

    sc2_parse_terrain_tex_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_cliff_catalog_doc(sc2Catalog_t *catalog, xmlDocPtr doc) {
    xmlNodePtr root;

    if (!doc) return;
    root = xmlDocGetRootElement(doc);
    for (xmlNodePtr node = root ? root->children : NULL; node; node = node->next) {
        char id[64];
        char mesh[64] = "";

        if (node->type != XML_ELEMENT_NODE || !sc2_contains_i((char const *)node->name, "CCliff"))
            continue;
        if (!sc2_xml_attr(node, "id", id, sizeof(id))) continue;
        for (xmlNodePtr child = node->children; child; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            if (sc2_streqi((char const *)child->name, "CliffMesh")) {
                sc2_xml_attr(child, "value", mesh, sizeof(mesh));
                break;
            }
        }
        sc2_catalog_add_cliff(catalog, id, mesh);
    }
}

static void sc2_parse_cliff_catalog_file(sc2Catalog_t *catalog, LPCSTR root_name) {
    xmlDocPtr doc = sc2_read_catalog_xml(root_name, "GameData\\CliffData.xml");

    sc2_parse_cliff_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_cliff_catalog_source(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    xmlDocPtr doc = sc2_read_map_catalog_xml(source, "GameData\\CliffData.xml");

    sc2_parse_cliff_catalog_doc(catalog, doc);
    xmlFreeDoc(doc);
}

static void sc2_parse_catalog_doc(sc2Catalog_t *catalog, xmlDocPtr doc) {
    sc2_parse_unit_catalog_doc(catalog, doc);
    sc2_parse_model_catalog_doc(catalog, doc);
    sc2_parse_actor_catalog_doc(catalog, doc);
    sc2_parse_footprint_catalog_doc(catalog, doc);
    sc2_parse_terrain_tex_catalog_doc(catalog, doc);
    sc2_parse_cliff_catalog_doc(catalog, doc);
}

static void sc2_parse_catalog_layer_doc(sc2Catalog_t *catalog,
                                        sc2MapSource_t *source,
                                        LPCSTR root_name,
                                        xmlDocPtr doc,
                                        DWORD depth) {
    xmlNodePtr root;

    if (!catalog || !doc)
        return;
    root = xmlDocGetRootElement(doc);
    if (!root || root->type != XML_ELEMENT_NODE)
        return;
    if (sc2_streqi((char const *)root->name, "Catalog")) {
        sc2_parse_catalog_doc(catalog, doc);
        return;
    }
    if (!sc2_streqi((char const *)root->name, "Includes") ||
        depth >= SC2_MAX_CATALOG_INCLUDE_DEPTH)
        return;
    for (xmlNodePtr node = root->children; node; node = node->next) {
        char include_path[MAX_PATHLEN];
        char path[MAX_PATHLEN];
        xmlDocPtr include_doc;

        if (node->type != XML_ELEMENT_NODE || !sc2_streqi((char const *)node->name, "Catalog"))
            continue;
        if (!sc2_xml_attr(node, "path", include_path, sizeof(include_path)) &&
            !sc2_xml_attr(node, "Path", include_path, sizeof(include_path)))
            continue;
        if (!sc2_catalog_include_path(include_path, path, sizeof(path)))
            continue;
        include_doc = sc2_read_layer_catalog_xml(source, root_name, path);
        if (!include_doc)
            continue;
        sc2_parse_catalog_layer_doc(catalog, source, root_name, include_doc, depth + 1);
        xmlFreeDoc(include_doc);
    }
}

static void sc2_parse_catalog_layer_fallback(sc2Catalog_t *catalog,
                                             sc2MapSource_t *source,
                                             LPCSTR root_name) {
    for (DWORD i = 0; sc2_catalog_known_files[i]; i++) {
        xmlDocPtr doc = sc2_read_layer_catalog_xml(source, root_name, sc2_catalog_known_files[i]);

        sc2_parse_catalog_doc(catalog, doc);
        xmlFreeDoc(doc);
    }
}

static void sc2_parse_catalog_layer(sc2Catalog_t *catalog,
                                    sc2MapSource_t *source,
                                    LPCSTR root_name) {
    xmlDocPtr manifest = sc2_read_layer_catalog_xml(source, root_name, "GameData.xml");

    if (manifest) {
        sc2_parse_catalog_layer_doc(catalog, source, root_name, manifest, 0);
        xmlFreeDoc(manifest);
        return;
    }
    sc2_parse_catalog_layer_fallback(catalog, source, root_name);
}

static void sc2_parse_catalogs(sc2Catalog_t *catalog, sc2MapSource_t *source) {
    for (DWORD i = 0; sc2_catalog_roots[i]; i++)
        sc2_parse_catalog_layer(catalog, NULL, sc2_catalog_roots[i]);
    sc2_parse_catalog_layer(catalog, NULL, "");
    sc2_parse_catalog_layer(catalog, source, NULL);
}

static void sc2_resolve_terrain_textures(sc2Catalog_t const *catalog) {
    FOR_LOOP(i, sc2_map.t3Terrain.num_terrain_textures) {
        char stem[128];
        char diffuse_path[256];
        char normal_path[256];
        LPCSTR normal;
        LPCSTR diffuse = sc2_catalog_terrain_diffuse(catalog, sc2_map.t3Terrain.terrain_textures[i].diffuse, &normal);

        if (diffuse) {
            snprintf(sc2_map.t3Terrain.terrain_textures[i].diffuse, sizeof(sc2_map.t3Terrain.terrain_textures[i].diffuse), "%s", diffuse);
            snprintf(sc2_map.t3Terrain.terrain_textures[i].normal, sizeof(sc2_map.t3Terrain.terrain_textures[i].normal), "%s",
                     normal && *normal ? normal : diffuse);
            continue;
        }
        if (sc2_path_has_dir(sc2_map.t3Terrain.terrain_textures[i].diffuse))
            continue;
        if (sc2_terrain_texture_path_from_tileset(sc2_map.t3Terrain.terrain_textures[i].diffuse,
                                                  sc2_map.t3Terrain.terrain_textures[i].diffuse,
                                                  sizeof(sc2_map.t3Terrain.terrain_textures[i].diffuse),
                                                  sc2_map.t3Terrain.terrain_textures[i].normal,
                                                  sizeof(sc2_map.t3Terrain.terrain_textures[i].normal))) {
            continue;
        }
        sc2_camel_to_underscore(sc2_map.t3Terrain.terrain_textures[i].diffuse, stem, sizeof(stem));
        snprintf(diffuse_path, sizeof(diffuse_path), "Assets\\Textures\\%s.dds", stem);
        snprintf(normal_path, sizeof(normal_path), "Assets\\Textures\\%sNormal.dds", stem);
        if (sc2_file_exists(diffuse_path)) {
            snprintf(sc2_map.t3Terrain.terrain_textures[i].diffuse, sizeof(sc2_map.t3Terrain.terrain_textures[i].diffuse), "%s", diffuse_path);
            snprintf(sc2_map.t3Terrain.terrain_textures[i].normal, sizeof(sc2_map.t3Terrain.terrain_textures[i].normal), "%s",
                     sc2_file_exists(normal_path) ? normal_path : diffuse_path);
        }
    }
}

static void sc2_resolve_cliff_sets(sc2Catalog_t const *catalog) {
    FOR_LOOP(i, sc2_map.t3Terrain.num_cliff_sets) {
        LPCSTR mesh = sc2_catalog_cliff_mesh(catalog, sc2_map.t3Terrain.cliff_sets[i].name);
        if (!mesh || !*mesh)
            mesh = sc2_cliff_mesh_fallback(sc2_map.t3Terrain.cliff_sets[i].name);
        if (mesh && *mesh) {
            snprintf(sc2_map.t3Terrain.cliff_sets[i].mesh, sizeof(sc2_map.t3Terrain.cliff_sets[i].mesh), "%s", mesh);
        } else if (sc2_map.t3Terrain.cliff_sets[i].name[0]) {
            snprintf(sc2_map.t3Terrain.cliff_sets[i].mesh,
                     sizeof(sc2_map.t3Terrain.cliff_sets[i].mesh),
                     "%s",
                     sc2_map.t3Terrain.cliff_sets[i].name);
        }
    }
}

static BOOL sc2_try_object_model_path(sc2MapObject_t *object, LPCSTR prefix) {
    char path[256];

    if (!object || !prefix || !*prefix || !object->name[0]) return false;
    snprintf(path, sizeof(path), "%s\\%s\\%s.m3", prefix, object->name, object->name);
    if (sc2_file_exists(path)) {
        snprintf(object->model, sizeof(object->model), "%s", path);
        return true;
    }
    snprintf(path, sizeof(path), "%s\\%s\\%s_%02u.m3", prefix, object->name, object->name, object->variation);
    if (sc2_file_exists(path)) {
        snprintf(object->model, sizeof(object->model), "%s", path);
        return true;
    }
    snprintf(path, sizeof(path), "%s\\%s\\%s_00.m3", prefix, object->name, object->name);
    if (sc2_file_exists(path)) {
        snprintf(object->model, sizeof(object->model), "%s", path);
        return true;
    }
    return false;
}

static void sc2_resolve_object_model_candidates(sc2MapObject_t *object) {
    static struct {
        LPCSTR name;
        LPCSTR model;
    } const aliases[] = {
        { "Civilian", "Assets\\Units\\Terran\\ColonistMale\\ColonistMale.m3" },
        { "CivilianFemale", "Assets\\Units\\Terran\\ColonistFemale\\ColonistFemale_00.m3" },
        { "LogisticsHeadquarters", "Assets\\Doodads\\TRaynor01RadioTower\\TRaynor01RadioTower.m3" },
        { NULL, NULL },
    };
    static LPCSTR const prefixes[] = {
        "Assets\\Units\\Critters",
        "Assets\\Buildings\\Terran",
        "Assets\\Units\\Terran",
        "Assets\\Doodads",
        "Assets\\Effects\\Terran",
        NULL,
    };

    if (!object || !object->name[0]) return;
    for (DWORD i = 0; aliases[i].name; i++) {
        if (!strcasecmp(object->name, aliases[i].name) && sc2_file_exists(aliases[i].model)) {
            snprintf(object->model, sizeof(object->model), "%s", aliases[i].model);
            return;
        }
    }
    for (DWORD i = 0; prefixes[i]; i++) {
        if (sc2_try_object_model_path(object, prefixes[i])) return;
    }
}

static void sc2_resolve_object_footprint(sc2Catalog_t const *catalog, sc2MapObject_t *object) {
    sc2CatalogFootprint_t const *footprint;

    if (!catalog || !object || !object->footprint[0]) return;
    footprint = sc2_catalog_footprint(catalog, object->footprint);
    if (!footprint) return;
    object->footprint_width = footprint->width;
    object->footprint_height = footprint->height;
    object->footprint_radius = footprint->radius;
}

static void sc2_resolve_object_models(sc2Catalog_t const *catalog) {
    sc2ResolvedObjectModel_t resolved[SC2_MAX_MAP_OBJECTS];
    DWORD resolved_count = 0;

    memset(resolved, 0, sizeof(resolved));
    FOR_LOOP(i, sc2_map.num_objects) {
        sc2MapObject_t *object = &sc2_map.objects[i];
        LPCSTR model_id;
        char path[256];

        if (!object->name[0]) continue;
        FOR_LOOP(j, resolved_count) {
            if (!strcasecmp(resolved[j].name, object->name)) {
                snprintf(object->model, sizeof(object->model), "%s", resolved[j].model);
                snprintf(object->footprint, sizeof(object->footprint), "%s", resolved[j].footprint);
                snprintf(object->mover, sizeof(object->mover), "%s", resolved[j].mover);
                object->radius = resolved[j].radius;
                object->footprint_width = resolved[j].footprint_width;
                object->footprint_height = resolved[j].footprint_height;
                object->footprint_radius = resolved[j].footprint_radius;
                object->unit_flags = resolved[j].unit_flags;
                goto next_object;
            }
        }
        model_id = NULL;
        if (object->type == SC2_OBJECT_UNIT) {
            sc2CatalogUnit_t const *unit = sc2_catalog_unit(catalog, object->name);
            if (unit) {
                if (unit->has_radius) object->radius = unit->radius;
                if (unit->footprint[0]) snprintf(object->footprint, sizeof(object->footprint), "%s", unit->footprint);
                if (unit->mover[0]) snprintf(object->mover, sizeof(object->mover), "%s", unit->mover);
                object->unit_flags |= unit->flags;
                if (unit->actor[0]) {
                    LPCSTR actor_footprint = sc2_catalog_actor_footprint(catalog, unit->actor);
                    if (!object->footprint[0] && actor_footprint)
                        snprintf(object->footprint, sizeof(object->footprint), "%s", actor_footprint);
                    model_id = sc2_catalog_actor_model(catalog, unit->actor);
                }
            }
        }
        if (!object->footprint[0]) {
            LPCSTR actor_footprint = sc2_catalog_actor_footprint(catalog, object->name);
            if (actor_footprint)
                snprintf(object->footprint, sizeof(object->footprint), "%s", actor_footprint);
        }
        sc2_resolve_object_footprint(catalog, object);
        if (!model_id) model_id = sc2_catalog_actor_model(catalog, object->name);
        if (!model_id) model_id = object->name;
        path[0] = '\0';
        if (sc2_catalog_model_path(catalog, model_id, path, sizeof(path))) {
            snprintf(object->model, sizeof(object->model), "%s", path);
        } else {
            sc2_resolve_object_model_candidates(object);
        }
        if (resolved_count < SC2_MAX_MAP_OBJECTS) {
            snprintf(resolved[resolved_count].name, sizeof(resolved[resolved_count].name), "%s", object->name);
            snprintf(resolved[resolved_count].model, sizeof(resolved[resolved_count].model), "%s", object->model);
            snprintf(resolved[resolved_count].footprint, sizeof(resolved[resolved_count].footprint), "%s", object->footprint);
            snprintf(resolved[resolved_count].mover, sizeof(resolved[resolved_count].mover), "%s", object->mover);
            resolved[resolved_count].radius = object->radius;
            resolved[resolved_count].footprint_width = object->footprint_width;
            resolved[resolved_count].footprint_height = object->footprint_height;
            resolved[resolved_count].footprint_radius = object->footprint_radius;
            resolved[resolved_count].unit_flags = object->unit_flags;
            resolved_count++;
        }
next_object:
        ;
    }
}

static DWORD sc2_count_unresolved_models(void) {
    DWORD count = 0;

    FOR_LOOP(i, sc2_map.num_objects) {
        sc2MapObject_t const *object = &sc2_map.objects[i];

        if ((object->type == SC2_OBJECT_UNIT ||
             object->type == SC2_OBJECT_DOODAD ||
             object->type == SC2_OBJECT_POINT) &&
            object->name[0] && !object->model[0]) {
            count++;
        }
    }
    return count;
}

static void sc2_resolve_catalogs(sc2MapSource_t *source) {
    sc2Catalog_t *catalog = sc2_alloc(sizeof(*catalog));

    if (!catalog) return;
    memset(catalog, 0, sizeof(*catalog));
    sc2_parse_catalogs(catalog, source);
    sc2_resolve_terrain_textures(catalog);
    sc2_resolve_cliff_sets(catalog);
    sc2_resolve_object_models(catalog);
    sc2_map.catalog.units = catalog->units_count;
    sc2_map.catalog.actors = catalog->actors_count;
    sc2_map.catalog.models = catalog->models_count;
    sc2_map.catalog.footprints = catalog->footprints_count;
    sc2_map.catalog.unresolved_models = sc2_count_unresolved_models();
    sc2_free(catalog);
}

static BOOL sc2_mapinfo_fourcc(sc2MapInfo_t const *mapInfo) {
    return mapInfo &&
        (mapInfo->fourcc == MAKEFOURCC('I','p','a','M') ||
         mapInfo->fourcc == MAKEFOURCC('M','a','p','I'));
}

static LPCSTR sc2_object_type_name(sc2ObjectType_t type) {
    switch (type) {
        case SC2_OBJECT_UNIT: return "unit";
        case SC2_OBJECT_DOODAD: return "doodad";
        case SC2_OBJECT_POINT: return "point";
        case SC2_OBJECT_CAMERA: return "camera";
    }
    return "unknown";
}

static void sc2_dump_cell_flags(FILE *out, sc2MapCellFlags_t const *layer) {
    DWORD counts[4] = { 0 };

    if (!out || !layer) return;
    FOR_LOOP(i, layer->width * layer->height) {
        if (layer->data[i] < SC2_ARRAY_LEN(counts))
            counts[layer->data[i]]++;
    }
    fprintf(out,
            "t3CellFlags: version=%u size=%ux%u counts[00]=%u counts[01]=%u counts[02]=%u counts[03]=%u\n",
            (unsigned)layer->version,
            (unsigned)layer->width,
            (unsigned)layer->height,
            (unsigned)counts[0],
            (unsigned)counts[1],
            (unsigned)counts[2],
            (unsigned)counts[3]);
}

static void sc2_dump_height_map(FILE *out, sc2MapHeightMap_t const *layer) {
    FLOAT min_height = 0.0f;
    FLOAT max_height = 0.0f;
    BOOL have_height = false;

    if (!out || !layer) return;
    FOR_LOOP(y, layer->height) {
        FOR_LOOP(x, layer->width) {
            FLOAT height = sc2_map_height_at_grid(&sc2_map, x, y);

            if (!have_height || height < min_height) min_height = height;
            if (!have_height || height > max_height) max_height = height;
            have_height = true;
        }
    }
    fprintf(out,
            "t3HeightMap: version=%u size=%ux%u min=%.3f max=%.3f\n",
            (unsigned)layer->version,
            (unsigned)layer->width,
            (unsigned)layer->height,
            min_height,
            max_height);
}

void SC2_MapDump(FILE *out, LPCSTR filename) {
    DWORD object_counts[4] = { 0 };

    if (!out) out = stdout;
    FOR_LOOP(i, sc2_map.num_objects) {
        if ((DWORD)sc2_map.objects[i].type < SC2_ARRAY_LEN(object_counts))
            object_counts[sc2_map.objects[i].type]++;
    }
    fprintf(out, "sc2map: file=%s\n", filename && *filename ? filename : "(current)");
    fprintf(out,
            "MapInfo: version=%u size=%ux%u name=\"%s\"\n",
            (unsigned)sc2_map.MapInfo.version,
            (unsigned)sc2_map.MapInfo.width,
            (unsigned)sc2_map.MapInfo.height,
            sc2_map.map_name);
    if (sc2_map.t3CellFlags)
        sc2_dump_cell_flags(out, sc2_map.t3CellFlags);
    if (sc2_map.t3HeightMap)
        sc2_dump_height_map(out, sc2_map.t3HeightMap);
    if (sc2_map.t3SyncHeightMap) {
        fprintf(out,
                "t3SyncHeightMap: version=%u size=%ux%u\n",
                (unsigned)sc2_map.t3SyncHeightMap->version,
                (unsigned)sc2_map.t3SyncHeightMap->width,
                (unsigned)sc2_map.t3SyncHeightMap->height);
    }
    if (sc2_map.t3SyncCliffLevel) {
        fprintf(out,
                "t3SyncCliffLevel: version=%u size=%ux%u\n",
                (unsigned)sc2_map.t3SyncCliffLevel->version,
                (unsigned)sc2_map.t3SyncCliffLevel->width,
                (unsigned)sc2_map.t3SyncCliffLevel->height);
    }
    if (sc2_map.t3TextureMasks) {
        fprintf(out,
                "t3TextureMasks: version=%u size=%ux%u bytes=%u\n",
                (unsigned)sc2_map.t3TextureMasks->version,
                (unsigned)sc2_map.t3TextureMasks->width,
                (unsigned)sc2_map.t3TextureMasks->height,
                (unsigned)sc2_map.t3TextureMasksSize);
    }
    fprintf(out,
            "Objects: units=%u doodads=%u points=%u cameras=%u total=%u\n",
            (unsigned)object_counts[SC2_OBJECT_UNIT],
            (unsigned)object_counts[SC2_OBJECT_DOODAD],
            (unsigned)object_counts[SC2_OBJECT_POINT],
            (unsigned)object_counts[SC2_OBJECT_CAMERA],
            (unsigned)sc2_map.num_objects);
    fprintf(out,
            "Catalog: units=%u actors=%u models=%u footprints=%u unresolvedModels=%u\n",
            (unsigned)sc2_map.catalog.units,
            (unsigned)sc2_map.catalog.actors,
            (unsigned)sc2_map.catalog.models,
            (unsigned)sc2_map.catalog.footprints,
            (unsigned)sc2_map.catalog.unresolved_models);
    FOR_LOOP(i, sc2_map.num_objects) {
        sc2MapObject_t const *object = &sc2_map.objects[i];

        fprintf(out,
                "Object[%u]: type=%s id=%u name=%s model=%s footprint=%s size=%.3fx%.3f fpRadius=%.3f mover=%s unitFlags=0x%08x pos=%.3f,%.3f,%.3f radius=%.3f\n",
                (unsigned)i,
                sc2_object_type_name(object->type),
                (unsigned)object->id,
                object->name,
                object->model,
                object->footprint,
                object->footprint_width,
                object->footprint_height,
                object->footprint_radius,
                object->mover,
                (unsigned)object->unit_flags,
                object->position.x,
                object->position.y,
                object->position.z,
                object->radius);
    }
}

static LPBYTE sc2_read_binary_layer(sc2MapSource_t *source,
                                    LPCSTR filename,
                                    DWORD min_size,
                                    DWORD expected_fourcc,
                                    LPDWORD out_size) {
    DWORD size = 0;
    DWORD fourcc = 0;
    LPBYTE data = sc2_source_read(source, filename, &size);
    LPBYTE copy;

    if (out_size) *out_size = 0;
    if (!data || size < min_size || size < sizeof(fourcc)) {
        sc2_free_file(data);
        return NULL;
    }
    memcpy(&fourcc, data, sizeof(fourcc));
    if (fourcc != expected_fourcc) {
        sc2_free_file(data);
        return NULL;
    }
    copy = sc2_alloc(size);
    if (!copy) {
        sc2_free_file(data);
        return NULL;
    }
    memcpy(copy, data, size);
    sc2_free_file(data);
    if (out_size) *out_size = size;
    return copy;
}

static BOOL sc2_binary_layer_payload_valid(DWORD size,
                                           DWORD header_size,
                                           DWORD width,
                                           DWORD height,
                                           DWORD sample_size) {
    size_t count;
    size_t payload_size;
    size_t total_size;

    if (!width || !height || !sample_size)
        return false;
    if ((size_t)width > (size_t)-1 / (size_t)height)
        return false;
    count = (size_t)width * (size_t)height;
    if (count > ((size_t)-1 - (size_t)header_size) / (size_t)sample_size)
        return false;
    payload_size = count * (size_t)sample_size;
    total_size = (size_t)header_size + payload_size;
    return (size_t)size >= total_size;
}

static void sc2_parse_height_map(sc2MapSource_t *source) {
    DWORD size = 0;

    sc2_map.t3HeightMap = (sc2MapHeightMap_t *)sc2_read_binary_layer(source,
                                                                     "t3HeightMap",
                                                                     sizeof(sc2MapHeightMap_t),
                                                                     MAKEFOURCC('H','M','A','P'),
                                                                     &size);
    if (sc2_map.t3HeightMap &&
        !sc2_binary_layer_payload_valid(size,
                                        sizeof(sc2MapHeightMap_t),
                                        sc2_map.t3HeightMap->width,
                                        sc2_map.t3HeightMap->height,
                                        sizeof(sc2MapHeightSample_t))) {
        SAFE_DELETE(sc2_map.t3HeightMap, sc2_free);
    }
}

static void sc2_parse_sync_height_map(sc2MapSource_t *source) {
    DWORD size = 0;

    sc2_map.t3SyncHeightMap = (sc2MapSyncHeightMap_t *)sc2_read_binary_layer(source,
                                                                             "t3SyncHeightMap",
                                                                             sizeof(sc2MapSyncHeightMap_t),
                                                                             MAKEFOURCC('S','M','A','P'),
                                                                             &size);
    if (sc2_map.t3SyncHeightMap &&
        !sc2_binary_layer_payload_valid(size,
                                        sizeof(sc2MapSyncHeightMap_t),
                                        sc2_map.t3SyncHeightMap->width,
                                        sc2_map.t3SyncHeightMap->height,
                                        sizeof(sc2MapSyncHeightSample_t))) {
        SAFE_DELETE(sc2_map.t3SyncHeightMap, sc2_free);
    }
}

static void sc2_parse_cell_flags(sc2MapSource_t *source) {
    DWORD size = 0;

    sc2_map.t3CellFlags = (sc2MapCellFlags_t *)sc2_read_binary_layer(source,
                                                                     "t3CellFlags",
                                                                     sizeof(sc2MapCellFlags_t),
                                                                     MAKEFOURCC('L','F','C','T'),
                                                                     &size);
    if (sc2_map.t3CellFlags &&
        !sc2_binary_layer_payload_valid(size,
                                        sizeof(sc2MapCellFlags_t),
                                        sc2_map.t3CellFlags->width,
                                        sc2_map.t3CellFlags->height,
                                        sizeof(BYTE))) {
        SAFE_DELETE(sc2_map.t3CellFlags, sc2_free);
    }
}

static void sc2_parse_sync_cliff_level(sc2MapSource_t *source) {
    DWORD size = 0;

    sc2_map.t3SyncCliffLevel = (sc2MapSyncCliffLevel_t *)sc2_read_binary_layer(source,
                                                                               "t3SyncCliffLevel",
                                                                               sizeof(sc2MapSyncCliffLevel_t),
                                                                               MAKEFOURCC('C','L','I','F'),
                                                                               &size);
    if (sc2_map.t3SyncCliffLevel &&
        !sc2_binary_layer_payload_valid(size,
                                        sizeof(sc2MapSyncCliffLevel_t),
                                        sc2_map.t3SyncCliffLevel->width,
                                        sc2_map.t3SyncCliffLevel->height,
                                        sizeof(USHORT))) {
        SAFE_DELETE(sc2_map.t3SyncCliffLevel, sc2_free);
    }
}

static void sc2_parse_texture_masks(sc2MapSource_t *source) {
    sc2_map.t3TextureMasks = (sc2MapTextureMasks_t *)sc2_read_binary_layer(source,
                                                                           "t3TextureMasks",
                                                                           sizeof(sc2MapTextureMasks_t),
                                                                           MAKEFOURCC('M','A','S','K'),
                                                                           &sc2_map.t3TextureMasksSize);
    if (sc2_map.t3TextureMasks &&
        !sc2_binary_layer_payload_valid(sc2_map.t3TextureMasksSize,
                                        sizeof(sc2MapTextureMasks_t),
                                        sc2_map.t3TextureMasks->width,
                                        sc2_map.t3TextureMasks->height,
                                        sizeof(BYTE))) {
        SAFE_DELETE(sc2_map.t3TextureMasks, sc2_free);
        sc2_map.t3TextureMasksSize = 0;
    }
}

void SC2_MapSetHost(sc2MapHost_t const *host) {
    memset(&sc2_host, 0, sizeof(sc2_host));
    if (host) sc2_host = *host;
}

BOOL SC2_MapLoad(LPCSTR mapFilename) {
    sc2MapSource_t source;
    sc2_map_clear();
    sc2_map.cell_size = SC2_CELL_SIZE;
    if (!sc2_source_open(&source, mapFilename)) {
        return false;
    }
    sc2_parse_mapinfo(&source);
    sc2_parse_terrain(&source);
    sc2_parse_terrain_data_catalogs();
    sc2_parse_terrain_data(&source);
    sc2_parse_light_data_catalogs();
    sc2_parse_light_data(&source);
    sc2_parse_height_map(&source);
    /* t3SyncHeightMap is not visible terrain height; merging it creates cliff spikes. */
    sc2_parse_sync_height_map(&source);
    sc2_parse_cell_flags(&source);
    sc2_parse_sync_cliff_level(&source);
    sc2_parse_texture_masks(&source);
    sc2_parse_objects(&source);
    sc2_resolve_catalogs(&source);
    sc2_source_close(&source);
    sc2_map.origin.x = 0.0f;
    sc2_map.origin.y = 0.0f;
    fprintf(stderr, "SC2_MapLoad: %s %ux%u objects=%u\n",
            sc2_map.map_name, (unsigned)sc2_map_width(), (unsigned)sc2_map_height(),
            (unsigned)sc2_map.num_objects);
    return true;
}

void SC2_MapShutdown(void) {
    sc2_map_clear();
}

sc2Map_t *SC2_MapCurrent(void) {
    return &sc2_map;
}

FLOAT SC2_MapHeightAtPoint(FLOAT x, FLOAT y) {
    return sc2_map_height_at_point(&sc2_map, x, y);
}

BOX2 SC2_MapBounds(void) {
    return (BOX2){
        .min = sc2_map.origin,
        .max = {
            sc2_map.origin.x + sc2_map_width() * sc2_map.cell_size,
            sc2_map.origin.y + sc2_map_height() * sc2_map.cell_size,
        },
    };
}

VECTOR2 SC2_MapNormalizedPosition(FLOAT x, FLOAT y) {
    BOX2 bounds = SC2_MapBounds();
    return (VECTOR2){
        (x - bounds.min.x) / MAX(1.0f, bounds.max.x - bounds.min.x),
        (y - bounds.min.y) / MAX(1.0f, bounds.max.y - bounds.min.y),
    };
}

VECTOR2 SC2_MapDenormalizedPosition(FLOAT x, FLOAT y) {
    BOX2 bounds = SC2_MapBounds();
    return (VECTOR2){
        bounds.min.x + x * (bounds.max.x - bounds.min.x),
        bounds.min.y + y * (bounds.max.y - bounds.min.y),
    };
}

DWORD SC2_MapObjectClassId(sc2MapObject_t const *object) {
    return object && object->name[0] ? sc2_hash32(object->name) : 0;
}

BOOL SC2_MapDefaultCamera(sc2MapCamera_t *camera) {
    sc2MapCamera_t value = { 0 };

    if (!camera) {
        return false;
    }
    if (sc2_map.MapInfo.width && sc2_map.MapInfo.height) {
        value.target = (VECTOR3){
            sc2_map.origin.x + (FLOAT)sc2_map.MapInfo.width * sc2_map.cell_size * 0.5f,
            sc2_map.origin.y + (FLOAT)sc2_map.MapInfo.height * sc2_map.cell_size * 0.5f,
            0.0f,
        };
    }
    value.distance = 34.07f;
    value.pitch = 56.0f;
    value.yaw = 179.9584f;
    value.fov = 27.7998f;
    value.znear = 0.0998f;
    value.zfar = 400.0f;

    FOR_LOOP(i, sc2_map.num_objects) {
        sc2MapObject_t const *object = &sc2_map.objects[i];

        if (object->type != SC2_OBJECT_CAMERA) {
            continue;
        }
        if (object->camera.target.x != 0.0f ||
            object->camera.target.y != 0.0f ||
            object->camera.target.z != 0.0f) {
            value.target = object->camera.target;
        }
        if (object->camera.distance > 0.0f) value.distance = object->camera.distance;
        if (object->camera.pitch != 0.0f) value.pitch = object->camera.pitch;
        if (object->camera.yaw != 0.0f) value.yaw = object->camera.yaw;
        if (object->camera.fov > 0.0f) value.fov = object->camera.fov;
        if (object->camera.znear > 0.0f) value.znear = object->camera.znear;
        if (object->camera.zfar > 0.0f) value.zfar = object->camera.zfar;
        if (object->camera.height_offset != 0.0f) value.height_offset = object->camera.height_offset;
        break;
    }

    *camera = value;
    return true;
}
