#ifndef RULES_REFERENCE_H
#define RULES_REFERENCE_H

/* An independent reference for any rules, written the way the classic ten-pin kata is and
 * sharing no code with the library, not even its rules type: frame by frame, a frame that
 * clears the rack with its k-th ball scores its pins and the next bonus[k-1] balls, and an open
 * frame scores its pins. The last frame's bonus balls are its fill balls. No count rule: every
 * ball counts the pins that fell. */

#include <cstddef>
#include <vector>

namespace rules_reference {

struct Rules {
    int frames;
    int balls_per_frame;
    int pins;
    int bonus[3]; /* by the ball that cleared the rack, first to third */
};

/* Complete frames only, as Scorer_Score counts them. */
inline int Score(const Rules &rules, const std::vector<int> &balls)
{
    const int count = static_cast<int>(balls.size());
    const auto at = [&balls](int index) { return balls[static_cast<size_t>(index)]; };
    int score = 0;
    int i = 0;
    for (int frame = 0; frame < rules.frames; ++frame) {
        int down = 0;
        int thrown = 0;
        while ((thrown < rules.balls_per_frame) && (down < rules.pins) && (i + thrown < count)) {
            down += at(i + thrown);
            ++thrown;
        }
        const bool cleared = (down == rules.pins);
        if (!cleared && (thrown < rules.balls_per_frame)) {
            break; /* the frame's own balls aren't all in */
        }
        const int bonus = cleared ? rules.bonus[thrown - 1] : 0;
        if (i + thrown + bonus > count) {
            break; /* its bonus balls aren't all in */
        }
        score += down;
        for (int b = 0; b < bonus; ++b) {
            score += at(i + thrown + b);
        }
        i += thrown;
    }
    return score;
}

/* The lane: which frame and ball is next, how many pins stand, and whether the game is over.
 * The last frame's fill balls get a fresh rack whenever one is cleared. */
struct Lane {
    Rules rules;
    int frame = 1;
    int ball = 0;
    int standing;
    int fill_left = 0;
    bool over = false;

    explicit Lane(const Rules &r) : rules(r), standing(r.pins) {}

    void Roll(int pins)
    {
        standing -= pins;
        if (fill_left > 0) {
            --fill_left;
            over = (fill_left == 0);
            if (standing == 0) {
                standing = rules.pins;
            }
            return;
        }
        ++ball;
        if ((standing > 0) && (ball < rules.balls_per_frame)) {
            return; /* the frame goes on */
        }
        const int bonus = (standing == 0) ? rules.bonus[ball - 1] : 0;
        standing = rules.pins;
        ball = 0;
        if (frame < rules.frames) {
            ++frame;
        } else if (bonus > 0) {
            fill_left = bonus;
        } else {
            over = true;
        }
    }
};

} // namespace rules_reference

#endif /* RULES_REFERENCE_H */
