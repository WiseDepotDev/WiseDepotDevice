#include "unity.h"

int unity_tests_run = 0;
int unity_tests_failed = 0;

void UnityBegin(const char* filename) {
    printf("-----------------------\n");
    printf("Running Tests: %s\n", filename);
    printf("-----------------------\n");
    unity_tests_run = 0;
    unity_tests_failed = 0;
}

int UnityEnd(void) {
    printf("-----------------------\n");
    printf("%d Tests %d Failures %d Ignored\n", unity_tests_run, unity_tests_failed, 0);
    if (unity_tests_failed == 0) {
        printf("OK\n");
        return 0;
    } else {
        printf("FAIL\n");
        return 1;
    }
}
