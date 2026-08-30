#include "s_skills.h"

void cancel_command(LPEDICT ent) {
    CMD_CancelCommand(ent);
}

ability_t a_cancel = {
    .cmd = cancel_command,
};
