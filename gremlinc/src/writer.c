#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "gremlinc/writer.h"

void
gremlinc_writer_init(struct gremlinc_writer *w, char *buf, size_t cap)
{
	w->buf = buf;
	w->cap = cap;
	w->offset = 0;
	w->owns_buf = false;
}

bool
gremlinc_writer_init_owned(struct gremlinc_writer *w, size_t initial_cap)
{
	if (initial_cap < 64) initial_cap = 64;
	char *p = malloc(initial_cap);
	if (p == NULL) return false;
	w->buf = p;
	w->cap = initial_cap;
	w->offset = 0;
	w->owns_buf = true;
	return true;
}

void
gremlinc_writer_dispose(struct gremlinc_writer *w)
{
	if (w == NULL || !w->owns_buf) return;
	free(w->buf);
	w->buf = NULL;
	w->cap = 0;
	w->offset = 0;
	w->owns_buf = false;
}

/* Grow an owned buffer to at least `need` bytes — doubles until the
 * need fits. Returns false on OOM; cap stays at its previous value
 * so subsequent writes overflow cleanly. */
static bool
ensure_cap(struct gremlinc_writer *w, size_t need)
{
	if (need <= w->cap) return true;
	if (!w->owns_buf) return false;
	size_t new_cap = w->cap;
	while (new_cap < need) new_cap *= 2;
	char *p = realloc(w->buf, new_cap);
	if (p == NULL) return false;
	w->buf = p;
	w->cap = new_cap;
	return true;
}

enum gremlinp_parsing_error
gremlinc_write(struct gremlinc_writer *w, const char *s, size_t len)
{
	if (w == NULL || s == NULL) return GREMLINP_ERROR_NULL_POINTER;
	if (!ensure_cap(w, w->offset + len)) return GREMLINP_ERROR_BUFFER_OVERFLOW;
	memcpy(w->buf + w->offset, s, len);
	w->offset += len;
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_write_cstr(struct gremlinc_writer *w, const char *s)
{
	if (s == NULL) return GREMLINP_ERROR_NULL_POINTER;
	return gremlinc_write(w, s, strlen(s));
}

enum gremlinp_parsing_error
gremlinc_write_i32(struct gremlinc_writer *w, int32_t v)
{
	char buf[16];
	int n = snprintf(buf, sizeof buf, "%d", (int)v);
	if (n < 0 || n >= (int)sizeof buf) return GREMLINP_ERROR_OVERFLOW;
	return gremlinc_write(w, buf, (size_t)n);
}

void
gremlinc_writer_trim_trailing_blank(struct gremlinc_writer *w)
{
	if (w == NULL) return;
	/* Keep at most one trailing '\n'. Walk back through a run of
	 * '\n' characters, stop when only one remains. */
	while (w->offset >= 2 &&
	       w->buf[w->offset - 1] == '\n' &&
	       w->buf[w->offset - 2] == '\n') {
		w->offset--;
	}
}
