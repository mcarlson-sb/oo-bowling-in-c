/* The runner for the GoogleTest shim (gtest/gtest.h) on the QEMU image: runs every registered
 * TEST, prints as GoogleTest does, through semihosting, and exits QEMU 0 only if every test
 * passed. */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "semihosting.h"

namespace testing {
namespace shim {
namespace {

Registration *s_first = nullptr;
Registration *s_last = nullptr;
bool s_current_failed = false;

void Write(const char *text)
{
    Semihosting_Write(text);
}

} // namespace

Message &Message::operator<<(const char *text)
{
    const size_t room = kCapacity - 1U - length_;
    const size_t length = std::min(std::strlen(text), room);
    std::memcpy(&text_[length_], text, length);
    length_ += length;
    text_[length_] = '\0';
    return *this;
}

Message &Message::Unsigned(unsigned long long value)
{
    char digits[24];
    size_t at = sizeof digits - 1U;
    digits[at] = '\0';
    do {
        digits[--at] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    return *this << &digits[at];
}

Message &Message::Signed(long long value)
{
    if (value < 0) {
        *this << "-";
        return Unsigned(0ULL - static_cast<unsigned long long>(value));
    }
    return Unsigned(static_cast<unsigned long long>(value));
}

void AssertHelper::operator=(const Message &streamed) const
{
    s_current_failed = true;
    Message where;
    where << file_ << ":" << line_ << ": Failure\n";
    Write(where.Text());
    Write(said_.Text());
    Write("\n");
    if (streamed.Text()[0] != '\0') {
        Write(streamed.Text());
        Write("\n");
    }
}

Registration::Registration(const char *suite_name, const char *test_name, TestBody test_body)
    : suite(suite_name), name(test_name), body(test_body), next(nullptr)
{
    if (s_last == nullptr) {
        s_first = this;
    } else {
        s_last->next = this;
    }
    s_last = this;
}

namespace {

void WriteTestLine(const char *tag, const Registration &test)
{
    Message line;
    line << tag << test.suite << "." << test.name << "\n";
    Write(line.Text());
}

} // namespace

/* Every registered test, in the order they were linked; the number that failed. */
unsigned RunAllTests()
{
    unsigned ran = 0U;
    unsigned failed = 0U;
    for (const Registration *test = s_first; test != nullptr; test = test->next) {
        WriteTestLine("[ RUN      ] ", *test);
        s_current_failed = false;
        test->body();
        WriteTestLine(s_current_failed ? "[  FAILED  ] " : "[       OK ] ", *test);
        ran++;
        failed += s_current_failed ? 1U : 0U;
    }
    Message summary;
    summary << "[==========] " << ran << " tests ran on the target.\n[  PASSED  ] "
            << (ran - failed) << " tests.\n";
    if (failed != 0U) {
        summary << "[  FAILED  ] " << failed << " tests.\n";
    }
    Write(summary.Text());
    return failed;
}

} // namespace shim
} // namespace testing

int main()
{
    Semihosting_Exit(::testing::shim::RunAllTests() == 0U);
}
