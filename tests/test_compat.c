#include "test.h"
#include "common/shared.h"

TEST(compat, strlcpy_reports_source_length_and_truncates) {
    char destination[4];

    T_EQ(bz_strlcpy(destination, "source", sizeof(destination)), 6);
    T_STREQ(destination, "sou");
}

TEST(compat, strlcpy_zero_size_does_not_write) {
    char destination[] = "keep";

    T_EQ(bz_strlcpy(destination, "source", 0), 6);
    T_STREQ(destination, "keep");
}

TEST(compat, strlcat_reports_combined_length_and_truncates) {
    char destination[6] = "ab";

    T_EQ(bz_strlcat(destination, "cdef", sizeof(destination)), 6);
    T_STREQ(destination, "abcde");
}

TEST(compat, strlcat_bounded_unterminated_destination_does_not_write) {
    char destination[] = {'a', 'b', 'c'};

    T_EQ(bz_strlcat(destination, "de", sizeof(destination)), 5);
    T_ASSERT(!memcmp(destination, "abc", sizeof(destination)));
}