#include <stdio.h>

extern void naming_test(void);
extern void const_convert_test(void);
extern void enum_test(void);

int main(void)
{
    printf("Running gremlinc tests...\n\n");

    naming_test();
    const_convert_test();
    enum_test();

    printf("\nAll gremlinc tests passed!\n");
    return 0;
}
