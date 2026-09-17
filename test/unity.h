#ifndef UNITY_FRAMEWORK_H
#define UNITY_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int unity_tests_run;
extern int unity_tests_failed;

void UnityBegin(const char* filename);
int UnityEnd(void);

#define UNITY_BEGIN() UnityBegin(__FILE__)
#define UNITY_END() UnityEnd()

#define RUN_TEST(func) \
    do { \
        extern void setUp(void); \
        extern void tearDown(void); \
        setUp(); \
        func(); \
        tearDown(); \
        unity_tests_run++; \
    } while(0)

#define TEST_ASSERT_EQUAL(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAIL at %s:%d: Expected %d Was %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); \
            unity_tests_failed++; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            printf("FAIL at %s:%d: Expected '%s' Was '%s'\n", __FILE__, __LINE__, (expected), (actual)); \
            unity_tests_failed++; \
        } \
    } while(0)

#define TEST_ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL at %s:%d: Expected TRUE\n", __FILE__, __LINE__); \
            unity_tests_failed++; \
        } \
    } while(0)

#define TEST_ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            printf("FAIL at %s:%d: Expected FALSE\n", __FILE__, __LINE__); \
            unity_tests_failed++; \
        } \
    } while(0)

#define TEST_ASSERT_NOT_NULL(pointer) \
    do { \
        if ((pointer) == NULL) { \
            printf("FAIL at %s:%d: Expected NOT NULL\n", __FILE__, __LINE__); \
            unity_tests_failed++; \
        } \
    } while(0)

#endif
