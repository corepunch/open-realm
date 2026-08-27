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
    API_ALLOC(REGION, region);
    (void)region;
    return 1;
}
DWORD RemoveRegion(LPJASS j) {
    //HANDLE whichRegion = jass_checkhandle(j, 1, "region");
    return 0;
}
DWORD RegionAddRect(LPJASS j) {
    LPREGION whichRegion = jass_checkhandle(j, 1, "region");
    LPCBOX2 r = jass_checkhandle(j, 2, "rect");
    if (whichRegion && r && whichRegion->num_rects < MAX_REGION_SIZE)
        whichRegion->rects[whichRegion->num_rects++] = *r;
    return 0;
}
DWORD RegionClearRect(LPJASS j) {
    LPREGION whichRegion = jass_checkhandle(j, 1, "region");
    LPCBOX2 r = jass_checkhandle(j, 2, "rect");
    if (!whichRegion || !r) return 0;
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
    whichLocation->x = jass_checknumber(j, 2);
    whichLocation->y = jass_checknumber(j, 3);
    return 0;
}
DWORD GetLocationX(LPJASS j) {
    LPVECTOR2 whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation->x);
}
DWORD GetLocationY(LPJASS j) {
    LPVECTOR2 whichLocation = jass_checkhandle(j, 1, "location");
    return jass_pushnumber(j, whichLocation->y);
}
