#include <stdlib.h>
#include <string.h>

#include "args.h"

void
args_print_usage(FILE *f)
{
	fprintf(f,
		"Usage: gremlinc-gen [-R <imports-root>] [-o <out-dir>] <proto>...\n"
		"\n"
		"  -R <dir>   root directory imports are resolved from.  Positional\n"
		"             <proto> arguments are also read as <dir>/<proto>.  The\n"
		"             logical path stored for each input (matched against\n"
		"             `import \"...\"` strings in other files) is the <proto>\n"
		"             argument verbatim.  Default: \".\".\n"
		"  -o <dir>   write generated .pb.h files into <dir>.  Default: \".\".\n"
		"  -h         show this help\n"
		"\n"
		"Example:\n"
		"  gremlinc-gen -R protos -o gen \\\n"
		"      google/protobuf/any.proto google/protobuf/timestamp.proto\n");
}

bool
args_parse(struct args *out, int argc, char **argv)
{
	out->imports_root = ".";
	out->out_dir = ".";
	out->inputs = NULL;
	out->n_inputs = 0;

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			args_print_usage(stdout);
			return true;
		}
		if (strcmp(a, "-R") == 0 || strcmp(a, "--imports-root") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "gremlinc-gen: %s needs an argument\n", a);
				args_print_usage(stderr);
				return false;
			}
			out->imports_root = argv[i];
			continue;
		}
		if (strcmp(a, "-o") == 0) {
			if (++i >= argc) {
				fprintf(stderr, "gremlinc-gen: -o needs an argument\n");
				args_print_usage(stderr);
				return false;
			}
			out->out_dir = argv[i];
			continue;
		}
		if (a[0] == '-' && a[1] != '\0') {
			fprintf(stderr, "gremlinc-gen: unknown option: %s\n", a);
			args_print_usage(stderr);
			return false;
		}
		break;
	}
	if (i >= argc) {
		fprintf(stderr, "gremlinc-gen: no input files\n");
		args_print_usage(stderr);
		return false;
	}
	out->inputs = (const char **)&argv[i];
	out->n_inputs = (size_t)(argc - i);
	return true;
}
