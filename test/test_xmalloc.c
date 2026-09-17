#include "common/xmalloc.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

void test_xmalloc(void) {
    long initial_count = xmalloc_get_allocation_count();
    
    // Test malloc
    char *p1 = xmalloc(10);
    assert(p1 != NULL);
    assert(xmalloc_get_allocation_count() == initial_count + 1);
    
    // Test calloc
    int *p2 = xcalloc(5, sizeof(int));
    assert(p2 != NULL);
    assert(xmalloc_get_allocation_count() == initial_count + 2);
    for (int i = 0; i < 5; i++) {
        assert(p2[i] == 0);
    }
    
    // Test realloc
    p1 = xrealloc(p1, 20);
    assert(p1 != NULL);
    assert(xmalloc_get_allocation_count() == initial_count + 2); // Realloc doesn't change count if ptr exists
    
    // Test free
    xfree(p1);
    assert(xmalloc_get_allocation_count() == initial_count + 1);
    xfree(p2);
    assert(xmalloc_get_allocation_count() == initial_count);
    
    // Test strdup
    char *s = xstrdup("hello");
    assert(s != NULL);
    assert(strcmp(s, "hello") == 0);
    assert(xmalloc_get_allocation_count() == initial_count + 1);
    xfree(s);
    assert(xmalloc_get_allocation_count() == initial_count);
}
