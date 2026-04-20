#ifndef _GREMLINC_GEN_ARGS_H_
#define _GREMLINC_GEN_ARGS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Parsed CLI arguments.
 *
 * `imports_root` is the directory imports are resolved from — every
 * `import "a/b.proto"` is looked up as `<imports_root>/a/b.proto`,
 * and every positional input is read as `<imports_root>/<input>`.
 * The logical path stored in the resolve context is the input string
 * verbatim (e.g. `a/b.proto`), which is what matches against import
 * spans the linker sees.
 *
 * Pointers here alias into argv — no ownership.
 */
struct args {
	const char	 *imports_root;	/* default: "." */
	const char	 *out_dir;	    /* default: "." */
	const char	**inputs;
	size_t		  n_inputs;
};

/*
 * Parse argv into *out. Returns true on success. On `-h` / `--help`
 * prints usage to stdout and returns true with n_inputs == 0 (caller
 * should exit 0). On bad usage prints an error + usage to stderr and
 * returns false.
 */
bool	args_parse(struct args *out, int argc, char **argv);

void	args_print_usage(FILE *f);

#endif /* !_GREMLINC_GEN_ARGS_H_ */
