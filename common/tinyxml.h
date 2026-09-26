/*
 * tinyxml.h — minimal self-contained XML DOM parser (no libxml2 dependency).
 *
 * Provides a libxml2-compatible subset of the DOM API used by the WoW FrameXML,
 * SC2 layout, and SC2 map parsers: xmlDoc/xmlNode/xmlAttr types, xmlReadMemory,
 * xmlParseMemory, xmlDocGetRootElement, xmlGetProp, xmlNodeGetContent,
 * xmlNodeListGetString, xmlStrcasecmp, and the XML_PARSE_* / BAD_CAST macros.
 *
 * Design notes:
 *   - All strings and nodes are allocated from a single per-document arena that
 *     is freed by xmlFreeDoc; xmlFree is therefore a no-op.  Callers only read
 *     the returned strings (strdup/copy) before the document is freed, matching
 *     libxml2's xmlGetProp/xmlNodeGetContent/xmlNodeListGetString call pattern.
 *   - Nodes use the same linked-list layout libxml2 exposes: children/next for
 *     element siblings, properties/next for attributes, and an attribute's
 *     value is stored as a text node under attr->children.
 *   - Parsing is lenient (equivalent to XML_PARSE_RECOVER): comments, processing
 *     instructions, and DOCTYPE declarations are skipped; CDATA becomes text.
 *   - The five standard XML entities and numeric character references are
 *     decoded in attribute values and text content.
 *
 * Header-only: every function is static and the include guard prevents
 * redefinition inside unity builds (a single translation unit that #includes
 * many .c files each pulling this header).
 */
#ifndef tinyxml_h
#define tinyxml_h

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char xmlChar;

#define BAD_CAST (xmlChar const *)

typedef struct _xmlNode xmlNode;
typedef struct _xmlAttr xmlAttr;
typedef struct _xmlDoc xmlDoc;

enum {
    XML_ELEMENT_NODE = 1,
    XML_TEXT_NODE    = 3,
};

enum {
    XML_PARSE_RECOVER   = 1 << 0,
    XML_PARSE_NOERROR   = 1 << 1,
    XML_PARSE_NOWARNING = 1 << 2,
    XML_PARSE_NONET     = 1 << 3,
    XML_PARSE_NOBLANKS  = 1 << 4,
};

struct _xmlAttr {
    xmlAttr *next;
    xmlNode *children;   /* single text node holding the value */
    xmlChar *name;
};

struct _xmlNode {
    void *_private;
    xmlNode *children;   /* first child */
    xmlNode *next;       /* next sibling */
    xmlNode *parent;
    xmlAttr *properties; /* first attribute */
    xmlChar *name;         /* element name, or NULL for text nodes */
    xmlChar *content;      /* text content (text nodes only) */
    int type;
    xmlDoc *doc;
};

/* The arena is a linked list of blocks so node/attr pointers stay valid across
 * growth (a single realloc'd buffer would invalidate every tree pointer). */
typedef struct xml_arena_block xml_arena_block;
struct xml_arena_block {
    xml_arena_block *next;
    size_t size;
    char data[];
};

struct _xmlDoc {
    xmlNode *root;
    xml_arena_block *blocks;
    char *cur;
    size_t left;
};

static void *xml_arena_alloc(xmlDoc *doc, size_t size) {
    size = (size + 7u) & ~(size_t)7u;
    if (size > doc->left) {
        size_t bsize = size > 8192 ? size : 8192;
        xml_arena_block *b = (xml_arena_block *)malloc(sizeof(*b) + bsize);
        if (!b) return NULL;
        b->next = doc->blocks; b->size = bsize;
        doc->blocks = b;
        doc->cur = b->data; doc->left = bsize;
    }
    char *p = doc->cur;
    doc->cur += size; doc->left -= size;
    return p;
}

static char *xml_arena_str(xmlDoc *doc, char const *s, size_t len) {
    char *p = (char *)xml_arena_alloc(doc, len + 1);
    if (!p) return NULL;
    memcpy(p, s, len); p[len] = 0;
    return p;
}

/* Copy s[0..len) into the arena, decoding &amp; &lt; &gt; &quot; &apos; and &#NNN;. */
static xmlChar *xml_arena_unescape(xmlDoc *doc, char const *s, size_t len) {
    char *out = (char *)xml_arena_alloc(doc, len + 1); /* decoded is never longer than source */
    size_t o = 0, i;
    if (!out) return NULL;
    for (i = 0; i < len; ) {
        if (s[i] == '&' && i + 2 < len) {
            size_t j = i + 1; int done = 0;
            while (j < len && s[j] != ';' && j - i < 12) j++;
            if (j < len && s[j] == ';') {
                size_t elen = j - i - 1; char const *b = s + i + 1;
                if      (elen == 2 && b[0]=='l' && b[1]=='t') { out[o++]='<'; done=1; }
                else if (elen == 2 && b[0]=='g' && b[1]=='t') { out[o++]='>'; done=1; }
                else if (elen == 3 && b[0]=='a' && b[1]=='m' && b[2]=='p') { out[o++]='&'; done=1; }
                else if (elen == 4 && b[0]=='q' && b[1]=='u' && b[2]=='o' && b[3]=='t') { out[o++]='"'; done=1; }
                else if (elen == 4 && b[0]=='a' && b[1]=='p' && b[2]=='o' && b[3]=='s') { out[o++]='\''; done=1; }
                else if (b[0]=='#') {
                    long v = 0; int base = 10, valid = 1; size_t k = 1;
                    if (b[1]=='x' || b[1]=='X') { base = 16; k = 2; }
                    for (; k < elen; k++) {
                        char c = b[k]; int d;
                        if      (c>='0' && c<='9') d = c-'0';
                        else if (base==16 && c>='a' && c<='f') d = c-'a'+10;
                        else if (base==16 && c>='A' && c<='F') d = c-'A'+10;
                        else { valid = 0; break; }
                        v = v*base + d;
                    }
                    if (valid && v > 0 && v <= 0x10FFFF) {
                        if      (v < 0x80)     out[o++] = (char)v;
                        else if (v < 0x800)    { out[o++]=(char)(0xC0|(v>>6));  out[o++]=(char)(0x80|(v&0x3F)); }
                        else if (v < 0x10000)  { out[o++]=(char)(0xE0|(v>>12)); out[o++]=(char)(0x80|((v>>6)&0x3F)); out[o++]=(char)(0x80|(v&0x3F)); }
                        else                   { out[o++]=(char)(0xF0|(v>>18)); out[o++]=(char)(0x80|((v>>12)&0x3F)); out[o++]=(char)(0x80|((v>>6)&0x3F)); out[o++]=(char)(0x80|(v&0x3F)); }
                        done = 1;
                    }
                }
                if (done) { i = j + 1; continue; }
            }
        }
        out[o++] = s[i++];
    }
    out[o] = 0;
    return (xmlChar *)out;
}

static xmlDoc *xml_new_doc(void) {
    return (xmlDoc *)calloc(1, sizeof(xmlDoc));
}

static xmlNode *xml_new_node(xmlDoc *doc, int type) {
    xmlNode *n = (xmlNode *)xml_arena_alloc(doc, sizeof(*n));
    memset(n, 0, sizeof(*n));
    n->type = type; n->doc = doc;
    return n;
}

static void xml_add_child(xmlNode *parent, xmlNode *child) {
    xmlNode *t;
    child->parent = parent;
    if (!parent->children) { parent->children = child; return; }
    for (t = parent->children; t->next; t = t->next) ;
    t->next = child;
}

static void xml_add_attr(xmlNode *node, char const *name, size_t nlen, char const *val, size_t vlen) {
    xmlDoc *doc = node->doc; xmlAttr *a = (xmlAttr *)xml_arena_alloc(doc, sizeof(*a));
    memset(a, 0, sizeof(*a));
    a->name = (xmlChar *)xml_arena_str(doc, name, nlen);
    a->children = xml_new_node(doc, XML_TEXT_NODE);
    a->children->content = xml_arena_unescape(doc, val, vlen);
    a->next = node->properties;
    node->properties = a;
}

static void xml_skip_ws(char const **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static xmlNode *xml_parse_node(xmlDoc *doc, char const **p);
static void xmlFreeDoc(xmlDoc *doc);

static void xml_parse_children(xmlDoc *doc, xmlNode *parent, char const **p) {
    for (;;) {
        xml_skip_ws(p);
        if (!**p || (**p == '<' && (*p)[1] == '/')) return;
        if (**p == '<') {
            xmlNode *child;
            if (!strncmp(*p, "<!--", 4)) { char const *e = strstr(*p, "-->"); *p = e ? e + 3 : *p + strlen(*p); continue; }
            if ((*p)[1] == '?')          { char const *e = strstr(*p, "?>"); *p = e ? e + 2 : *p + strlen(*p); continue; }
            if (!strncmp(*p, "<![CDATA[", 9)) {
                char const *start = *p + 9, *end = strstr(start, "]]>");
                xmlNode *t;
                if (!end) end = start + strlen(start);
                t = xml_new_node(doc, XML_TEXT_NODE);
                t->content = (xmlChar *)xml_arena_str(doc, start, (size_t)(end - start));
                xml_add_child(parent, t);
                *p = end + 3;
                continue;
            }
            child = xml_parse_node(doc, p);
            if (!child) return;
            xml_add_child(parent, child);
            continue;
        }
        {
            char const *start = *p; xmlNode *t;
            while (**p && **p != '<') (*p)++;
            t = xml_new_node(doc, XML_TEXT_NODE);
            t->content = xml_arena_unescape(doc, start, (size_t)(*p - start));
            xml_add_child(parent, t);
        }
    }
}

static xmlNode *xml_parse_node(xmlDoc *doc, char const **p) {
    xmlNode *n;
    char const *ns; size_t nlen;
    xml_skip_ws(p);
    if (**p != '<') return NULL;
    (*p)++;
    ns = *p;
    while (**p && !isspace((unsigned char)**p) && **p != '>' && **p != '/') (*p)++;
    nlen = (size_t)(*p - ns);
    if (!nlen) return NULL;
    n = xml_new_node(doc, XML_ELEMENT_NODE);
    n->name = (xmlChar *)xml_arena_str(doc, ns, nlen);
    for (;;) {
        xml_skip_ws(p);
        if (!**p) break;
        if (**p == '>') {
            (*p)++; xml_parse_children(doc, n, p); xml_skip_ws(p);
            if (!strncmp(*p, "</", 2)) { char const *e = strchr(*p, '>'); *p = e ? e + 1 : *p + strlen(*p); }
            return n;
        }
        if (**p == '/') { if ((*p)[1] == '>') *p += 2; else (*p)++; return n; }
        {
            char const *as = *p, *vs = ""; size_t alen, vlen = 0;
            while (**p && !isspace((unsigned char)**p) && **p != '=' && **p != '>' && **p != '/') (*p)++;
            alen = (size_t)(*p - as);
            if (!alen) { (*p)++; continue; }
            xml_skip_ws(p);
            if (**p == '=') {
                (*p)++; xml_skip_ws(p);
                if (**p == '"' || **p == '\'') {
                    char q = *(*p)++; vs = *p;
                    while (**p && **p != q) (*p)++;
                    vlen = (size_t)(*p - vs);
                    if (**p == q) (*p)++;
                } else {
                    vs = *p;
                    while (**p && !isspace((unsigned char)**p) && **p != '>' && **p != '/') (*p)++;
                    vlen = (size_t)(*p - vs);
                }
            }
            xml_add_attr(n, as, alen, vs, vlen);
        }
    }
    return n;
}

static xmlDoc *xml_read_memory(char const *buf, int size) {
    char *copy; char const *p; xmlDoc *doc;
    if (!buf || size <= 0) return NULL;
    copy = (char *)malloc((size_t)size + 1);
    if (!copy) return NULL;
    memcpy(copy, buf, (size_t)size); copy[size] = 0;
    p = copy;
    for (;;) {
        xml_skip_ws(&p);
        if (!*p) { free(copy); return NULL; }
        if (!strncmp(p, "<?", 2))     { char const *e = strstr(p, "?>"); p = e ? e + 2 : p + strlen(p); continue; }
        if (!strncmp(p, "<!--", 4))   { char const *e = strstr(p, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
        if (!strncmp(p, "<!", 2))     { char const *e = strchr(p, '>'); p = e ? e + 1 : p + strlen(p); continue; }
        break;
    }
    if (*p != '<') { free(copy); return NULL; }
    doc = xml_new_doc();
    if (!doc) { free(copy); return NULL; }
    doc->root = xml_parse_node(doc, &p);
    if (!doc->root) { xmlFreeDoc(doc); free(copy); return NULL; }
    free(copy);
    return doc;
}

static xmlDoc *xmlReadMemory(char const *buf, int size, char const *url, char const *enc, int opts) {
    (void)url; (void)enc; (void)opts;
    return xml_read_memory(buf, size);
}

static xmlDoc *xmlParseMemory(char const *buf, int size) {
    return xml_read_memory(buf, size);
}

static xmlNode *xmlDocGetRootElement(xmlDoc *doc) {
    return doc ? doc->root : NULL;
}

static void xmlFreeDoc(xmlDoc *doc) {
    xml_arena_block *b, *n;
    if (!doc) return;
    for (b = doc->blocks; b; b = n) { n = b->next; free(b); }
    free(doc);
}

static void xmlFree(void *p) { (void)p; }

static int xmlStrcasecmp(xmlChar const *a, xmlChar const *b) {
    return strcasecmp((char const *)a, (char const *)b);
}

static xmlChar *xmlGetProp(xmlNode *node, xmlChar const *name) {
    xmlAttr *a;
    if (!node || !name) return NULL;
    for (a = node->properties; a; a = a->next)
        if (!strcmp((char const *)a->name, (char const *)name))
            return a->children ? a->children->content : NULL;
    return NULL;
}

static xmlChar *xmlNodeListGetString(xmlDoc *doc, xmlNode *list, int inLine) {
    size_t total = 0, pos = 0; xmlNode *n; char *out;
    (void)inLine;
    if (!doc || !list) return NULL;
    for (n = list; n; n = n->next)
        if (n->type == XML_TEXT_NODE && n->content) total += strlen((char const *)n->content);
    if (!total) return NULL;
    out = (char *)xml_arena_alloc(doc, total + 1);
    for (n = list; n; n = n->next)
        if (n->type == XML_TEXT_NODE && n->content) {
            size_t l = strlen((char const *)n->content);
            memcpy(out + pos, n->content, l); pos += l;
        }
    out[pos] = 0;
    return (xmlChar *)out;
}

static size_t xml_content_len(xmlNode *n) {
    xmlNode *c; size_t total = 0;
    if (!n) return 0;
    if (n->type == XML_TEXT_NODE) return n->content ? strlen((char const *)n->content) : 0;
    if (n->type != XML_ELEMENT_NODE) return 0;
    for (c = n->children; c; c = c->next) total += xml_content_len(c);
    return total;
}

static void xml_content_fill(xmlNode *n, char *out, size_t *pos) {
    xmlNode *c;
    if (!n) return;
    if (n->type == XML_TEXT_NODE) {
        if (n->content) { size_t l = strlen((char const *)n->content); memcpy(out + *pos, n->content, l); *pos += l; }
        return;
    }
    if (n->type != XML_ELEMENT_NODE) return;
    for (c = n->children; c; c = c->next) xml_content_fill(c, out, pos);
}

static xmlChar *xmlNodeGetContent(xmlNode *n) {
    size_t len, pos = 0; char *out;
    if (!n) return NULL;
    len = xml_content_len(n);
    if (!len) return NULL;
    out = (char *)xml_arena_alloc(n->doc, len + 1);
    xml_content_fill(n, out, &pos);
    out[pos] = 0;
    return (xmlChar *)out;
}

#ifdef __cplusplus
}
#endif

#endif /* tinyxml_h */
