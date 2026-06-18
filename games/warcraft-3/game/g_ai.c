#include "g_local.h"

#define NAVI_THRESHOLD 128.0f

/* Wrap an angle delta into [-PI, PI]. */
static FLOAT angle_wrap(FLOAT a) {
    while (a > (FLOAT)M_PI)  a -= 2.0f * (FLOAT)M_PI;
    while (a < -(FLOAT)M_PI) a += 2.0f * (FLOAT)M_PI;
    return a;
}

void unit_changeangle(LPEDICT self) {
    VECTOR2 to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    FLOAT dist = Vector2_len(&to_goal);
    VECTOR2 dir;

    if (dist <= NAVI_THRESHOLD) {
        /* Close enough: use direct vector, skip heatmap entirely. */
        dir = to_goal;
    } else {
        DWORD heatmap = M_RefreshHeatmap(self->goalentity);
        dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
        if (Vector2_len(&dir) <= 0.001f) {
            dir = to_goal;
        }
    }

    /* Turn gradually toward the target facing, at the unit's authentic turn
     * rate ('umvr', radians per sim tick; WC3 default 0.5), instead of snapping
     * instantly. */
    FLOAT const target = (FLOAT)atan2(dir.y, dir.x);
    FLOAT turn = UNIT_TURN_RATE(self->class_id);
    if (turn <= 0.0f) turn = 0.5f;
    FLOAT const delta = angle_wrap(target - self->s.angle);
    if (delta > turn) {
        self->s.angle = angle_wrap(self->s.angle + turn);
    } else if (delta < -turn) {
        self->s.angle = angle_wrap(self->s.angle - turn);
    } else {
        self->s.angle = target;
    }
}

FLOAT unit_movedistance(LPEDICT self) {
    FLOAT speed = self->unitinfo.MoveSpeed > 0
        ? self->unitinfo.MoveSpeed
        : UNIT_SPEED(self->class_id);
    return 10 * speed / FRAMETIME;
}

void unit_moveindirection(LPEDICT self) {
    G_PushEntity(self,
                 unit_movedistance(self),
                 &MAKE(VECTOR2, cos(self->s.angle), sin(self->s.angle)));
}

void unit_setanimation(LPEDICT self, LPCSTR anim) {
    self->animation = G_GetAnimation(self->s.model, anim);
}

void unit_setmove(LPEDICT self, umove_t *move) {
    self->currentmove = move;
    self->animation = G_GetAnimation(self->s.model, move->animation);
    if (self->animation) {
        // skip
    } else if (strstr(move->animation, "run")) {
        self->animation = G_GetAnimation(self->s.model, "walk");
    } else if (strstr(move->animation, "stand ")) {
        self->animation = G_GetAnimation(self->s.model, "stand");
    } else if (strstr(move->animation, "attack ")) {
        self->animation = G_GetAnimation(self->s.model, "attack");
    }
}

void unit_runwait(LPEDICT self, void (*callback)(LPEDICT )) {
    if (self->wait <= 0)
        return;
    if (self->wait > FRAMETIME / 1000.f) {
        self->wait -= FRAMETIME / 1000.f;
    } else {
        self->wait = 0;
        callback(self);
    }
}

void ai_idle(LPEDICT self) {
}

void order_attack(LPEDICT self, LPEDICT target);

#define MAX_SIGHT_ENTITIES 256

static LPEDICT ai_current_entity = NULL;
static LPEDICT sight_entities[MAX_SIGHT_ENTITIES];

static BOOL filter_sight(LPCEDICT ent) {
    if (!(ent->svflags & SVF_MONSTER) || ent->s.player == ai_current_entity->s.player)
        return false;
    if (level.alliances[ent->s.player][ai_current_entity->s.player] != 0)
        return false;
    if (level.mapinfo->players[ent->s.player].playerType != kPlayerTypeHuman)
        return false;
    if (ent->svflags & SVF_DEADMONSTER)
        return false;
    if (UNIT_IS_BUILDING(ent->class_id))
        return false;
    return true;
}

void ai_stand(LPEDICT self) {
    if (!(self->svflags & SVF_MONSTER))
        return;
    if (level.mapinfo->players[self->s.player].playerType != kPlayerTypeComputer)
        return;
    ai_current_entity = self;
    FLOAT const sight = self->balance.sight_radius.day / 2;
    BOX2 const sightbox = {
        { self->s.origin2.x - sight, self->s.origin2.y - sight },
        { self->s.origin2.x + sight, self->s.origin2.y + sight },
    };
    DWORD numents = gi.BoxEdicts(&sightbox, sight_entities, MAX_SIGHT_ENTITIES, filter_sight);
    FOR_LOOP(i, numents) {
        LPEDICT ent = sight_entities[i];
        if (Vector2_distance(&ent->s.origin2, &self->s.origin2) < sight) {
            order_attack(self, ent);
        }
    }
}

void ai_birth(LPEDICT self) {
}

void ai_pain(LPEDICT self) {
}
