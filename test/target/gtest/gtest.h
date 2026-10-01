#ifndef OO_GTEST_SHIM_H
#define OO_GTEST_SHIM_H

/* GoogleTest's TEST and assertions, as much of them as the pure core's tests use, for the QEMU
 * image: the host's test sources build for the target unchanged, with no GoogleTest library, no
 * exceptions and no RTTI. A failure prints as GoogleTest's would, through semihosting, and the
 * runner (gtest_shim.cpp) runs every TEST and exits QEMU with the result. Death tests aren't
 * here: they need a process to fork, and stay on the host. */

#include <cstddef>
#include <type_traits>

namespace testing {
namespace shim {

/* What an assertion says, and what a test streams after it with <<, in a fixed buffer. */
class Message {
public:
    Message() = default;

    Message &operator<<(const char *text);
    Message &operator<<(char *text) { return *this << static_cast<const char *>(text); }
    Message &operator<<(bool value) { return *this << (value ? "true" : "false"); }
    Message &operator<<(const Message &other) { return *this << other.Text(); }

    template <typename T>
    typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, Message &>::type
    operator<<(T value)
    {
        return std::is_signed<T>::value ? Signed(static_cast<long long>(value))
                                        : Unsigned(static_cast<unsigned long long>(value));
    }

    template <typename T> Message &operator<<(T *pointer)
    {
        return Unsigned(reinterpret_cast<unsigned long long>(pointer));
    }

    /* Anything else, such as a test's own struct: GoogleTest prints its bytes; this says so. */
    template <typename T>
    typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_enum<T>::value &&
                                !std::is_pointer<T>::value,
                            Message &>::type
    operator<<(const T &)
    {
        return *this << "(an object)";
    }

    const char *Text() const { return text_; }

private:
    Message &Signed(long long value);
    Message &Unsigned(unsigned long long value);

    static constexpr size_t kCapacity = 384U;
    char text_[kCapacity] = {};
    size_t length_ = 0U;
};

/* An assertion's outcome, and what it says when it failed. */
struct Result {
    bool passed;
    Message message;
    explicit operator bool() const { return passed; }
};

/* Reports a failed assertion: its file and line, what it says, and what the test streamed. */
class AssertHelper {
public:
    AssertHelper(const char *file, int line, const Message &said)
        : file_(file), line_(line), said_(said)
    {
    }
    void operator=(const Message &streamed) const;

private:
    const char *file_;
    int line_;
    const Message &said_;
};

template <typename A, typename B>
Result Compare(bool passed, const char *operation, const char *a_text, const char *b_text,
               const A &a, const B &b)
{
    Result result{passed, Message()};
    if (!passed) {
        result.message << "Expected: (" << a_text << ") " << operation << " (" << b_text
                       << "), actual: " << a << " vs " << b;
    }
    return result;
}

template <typename A, typename B>
Result CompareEq(const char *a_text, const char *b_text, const A &a, const B &b)
{
    Result result{a == b, Message()};
    if (!result.passed) {
        result.message << "Expected equality of these values:\n  " << a_text << "\n    Which is: "
                       << a << "\n  " << b_text << "\n    Which is: " << b;
    }
    return result;
}

inline Result CheckBool(bool actual, bool expected, const char *text)
{
    Result result{actual == expected, Message()};
    if (!result.passed) {
        result.message << "Value of: " << text << "\n  Actual: " << actual
                       << "\nExpected: " << expected;
    }
    return result;
}

/* One TEST, registered by a static object's constructor, which the startup runs before main. */
using TestBody = void (*)();
struct Registration {
    Registration(const char *suite, const char *name, TestBody body);
    const char *suite;
    const char *name;
    TestBody body;
    Registration *next;
};

} // namespace shim
} // namespace testing

#define GTEST_SHIM_ASSERT_(result, on_failure)                                                    \
    switch (0)                                                                                    \
    case 0:                                                                                       \
    default:                                                                                      \
        if (const ::testing::shim::Result gtest_shim_result = (result))                           \
            ;                                                                                     \
        else                                                                                      \
            on_failure ::testing::shim::AssertHelper(__FILE__, __LINE__, gtest_shim_result.message) = \
                ::testing::shim::Message()

#define GTEST_SHIM_COMPARE_(a, b, test, operation, on_failure)                                    \
    GTEST_SHIM_ASSERT_(::testing::shim::Compare(((a)test(b)), operation, #a, #b, a, b), on_failure)

#define EXPECT_EQ(a, b) GTEST_SHIM_ASSERT_(::testing::shim::CompareEq(#a, #b, a, b), )
#define ASSERT_EQ(a, b) GTEST_SHIM_ASSERT_(::testing::shim::CompareEq(#a, #b, a, b), return)
#define EXPECT_NE(a, b) GTEST_SHIM_COMPARE_(a, b, !=, "!=", )
#define ASSERT_NE(a, b) GTEST_SHIM_COMPARE_(a, b, !=, "!=", return)
#define EXPECT_LE(a, b) GTEST_SHIM_COMPARE_(a, b, <=, "<=", )
#define ASSERT_LE(a, b) GTEST_SHIM_COMPARE_(a, b, <=, "<=", return)
#define EXPECT_LT(a, b) GTEST_SHIM_COMPARE_(a, b, <, "<", )
#define ASSERT_LT(a, b) GTEST_SHIM_COMPARE_(a, b, <, "<", return)
#define EXPECT_GE(a, b) GTEST_SHIM_COMPARE_(a, b, >=, ">=", )
#define ASSERT_GE(a, b) GTEST_SHIM_COMPARE_(a, b, >=, ">=", return)
#define EXPECT_GT(a, b) GTEST_SHIM_COMPARE_(a, b, >, ">", )
#define ASSERT_GT(a, b) GTEST_SHIM_COMPARE_(a, b, >, ">", return)
#define EXPECT_TRUE(c) GTEST_SHIM_ASSERT_(::testing::shim::CheckBool(static_cast<bool>(c), true, #c), )
#define ASSERT_TRUE(c) GTEST_SHIM_ASSERT_(::testing::shim::CheckBool(static_cast<bool>(c), true, #c), return)
#define EXPECT_FALSE(c) GTEST_SHIM_ASSERT_(::testing::shim::CheckBool(static_cast<bool>(c), false, #c), )
#define ASSERT_FALSE(c) GTEST_SHIM_ASSERT_(::testing::shim::CheckBool(static_cast<bool>(c), false, #c), return)

#define TEST(suite, name)                                                                         \
    static void suite##_##name##_Body();                                                          \
    static const ::testing::shim::Registration suite##_##name##_registration(#suite, #name,       \
                                                                             &suite##_##name##_Body); \
    static void suite##_##name##_Body()

#endif /* OO_GTEST_SHIM_H */
