#pragma once
/* Extra Unity assertions shared by the host tests (include after unity.h). */
#include "unity.h"

/* An error chain `e` is there and its outermost tag is `expected`: a missing error is a clean FAIL, not a NULL dereference. */
#define TEST_ASSERT_ERR_TAG(expected, e)                                                     \
  do {                                                                                    \
    TEST_ASSERT_NOT_NULL_MESSAGE((e), "expected an error chain, got success (NULL)");     \
    TEST_ASSERT_EQUAL_HEX32((expected), (e)->tag);                                          \
  } while (0)
