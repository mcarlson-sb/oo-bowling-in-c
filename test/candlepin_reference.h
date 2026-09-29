#ifndef CANDLEPIN_REFERENCE_H
#define CANDLEPIN_REFERENCE_H

/* An independent reference for candlepin, written the way the classic ten-pin kata is, sharing
 * no code with the library: up to three balls a frame, a strike scores the next two balls, a
 * spare the next one, and a ten-box (10 in three balls) and an open frame score their pins. */

#include <cstddef>
#include <vector>

namespace candlepin_reference {

/* Complete frames only, as Scorer_Score counts them. */
inline int Score(const std::vector<int> &balls)
{
    int score = 0;
    size_t i = 0;
    for (int frame = 0; frame < 10; ++frame) {
        const size_t left = balls.size() - i;
        if ((left >= 1) && (balls[i] == 10)) {
            if (left < 3) {
                break;
            }
            score += 10 + balls[i + 1] + balls[i + 2];
            i += 1;
        } else if ((left >= 2) && (balls[i] + balls[i + 1] == 10)) {
            if (left < 3) {
                break;
            }
            score += 10 + balls[i + 2];
            i += 2;
        } else {
            if (left < 3) {
                break;
            }
            score += balls[i] + balls[i + 1] + balls[i + 2];
            i += 3;
        }
    }
    return score;
}

/* The lane: which frame and ball is next, how many pins stand, and whether the game is over.
 * Knocked-down pins stay down until the frame ends. The tenth frame always takes exactly three
 * balls, with a fresh rack whenever one is cleared. */
struct Lane {
    int frame = 1;
    int ball = 0;
    int standing = 10;
    bool over = false;

    void Roll(int pins)
    {
        standing -= pins;
        ++ball;
        if (frame < 10) {
            if ((standing == 0) || (ball == 3)) {
                ++frame;
                ball = 0;
                standing = 10;
            }
            return;
        }
        over = (ball == 3);
        if (standing == 0) {
            standing = 10;
        }
    }
};

} // namespace candlepin_reference

#endif /* CANDLEPIN_REFERENCE_H */
