#include <stdio.h>

extern void lexems_test(void);
extern void integration_test(void);

int main(void)
{
    printf("Running parser tests...\n\n");

    lexems_test();
    integration_test();

    printf("\nAll parser tests passed!\n");
    return 0;
}
