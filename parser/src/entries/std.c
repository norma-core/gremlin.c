/*
 * Trivial library wrappers. Kept in a separate .c file so they are built
 * into libgremlinp.a but NOT passed to frama-c. WP sees only the extern
 * declarations in include/gremlinp/std.h and treats them as opaque
 * library functions with the stated contracts, which is what we want:
 *  - it lets us restate libc contracts that are incomplete upstream
 *    (e.g. strtod not mentioning errno);
 *  - it avoids emitting -wp-rte "is_nan_or_infinite" assertions on the
 *    bodies of the sentinel-value helpers.
 */
#include <stdlib.h>
#include <math.h>

double
gremlinp_strtod(const char *nptr, char **endptr)
{
    return strtod(nptr, endptr);
}

long long
gremlinp_strtoll(const char *nptr, char **endptr, int base)
{
    return strtoll(nptr, endptr, base);
}

unsigned long long
gremlinp_strtoull(const char *nptr, char **endptr, int base)
{
    return strtoull(nptr, endptr, base);
}

double
gremlinp_pos_infinity(void)
{
    return INFINITY;
}

double
gremlinp_neg_infinity(void)
{
    return -INFINITY;
}

double
gremlinp_quiet_nan(void)
{
    return NAN;
}
