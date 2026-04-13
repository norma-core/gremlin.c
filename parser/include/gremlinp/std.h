#ifndef _GREMLINP_STD_H_
#define _GREMLINP_STD_H_

#include <stdlib.h>
#include <errno.h>
#include <math.h>

/*
 * Locally enriched libc wrappers.
 *
 * Frama-C ships ACSL contracts for most of <stdlib.h>, but they are
 * incomplete in a way that hurts our verification. In particular,
 * `strtod` is declared without any clause about `errno`, so WP infers
 * that `errno` is unchanged across the call. Combined with our
 * `errno = 0` immediately before the call, WP then concludes that
 * `errno == ERANGE` is impossible and marks the overflow-handling
 * branch as dead code (smoke-test failure).
 *
 * We provide thin wrappers below whose contract DOES say `errno` may
 * be modified, mirroring the contracts frama-c already ships for
 * `strtoll` / `strtoull`. Use these instead of calling `strtod`
 * directly from parser code.
 */

/*@ requires valid_string_nptr: valid_read_string(nptr);
  @ requires separation:        \separated(nptr, endptr);
  @ requires valid_endptr:      \valid(endptr);
  @ assigns  *endptr,
  @          __fc_errno;
  @*/
extern double gremlinp_strtod(const char *nptr, char **endptr);

/*@ requires valid_string_nptr: valid_read_string(nptr);
  @ requires separation:        \separated(nptr, endptr);
  @ requires valid_endptr:      \valid(endptr);
  @ requires base_range:        base == 0 || (2 <= base <= 36);
  @ assigns  *endptr,
  @          __fc_errno;
  @*/
extern long long gremlinp_strtoll(const char *nptr, char **endptr, int base);

/*@ requires valid_string_nptr: valid_read_string(nptr);
  @ requires separation:        \separated(nptr, endptr);
  @ requires valid_endptr:      \valid(endptr);
  @ requires base_range:        base == 0 || (2 <= base <= 36);
  @ assigns  *endptr,
  @          __fc_errno;
  @*/
extern unsigned long long gremlinp_strtoull(const char *nptr, char **endptr, int base);

/*
 * Floating-point sentinel values. Frama-C / WP does not recognize the
 * `INFINITY` / `NAN` macros expanded by <math.h> — it emits "Hide sub-term
 * definition / Unexpected constant literal INFINITY" warnings and then
 * cannot reason about any post-condition of a function that mentions
 * them. We wrap the macros in trivial pure-double-returning functions
 * whose contract assigns nothing. They are declared extern so WP treats
 * them as opaque library functions (with the stated contract) and does
 * NOT run -wp-rte on their bodies, which would otherwise emit an
 * is_nan_or_infinite assertion that cannot be discharged.
 */

/*@ assigns \nothing; */
extern double gremlinp_pos_infinity(void);

/*@ assigns \nothing; */
extern double gremlinp_neg_infinity(void);

/*@ assigns \nothing; */
extern double gremlinp_quiet_nan(void);

#endif /* !_GREMLINP_STD_H_ */
