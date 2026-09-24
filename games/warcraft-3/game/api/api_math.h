DWORD Rect(LPJASS j) {
    API_ALLOC(BOX2, rect);
    rect->min.x = jass_checknumber(j, 1);
    rect->min.y = jass_checknumber(j, 2);
    rect->max.x = jass_checknumber(j, 3);
    rect->max.y = jass_checknumber(j, 4);
    return 1;
}
DWORD RectFromLoc(LPJASS j) {
    LPCVECTOR2 min = jass_checkhandle(j, 1, "location");
    LPCVECTOR2 max = jass_checkhandle(j, 2, "location");
    API_ALLOC(BOX2, rect);
    if (min) rect->min = *min;
    if (max) rect->max = *max;
    return 1;
}
DWORD RemoveRect(LPJASS j) {
    //HANDLE whichRect = jass_checkhandle(j, 1, "rect");
    return 0;
}
DWORD SetRect(LPJASS j) {
    LPBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    FLOAT minx = jass_checknumber(j, 2);
    FLOAT miny = jass_checknumber(j, 3);
    FLOAT maxx = jass_checknumber(j, 4);
    FLOAT maxy = jass_checknumber(j, 5);
    if (whichRect) {
        whichRect->min.x = minx;
        whichRect->min.y = miny;
        whichRect->max.x = maxx;
        whichRect->max.y = maxy;
    }
    return 0;
}
DWORD SetRectFromLoc(LPJASS j) {
    LPBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    LPCVECTOR2 min = jass_checkhandle(j, 2, "location");
    LPCVECTOR2 max = jass_checkhandle(j, 3, "location");
    if (whichRect && min) whichRect->min = *min;
    if (whichRect && max) whichRect->max = *max;
    return 0;
}
DWORD MoveRectTo(LPJASS j) {
    LPBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    VECTOR2 newCenterLoc = {
        jass_checknumber(j, 2),
        jass_checknumber(j, 3),
    };
    if (whichRect) Box2_moveTo(whichRect, &newCenterLoc);
    return 0;
}
DWORD MoveRectToLoc(LPJASS j) {
    LPBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    LPCVECTOR2 newCenterLoc = jass_checkhandle(j, 2, "location");
    if (whichRect && newCenterLoc) Box2_moveTo(whichRect, newCenterLoc);
    return 0;
}
DWORD GetRectCenterX(LPJASS j) {
    LPCBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? Box2_center(whichRect).x : 0);
}
DWORD GetRectCenterY(LPJASS j) {
    LPCBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? Box2_center(whichRect).y : 0);
}
DWORD GetRectMinX(LPJASS j) {
    LPCBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->min.x : 0);
}
DWORD GetRectMinY(LPJASS j) {
    LPCBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->min.y : 0);
}
DWORD GetRectMaxX(LPJASS j) {
    LPCBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->max.x : 0);
}
DWORD GetRectMaxY(LPJASS j) {
    LPCBOX2 whichRect = jass_checkhandle(j, 1, "rect");
    return jass_pushnumber(j, whichRect ? whichRect->max.y : 0);
}
DWORD CreateRegion(LPJASS j) {
    DWORD i;
    LPREGION region;
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
DWORD RemoveRegion(LPJASS j) {
    HANDLE handle = jass_checkhandle(j, 1, "region");
    LPREGION region = G_RegionFromHandle(handle);
    if (!region) return 0;
    region->inuse = false;
    region->num_rects = 0;
    memset(region->rects, 0, sizeof(region->rects));
    if (region->generation == REGION_HANDLE_GENERATION_MAX) region->exhausted = true;
    else region->generation++;
    FOR_LOOP(i, MAX_EVENTS) {
        LPEVENT event = &level.events.handlers[i];
        if (!event->inuse || event->region != handle) continue;
        event->region = NULL;
        for (DWORD n = level.events.read; n < level.events.write; n++)
            if (level.events.queue[n % MAX_EVENT_QUEUE].responseTo == event)
                level.events.queue[n % MAX_EVENT_QUEUE].responseTo = NULL;
        event->inuse = false;
        if (event->handle_generation == EVENT_HANDLE_GENERATION_MAX) event->generation_exhausted = true;
        else event->handle_generation++;
    }
    return 0;
}
DWORD RegionAddRect(LPJASS j) {
    LPREGION whichRegion = G_RegionFromHandle(jass_checkhandle(j, 1, "region"));
    LPCBOX2 r = jass_checkhandle(j, 2, "rect");
    if (whichRegion && whichRegion->inuse && r && whichRegion->num_rects < MAX_REGION_SIZE)
        whichRegion->rects[whichRegion->num_rects++] = *r;
    return 0;
}
DWORD RegionClearRect(LPJASS j) {
    LPREGION whichRegion = G_RegionFromHandle(jass_checkhandle(j, 1, "region"));
    LPCBOX2 r = jass_checkhandle(j, 2, "rect");
    if (!whichRegion || !whichRegion->inuse || !r) return 0;
    FOR_LOOP(i, whichRegion->num_rects) {
        if (memcmp(whichRegion->rects + i, r, sizeof(*r))) continue;
        memmove(whichRegion->rects + i, whichRegion->rects + i + 1,
                (--whichRegion->num_rects - i) * sizeof(*r));
        break;
    }
    return 0;
}
DWORD RegionAddCell(LPJASS j) {
    //HANDLE whichRegion = jass_checkhandle(j, 1, "region");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    return 0;
}
DWORD RegionAddCellAtLoc(LPJASS j) {
    //HANDLE whichRegion = jass_checkhandle(j, 1, "region");
    //HANDLE whichLocation = jass_checkhandle(j, 2, "location");
    return 0;
}
DWORD RegionClearCell(LPJASS j) {
    //HANDLE whichRegion = jass_checkhandle(j, 1, "region");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    return 0;
}
DWORD RegionClearCellAtLoc(LPJASS j) {
    //HANDLE whichRegion = jass_checkhandle(j, 1, "region");
    //HANDLE whichLocation = jass_checkhandle(j, 2, "location");
    return 0;
}
DWORD Location(LPJASS j) {
    API_ALLOC(VECTOR2, location);
    location->x = jass_checknumber(j, 1);
    location->y = jass_checknumber(j, 2);
    return 1;
}
DWORD RemoveLocation(LPJASS j) {
    //HANDLE whichLocation = jass_checkhandle(j, 1, "location");
    return 0;
}
DWORD MoveLocation(LPJASS j) {
    LPVECTOR2 whichLocation = jass_checkhandle(j, 1, "location");
    if (whichLocation) { whichLocation->x = jass_checknumber(j, 2); whichLocation->y = jass_checknumber(j, 3); }
    return 0;
}
DWORD GetLocationX(LPJASS j) {
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation ? whichLocation->x : 0); // null location reads as 0, like GetRectCenterX
}
DWORD GetLocationY(LPJASS j) {
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation ? whichLocation->y : 0); // null location reads as 0, like GetRectCenterX
}
DWORD GetLocationZ(LPJASS j) {
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation ? CM_GetHeightAtPoint(whichLocation->x, whichLocation->y) : 0);
}
