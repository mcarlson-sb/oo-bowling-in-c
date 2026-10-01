#ifndef TEN_PIN_REFERENCE_H
#define TEN_PIN_REFERENCE_H

/* An independent reference for ten-pin: the classic procedural kata, written here and sharing
 * no code with the library. */

#include <cstddef>
#include <vector>

namespace ten_pin_reference {

/* Complete frames only, as Game_Score counts them. */
inline int Score(const std::vector<int> &rolls)
{
    int score = 0;
    size_t i = 0;
    for (int frame = 0; frame < 10; ++frame) {
        const size_t left = rolls.size() - i;
        if ((left >= 1) && (rolls[i] == 10)) {
            if (left < 3) {
                break;
            }
            score += 10 + rolls[i + 1] + rolls[i + 2];
            i += 1;
        } else if ((left >= 2) && (rolls[i] + rolls[i + 1] == 10)) {
            if (left < 3) {
                break;
            }
            score += 10 + rolls[i + 2];
            i += 2;
        } else {
            if (left < 2) {
                break;
            }
            score += rolls[i] + rolls[i + 1];
            i += 2;
        }
    }
    return score;
}

/* The lane: which frame and ball is next, how many pins stand, and whether the game is over.
 * The tenth frame gets a fresh rack after a strike or a spare, and ends after two balls that
 * leave pins standing, or after three. */
struct Lane {
    int frame = 1;
    int ball = 0;
    int standing = 10;
    int tenth_first_two = 0;
    bool over = false;

    void Roll(int counted)
    {
        standing -= counted;
        ++ball;
        if (frame < 10) {
            if ((standing == 0) || (ball == 2)) {
                ++frame;
                ball = 0;
                standing = 10;
            }
            return;
        }
        if (ball <= 2) {
            tenth_first_two += counted;
        }
        over = (ball == 3) || ((ball == 2) && (tenth_first_two < 10));
        if (standing == 0) {
            standing = 10;
        }
    }
};

} // namespace ten_pin_reference

#endif /* TEN_PIN_REFERENCE_H */
