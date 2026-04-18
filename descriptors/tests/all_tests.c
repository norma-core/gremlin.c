#include <stdio.h>

extern void arena_test(void);
extern void build_test(void);
extern void name_test(void);
extern void resolve_test(void);
extern void integration_test(void);

int main(void)
{
    printf("Running gremlind tests...\n\n");

    arena_test();
    build_test();
    name_test();
    resolve_test();
    integration_test();

    printf("\nAll gremlind tests passed!\n");
    return 0;
}
