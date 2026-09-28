/* SCRATCH: a deliberate violation, to prove the function-pointer check fires. Reverted in the
 * next commit. */
#include "bowling_types.h"

typedef Score (*Scratch_ScoreFn)(Pins pins);

static Score Scratch_Double(Pins pins)
{
    return (Score)(pins * 2U);
}

struct Scratch_Holder {
    Scratch_ScoreFn score;
};

const struct Scratch_Holder scratch_holder = { Scratch_Double };
