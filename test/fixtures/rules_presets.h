#ifndef RULES_PRESETS_H
#define RULES_PRESETS_H

/* The variants the tests send, as data: the scorer compiles none of them in. */

extern "C" {
#include "scorer.h"
}

namespace rules {

constexpr ScorerRules kTenPin = {10U, 2U, 10U, {2U, 1U, 0U}, 0U};
constexpr ScorerRules kTenPinNoTap = {10U, 2U, 10U, {2U, 1U, 0U}, 1U};
/* A third ball clears the rack too, a ten-box, which earns no bonus. */
constexpr ScorerRules kCandlepin = {10U, 3U, 10U, {2U, 1U, 0U}, 0U};

} // namespace rules

#endif /* RULES_PRESETS_H */
