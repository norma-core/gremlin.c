#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gremlinc/lib.h"
#include "gremlind/lib.h"

#include "args.h"
#include "emit_file.h"
#include "io.h"
#include "sources.h"

static int
run_pipeline(const struct args *args,
	     struct source_list *sources,
	     struct gremlind_arena *arena)
{
	struct gremlind_source *gs = calloc(sources->count, sizeof *gs);
	if (gs == NULL) {
		fprintf(stderr, "gremlinc-gen: out of memory\n");
		return 1;
	}
	for (size_t i = 0; i < sources->count; i++) {
		gs[i].path = sources->items[i].path;
		gs[i].path_len = strlen(sources->items[i].path);
		gremlinp_parser_buffer_init(&gs[i].buf,
					    sources->items[i].buf, 0);
	}

	struct gremlind_resolve_context ctx;
	gremlind_resolve_context_init(&ctx, arena, gs, sources->count);

	enum gremlinp_parsing_error err;
	if ((err = gremlind_build_all(&ctx)))          goto fail;
	if ((err = gremlind_link_imports(&ctx)))       goto fail;
	if ((err = gremlind_check_no_cycles(&ctx)))    goto fail;
	for (size_t i = 0; i < ctx.n_sources; i++) {
		err = gremlind_compute_scoped_names(arena, ctx.files[i]);
		if (err) goto fail;
	}
	if ((err = gremlind_compute_visibility(&ctx))) goto fail;
	if ((err = gremlind_propagate_extends(&ctx)))  goto fail;
	if ((err = gremlind_resolve_type_refs(&ctx)))  goto fail;

	struct gremlinc_name_scope global_scope;
	gremlinc_name_scope_init(&global_scope, arena);
	for (size_t i = 0; i < ctx.n_sources; i++) {
		err = gremlinc_assign_c_names(&global_scope, ctx.files[i]);
		if (err) goto fail;
	}

	/* Emit one .pb.h per listed input — every file the caller passed
	 * on the command line gets its own output. */
	for (size_t i = 0; i < sources->count; i++) {
		struct gremlinc_writer w;
		if (!gremlinc_writer_init_owned(&w, 64 * 1024)) {
			fprintf(stderr, "gremlinc-gen: writer init failed\n");
			free(gs);
			return 1;
		}

		err = emit_file(arena, &global_scope, ctx.files[i],
				sources->items[i].path, &w);
		if (err != GREMLINP_OK) {
			gremlinc_writer_dispose(&w);
			goto fail;
		}

		char *out_path = derive_output_path(args->out_dir,
						    sources->items[i].path);
		if (out_path == NULL) {
			gremlinc_writer_dispose(&w);
			fprintf(stderr, "gremlinc-gen: out of memory\n");
			free(gs);
			return 1;
		}
		if (ensure_parent_dir(out_path) != 0) {
			fprintf(stderr,
				"gremlinc-gen: cannot create directory for %s: %s\n",
				out_path, strerror(errno));
			free(out_path);
			gremlinc_writer_dispose(&w);
			free(gs);
			return 1;
		}
		FILE *f = fopen(out_path, "wb");
		if (f == NULL) {
			fprintf(stderr, "gremlinc-gen: cannot write %s: %s\n",
				out_path, strerror(errno));
			free(out_path);
			gremlinc_writer_dispose(&w);
			free(gs);
			return 1;
		}
		if (fwrite(w.buf, 1, w.offset, f) != w.offset) {
			fclose(f);
			free(out_path);
			gremlinc_writer_dispose(&w);
			fprintf(stderr, "gremlinc-gen: write failed: %s\n",
				strerror(errno));
			free(gs);
			return 1;
		}
		fclose(f);
		free(out_path);
		gremlinc_writer_dispose(&w);
	}

	free(gs);
	return 0;

fail:
	fprintf(stderr,
		"gremlinc-gen: pipeline error %s (source #%zu, offset %zu)\n",
		gremlinp_parsing_error_to_string(err),
		ctx.failed_source_idx, ctx.error_offset);
	free(gs);
	return 1;
}

int
main(int argc, char **argv)
{
	struct args args;
	if (!args_parse(&args, argc, argv)) return 1;
	if (args.n_inputs == 0) return 0;	/* -h / --help was shown */

	struct source_list sources = { 0 };
	for (size_t i = 0; i < args.n_inputs; i++) {
		if (!sources_load(&sources, args.imports_root, args.inputs[i])) {
			sources_free(&sources);
			return 1;
		}
	}

	struct gremlind_arena arena;
	if (!gremlind_arena_init_malloc(&arena, 1u << 20)) {
		fprintf(stderr, "gremlinc-gen: arena init failed\n");
		sources_free(&sources);
		return 1;
	}

	int rc = run_pipeline(&args, &sources, &arena);

	gremlind_arena_free_malloc(&arena);
	sources_free(&sources);
	return rc;
}
