/* Private catalog parsing/resolution for the supported damage-effect subset.
 * Included by sc2_map.c; unknown graphs/validators fail rather than inventing damage. */
static int sc2_catalog_target_bit(cstring_t name);
static int sc2_effect_index(xmlNode *node, uint32_t *next, uint32_t capacity) {
    char index[16];
    int at=sc2_xml_attr(node,"index",index,sizeof(index)) ? atoi(index) : (int)(*next)++;
    return at>=0 && (uint32_t)at<capacity ? at : -1;
}
static void sc2_parse_combat_catalog_doc(sc2Catalog_t *catalog, xmlDoc *doc) {
    xmlNode *root=xmlDocGetRootElement(doc);
    for (xmlNode *node=root ? root->children : NULL;node;node=node->next) {
        cstring_t tag=(cstring_t)node->name;
        char id[64],value[256];
        bool damage=sc2_streqi(tag,"CEffectDamage"),set=sc2_streqi(tag,"CEffectSet");
        if (!damage && !set && !sc2_contains_i(tag,"CValidator")) continue;
        if (!sc2_xml_attr(node,"id",id,sizeof(id))) {
            if (!sc2_xml_attr(node,"default",value,sizeof(value)) || !atoi(value)) continue;
            snprintf(id,sizeof(id),"$%s",tag);
        }
        if (!damage && !set) {
            sc2CatalogValidator_t *v=NULL;
            FOR_LOOP(i,catalog->validators_count) if (!strcmp(catalog->validators[i].id,id)) { v=&catalog->validators[i]; break; }
            if (!v) {
                if (catalog->validators_count==SC2_ARRAY_LEN(catalog->validators)) { fprintf(stderr,"SC2 validator catalog full: %s\n",id); continue; }
                v=&catalog->validators[catalog->validators_count++];
                snprintf(v->id,sizeof(v->id),"%s",id);
            }
            v->supported=sc2_streqi(tag,"CValidatorUnitCompareField");
            if (sc2_xml_attr(node,"parent",value,sizeof(value))) v->supported=false;
            for (xmlNode *c=node->children;c;c=c->next) {
                if (sc2_streqi((cstring_t)c->name,"WhichUnit")) sc2_xml_attr(c,"Value",v->which,sizeof(v->which));
                else if (sc2_streqi((cstring_t)c->name,"Field")) sc2_xml_attr(c,"value",v->field,sizeof(v->field));
                else if (sc2_streqi((cstring_t)c->name,"Compare")) sc2_xml_attr(c,"value",v->compare,sizeof(v->compare));
                else if (sc2_streqi((cstring_t)c->name,"Value") && sc2_xml_attr(c,"value",value,sizeof(value))) v->value=strtof(value,NULL);
            }
            continue;
        }
        sc2CatalogDamage_t *e=NULL;
        FOR_LOOP(i,catalog->damage_count) if (!strcmp(catalog->damage[i].id,id)) { e=&catalog->damage[i]; break; }
        if (!e) {
            if (catalog->damage_count==SC2_ARRAY_LEN(catalog->damage)) { fprintf(stderr,"SC2 effect catalog full: %s\n",id); continue; }
            e=&catalog->damage[catalog->damage_count++];
            snprintf(e->id,sizeof(e->id),"%s",id);
        }
        e->kind=set ? SC2_EFFECT_SET : SC2_EFFECT_DAMAGE;
        sc2_xml_attr(node,"parent",e->parent,sizeof(e->parent));
        uint32_t area=0,child=0,validator=0;
        for (xmlNode *c=node->children;c;c=c->next) {
            cstring_t name=(cstring_t)c->name;
            bool removed=sc2_xml_attr(c,"removed",value,sizeof(value)) && atoi(value);
            if (sc2_streqi(name,"Amount") && sc2_xml_attr(c,"value",value,sizeof(value))) {
                e->damage.amount=strtof(value,NULL); e->fields|=SC2_EF_AMOUNT;
            } else if (sc2_streqi(name,"SearchFilters") && sc2_xml_attr(c,"value",e->filters,sizeof(e->filters))) e->fields|=SC2_EF_FILTER;
            else if (sc2_streqi(name,"SearchFlags") && sc2_xml_attr(c,"index",value,sizeof(value)) && !strcmp(value,"OffsetByUnitRadius")) {
                if (sc2_xml_attr(c,"value",value,sizeof(value))) { e->damage.offset_by_radius=atoi(value)!=0; e->fields|=SC2_EF_OFFSET; }
            } else if (sc2_streqi(name,"ExcludeArray") && sc2_xml_attr(c,"Value",value,sizeof(value))) {
                if (!strcmp(value,"Target")) { e->damage.exclude_target=!removed; e->fields|=SC2_EF_EXCLUDE; }
                else e->invalid=true;
            } else if (sc2_streqi(name,"AreaArray")) {
                int at=sc2_effect_index(c,&area,SC2_DAMAGE_AREAS);
                if (at<0) { e->invalid=true; continue; }
                e->area_mask|=1u<<at;
                sc2DamageArea_t *a=&e->damage.areas[at];
                if (removed) { e->area_fields[at]=4; continue; }
                e->area_fields[at]&=~4u;
                if (sc2_xml_attr(c,"Radius",value,sizeof(value))) { a->radius=strtof(value,NULL); e->area_fields[at]|=1; }
                if (sc2_xml_attr(c,"Fraction",value,sizeof(value))) { a->fraction=strtof(value,NULL); e->area_fields[at]|=2; }
            } else if (sc2_streqi(name,"EffectArray") || sc2_streqi(name,"ValidatorArray")) {
                bool val=sc2_streqi(name,"ValidatorArray");
                int at=sc2_effect_index(c,val ? &validator : &child,val ? SC2_DAMAGE_VALIDATORS : SC2_DAMAGE_EFFECTS);
                if (at<0) { e->invalid=true; continue; }
                char *link=val ? e->validators[at] : e->children[at];
                if (val) e->validator_mask|=1u<<at; else e->child_mask|=1u<<at;
                if (removed) *link=0; else sc2_xml_attr(c,"value",link,64);
            }
        }
    }
}
static sc2CatalogDamage_t const *sc2_find_effect(cstring_t id) {
    FOR_LOOP(i,sc2_persistent_catalog->damage_count) if (!strcmp(sc2_persistent_catalog->damage[i].id,id)) return &sc2_persistent_catalog->damage[i];
    return NULL;
}
static bool sc2_resolve_effect(cstring_t id, sc2CatalogDamage_t *out, uint32_t depth) {
    sc2CatalogDamage_t const *e=sc2_find_effect(id);
    if (!e || e->invalid || depth>=SC2_MAX_CATALOG_PARENT_DEPTH) return false;
    if (*e->parent && !sc2_resolve_effect(e->parent,out,depth+1)) return false;
    out->kind=e->kind;
    if (e->fields&SC2_EF_AMOUNT) out->damage.amount=e->damage.amount;
    if (e->fields&SC2_EF_FILTER) snprintf(out->filters,sizeof(out->filters),"%s",e->filters);
    if (e->fields&SC2_EF_OFFSET) out->damage.offset_by_radius=e->damage.offset_by_radius;
    if (e->fields&SC2_EF_EXCLUDE) out->damage.exclude_target=e->damage.exclude_target;
    FOR_LOOP(i,SC2_DAMAGE_AREAS) if (e->area_mask&(1u<<i)) {
        if (!(out->area_mask&(1u<<i))) out->damage.areas[i]=(sc2DamageArea_t){.fraction=1};
        if (e->area_fields[i]&4) out->damage.areas[i].radius=-1;
        else {
            if (e->area_fields[i]&1) out->damage.areas[i].radius=e->damage.areas[i].radius;
            if (e->area_fields[i]&2) out->damage.areas[i].fraction=e->damage.areas[i].fraction;
        }
    }
    FOR_LOOP(i,SC2_DAMAGE_EFFECTS) if (e->child_mask&(1u<<i)) snprintf(out->children[i],64,"%s",e->children[i]);
    FOR_LOOP(i,SC2_DAMAGE_VALIDATORS) if (e->validator_mask&(1u<<i)) snprintf(out->validators[i],64,"%s",e->validators[i]);
    out->area_mask|=e->area_mask;
    return true;
}
static bool sc2_damage_filter(cstring_t text, sc2DamageEffect_t *damage) {
    char copy[256]; snprintf(copy,sizeof(copy),"%s",text);
    char *excluded=strchr(copy,';');
    if (excluded) *excluded++=0;
    for (int part=0;part<2;part++) {
        if (part && !excluded) continue;
        char *save=NULL;
        for (char *word=strtok_r(part ? excluded : copy,",",&save);word;word=strtok_r(NULL,",",&save)) {
            if (!strcmp(word,"-")) continue;
            int bit=sc2_catalog_target_bit(word);
            if (bit<0) return false;
            if (part) damage->excluded|=1ull<<bit; else damage->required|=1ull<<bit;
        }
    }
    return true;
}
static bool sc2_compile_effect(cstring_t id, sc2WeaponPresentation_t *out, sc2RadiusTest_t const *inherited, uint32_t inherited_n, uint32_t depth) {
    if (depth>=SC2_MAX_CATALOG_PARENT_DEPTH) { fprintf(stderr,"SC2 effect cycle/depth: %s\n",id); return false; }
    sc2CatalogDamage_t e={0};
    sc2CatalogDamage_t const *raw=sc2_find_effect(id);
    if (!raw) { fprintf(stderr,"SC2 unsupported/unresolved weapon effect: %s\n",id); return false; }
    cstring_t base=raw->kind==SC2_EFFECT_SET ? "$CEffectSet" : "$CEffectDamage";
    if (strcmp(id,base) && sc2_find_effect(base) && !sc2_resolve_effect(base,&e,0)) return false;
    if (!sc2_resolve_effect(id,&e,0)) { fprintf(stderr,"SC2 invalid effect inheritance: %s\n",id); return false; }
    sc2RadiusTest_t tests[SC2_DAMAGE_VALIDATORS];
    uint32_t count=inherited_n;
    if (count) memcpy(tests,inherited,count*sizeof(*tests));
    FOR_LOOP(i,SC2_DAMAGE_VALIDATORS) if (*e.validators[i]) {
        sc2CatalogValidator_t const *v=NULL;
        FOR_LOOP(j,sc2_persistent_catalog->validators_count) if (!strcmp(sc2_persistent_catalog->validators[j].id,e.validators[i])) { v=&sc2_persistent_catalog->validators[j]; break; }
        static cstring_t const ops[]={"LT","LE","EQ","NE","GE","GT"};
        int op=-1;
        if (v) FOR_LOOP(j,SC2_ARRAY_LEN(ops)) if (!strcmp(v->compare,ops[j])) op=(int)j;
        if (!v || !v->supported || strcmp(v->field,"Radius") || (*v->which && strcmp(v->which,"Target")) || op<0 || !isfinite(v->value) || count==SC2_DAMAGE_VALIDATORS) {
            fprintf(stderr,"SC2 unsupported weapon validator: %s (%s)\n",e.validators[i],id); return false;
        }
        tests[count++]=(sc2RadiusTest_t){v->value,(uint8_t)op};
    }
    if (e.kind==SC2_EFFECT_SET) {
        bool any=false;
        FOR_LOOP(i,SC2_DAMAGE_EFFECTS) if (*e.children[i]) {
            any=true;
            if (!sc2_compile_effect(e.children[i],out,tests,count,depth+1)) return false;
        }
        return any;
    }
    if (out->effect_count==SC2_DAMAGE_EFFECTS || !isfinite(e.damage.amount) || e.damage.amount<0) return false;
    sc2DamageEffect_t damage=e.damage;
    damage.validator_count=(uint8_t)count;
    if (count) memcpy(damage.validators,tests,count*sizeof(*tests));
    damage.area_count=0;
    FOR_LOOP(i,SC2_DAMAGE_AREAS) if ((e.area_mask&(1u<<i)) && e.damage.areas[i].radius>=0) {
        sc2DamageArea_t a=e.damage.areas[i];
        if (!isfinite(a.radius) || !isfinite(a.fraction) || a.fraction<0) return false;
        damage.areas[damage.area_count++]=a;
    }
    if (!sc2_damage_filter(e.filters,&damage)) { fprintf(stderr,"SC2 unsupported damage filter: %s (%s)\n",e.filters,id); return false; }
    out->effects[out->effect_count++]=damage;
    return true;
}
