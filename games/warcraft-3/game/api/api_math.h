uint32_t Rect(jass_t * j) {
    API_ALLOC(box2_t, rect);
    rect->min.x = jass_checknumber(j, 1);
    rect->min.y = jass_checknumber(j, 2);
    rect->max.x = jass_checknumber(j, 3);
    rect->max.y = jass_checknumber(j, 4);
    return 1;
}
uint32_t RectFromLoc(jass_t * j) {
    vector2_t const * min = jass_checkhandle(j, 1, "location");
    vector2_t const * max = jass_checkhandle(j, 2, "location");
    API_ALLOC(box2_t, rect);
    if (min) rect->min = *min;
    if (max) rect->max = *max;
    return 1;
}
uint32_t RemoveRect(jass_t * j) {
    //handle_t whichRect = jass_checkhandle(j, 1, "rect");
    return 0;
}
uint32_t SetRect(jass_t * j) {
    box2_t * whichRect = jass_checkhandle(j, 1, "rect");
    float minx = jass_checknumber(j, 2);
    float miny = jass_checknumber(j, 3);
    float maxx = jass_checknumber(j, 4);
    float maxy = jass_checknumber(j, 5);
    if (whichRect) {
        whichRect->min.x = minx;
        whichRect->min.y = miny;
        whichRect->max.x = maxx;
        whichRect->max.y = maxy;
    }
    return 0;
}
uint32_t SetRectFromLoc(jass_t * j) {
    box2_t * whichRect = jass_checkhandle(j, 1, "rect");
    vector2_t const * min = jass_checkhandle(j, 2, "location");
    vector2_t const * max = jass_checkhandle(j, 3, "location");
    if (whichRect && min) whichRect->min = *min;
    if (whichRect && max) whichRect->max = *max;
    return 0;
}
uint32_t MoveRectTo(jass_t * j) {
    box2_t * whichRect = jass_checkhandle(j, 1, "rect");
    vector2_t newCenterLoc = {
        jass_checknumber(j, 2),
        jass_checknumber(j, 3),
    };
    if (whichRect) Box2_moveTo(whichRect, &newCenterLoc);
    return 0;
}
uint32_t MoveRectToLoc(jass_t * j) {
    box2_t * whichRect = jass_checkhandle(j, 1, "rect");
    vector2_t const * newCenterLoc = jass_checkhandle(j, 2, "location");
    if (whichRect && newCenterLoc) Box2_moveTo(whichRect, newCenterLoc);
    return 0;
}
uint32_t GetRectCenterX(jass_t * j) {
    box2_t const * whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? Box2_center(whichRect).x : 0);
}
uint32_t GetRectCenterY(jass_t * j) {
    box2_t const * whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? Box2_center(whichRect).y : 0);
}
uint32_t GetRectMinX(jass_t * j) {
    box2_t const * whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->min.x : 0);
}
uint32_t GetRectMinY(jass_t * j) {
    box2_t const * whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->min.y : 0);
}
uint32_t GetRectMaxX(jass_t * j) {
    box2_t const * whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->max.x : 0);
}
uint32_t GetRectMaxY(jass_t * j) {
    box2_t const * whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->max.y : 0);
}
uint32_t CreateRegion(jass_t * j) {
    uint32_t i;
    region_t * region;
    for (i = 0; i < MAX_REGIONS && (level.regions[i].inuse || level.regions[i].exhausted); i++) { }
    if (i == MAX_REGIONS) {
        fprintf(stderr, "WC3 CreateRegion: no reusable region slots (%u)\n", MAX_REGIONS);
        return jass_pushnullhandle(j, "region");
    }
    region = &level.regions[i];
    memset(region->rects, 0, sizeof(region->rects)); region->num_rects = 0;
    region->inuse = true;
    if (i >= level.num_regions) level.num_regions = i + 1;
    return jass_pushlighthandle(j, G_RegionHandle(i), "region");
}
uint32_t RemoveRegion(jass_t * j) {
    handle_t handle = jass_checkhandle(j, 1, "region");
    region_t * region = G_RegionFromHandle(handle);
    if (!region) return 0;
    region->inuse = false;
    region->num_rects = 0;
    memset(region->rects, 0, sizeof(region->rects));
    if (region->generation == REGION_HANDLE_GENERATION_MAX) region->exhausted = true;
    else region->generation++;
    FOR_LOOP(i, MAX_EVENTS) {
        event_t * event = &level.events.handlers[i];
        if (!event->inuse || event->region != handle) continue;
        event->region = NULL;
        for (uint32_t n = level.events.read; n < level.events.write; n++)
            if (level.events.queue[n % MAX_EVENT_QUEUE].responseTo == event)
                level.events.queue[n % MAX_EVENT_QUEUE].responseTo = NULL;
        event->inuse = false;
        if (event->handle_generation == EVENT_HANDLE_GENERATION_MAX) event->generation_exhausted = true;
        else event->handle_generation++;
    }
    return 0;
}
uint32_t RegionAddRect(jass_t * j) {
    region_t * whichRegion = G_RegionFromHandle(jass_checkhandle(j, 1, "region"));
    box2_t const * r = jass_checkhandle(j, 2, "rect");
    if (!whichRegion || !whichRegion->inuse || !r) return 0;
    if (whichRegion->num_rects >= MAX_REGION_SIZE) {
        fprintf(stderr, "WC3: RegionAddRect rejected rectangle: MAX_REGION_SIZE (%u) reached\n",
                (unsigned)MAX_REGION_SIZE);
        return 0;
    }
    whichRegion->rects[whichRegion->num_rects++] = *r;
    return 0;
}
uint32_t RegionClearRect(jass_t * j) {
    region_t * whichRegion = G_RegionFromHandle(jass_checkhandle(j, 1, "region"));
    box2_t const * r = jass_checkhandle(j, 2, "rect");
    if (!whichRegion || !whichRegion->inuse || !r) return 0;
    FOR_LOOP(i, whichRegion->num_rects) {
        if (memcmp(whichRegion->rects + i, r, sizeof(*r))) continue;
        memmove(whichRegion->rects + i, whichRegion->rects + i + 1,
                (--whichRegion->num_rects - i) * sizeof(*r));
        break;
    }
    return 0;
}
uint32_t RegionAddCell(jass_t * j) {
    //handle_t whichRegion = jass_checkhandle(j, 1, "region");
    //float x = jass_checknumber(j, 2);
    //float y = jass_checknumber(j, 3);
    return 0;
}
uint32_t RegionAddCellAtLoc(jass_t * j) {
    //handle_t whichRegion = jass_checkhandle(j, 1, "region");
    //handle_t whichLocation = jass_checkhandle(j, 2, "location");
    return 0;
}
uint32_t RegionClearCell(jass_t * j) {
    //handle_t whichRegion = jass_checkhandle(j, 1, "region");
    //float x = jass_checknumber(j, 2);
    //float y = jass_checknumber(j, 3);
    return 0;
}
uint32_t RegionClearCellAtLoc(jass_t * j) {
    //handle_t whichRegion = jass_checkhandle(j, 1, "region");
    //handle_t whichLocation = jass_checkhandle(j, 2, "location");
    return 0;
}
uint32_t Location(jass_t * j) {
    API_ALLOC(vector2_t, location);
    location->x = jass_checknumber(j, 1);
    location->y = jass_checknumber(j, 2);
    return 1;
}
uint32_t RemoveLocation(jass_t * j) {
    //handle_t whichLocation = jass_checkhandle(j, 1, "location");
    return 0;
}
uint32_t MoveLocation(jass_t * j) {
    vector2_t * whichLocation = jass_checkhandle(j, 1, "location");
    if (whichLocation) { whichLocation->x = jass_checknumber(j, 2); whichLocation->y = jass_checknumber(j, 3); }
    return 0;
}
uint32_t GetLocationX(jass_t * j) {
    vector2_t const * whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation ? whichLocation->x : 0); // null location reads as 0, like GetRectCenterX
}
uint32_t GetLocationY(jass_t * j) {
    vector2_t const * whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation ? whichLocation->y : 0); // null location reads as 0, like GetRectCenterX
}
uint32_t GetLocationZ(jass_t * j) {
    vector2_t const * whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation ? CM_GetHeightAtPoint(whichLocation->x, whichLocation->y) : 0);
}
