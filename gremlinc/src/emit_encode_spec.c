#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gremlin.h"
#include "emit_common.h"

/*
 * ACSL for the write side. A message gets it when its fields are in the
 * families below, its children are non-recursive messages of the same file
 * that have it too, and no repeated message sits under another one; others
 * are left as is.
 */

#define WT_VARINT_NUM		0u
#define WT_FIXED64_NUM		1u
#define WT_LEN_PREFIX_NUM	2u
#define WT_FIXED32_NUM		5u

enum ek {
	EK_SCALAR, EK_ENUM, EK_BYTES, EK_MSG,
	EK_REP_MSG, EK_REP_BYTES, EK_REP_VARINT, EK_REP_FIXED
};

#define WEIGHT_MAX	32
#define COST_MAX	2048
#define FILE_MAX	65536

struct ep_field {
	enum ek			 kind;
	int			 child;		/* plan index, -1 when no message */
	const char		*ctype;		/* element type of a value array */
	const char		*tag;		/* avoid-predicate suffix for it */
	enum scalar_kind	 sk;
	unsigned		 width;		/* EK_REP_FIXED element width */
	unsigned		 tagb;		/* bytes of the (packed) tag */
	unsigned		 utagb;		/* bytes of the unpacked tag */
	/* names, set on emission */
	const char		*fname;
	const char		*def;
	const char		*t;		/* the field's share of tsize */
	const char		*tf;		/* frame lemma of t */
	const char		*term, *tframe, *sum, *frame, *ind, *mono, *tnull;
};

struct ep_msg {
	const struct gremlind_message	*m;
	bool				 ok;
	bool				 rep;	/* repeated message somewhere below */
	bool				 elem;	/* element of a repeated message field */
	bool				 kids;	/* has a message field */
	size_t				 weight;	/* nodes and 64-bit fields below */
	size_t				 nf;
	struct ep_field			*f;
	const char			*tname;
	const char *tsize, *own, *dsame, *csame, *sized, *valid, *bufsep;
	const char *tsize_frame, *sized_frame, *dsame_trans, *bufsep_sub, *nonneg;
	const char *pos;	/* only tsize >= 0, for the loop over an array of these */
};

struct ep_target {
	int		 msg;		/* >= 0: a message; -1: a value array */
	const char	*ctype;
	const char	*tag;
};

struct ep_pair {
	int			 x;
	struct ep_target	 t;
	const char		*name;
};

struct ep_pairs {
	struct ep_pair	*items;
	size_t		 count, cap;
};

struct gremlinc_encode_plan {
	struct gremlind_arena	*arena;
	struct ep_msg		*msgs;
	size_t			 count;
	struct ep_pairs		 avoid;		/* X's subtree avoids target T */
	struct ep_pairs		 navoid;	/* every object of X avoided by T's subtree */
	struct gremlinc_name_scope *logic;
	const struct gremlinc_name_scope *reserved;
};

struct gremlinc_espec {
	struct gremlinc_encode_plan	*plan;
	struct ep_msg			*e;
	int				 idx;
};

static enum gremlinp_parsing_error wf(struct gremlinc_writer *w, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
static const char *dup(struct gremlind_arena *a, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static enum gremlinp_parsing_error
wf(struct gremlinc_writer *w, const char *fmt, ...)
{
	char small[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(small, sizeof small, fmt, ap);
	va_end(ap);
	if (n < 0) return GREMLINP_ERROR_OUT_OF_MEMORY;
	if ((size_t)n < sizeof small) return gremlinc_write(w, small, (size_t)n);
	char *big = malloc((size_t)n + 1);
	if (big == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	va_start(ap, fmt);
	vsnprintf(big, (size_t)n + 1, fmt, ap);
	va_end(ap);
	enum gremlinp_parsing_error err = gremlinc_write(w, big, (size_t)n);
	free(big);
	return err;
}

#define F(...)	do {							\
			err = wf(w, __VA_ARGS__);			\
			if (err != GREMLINP_OK) return err;		\
		} while (0)


static const char *
dup(struct gremlind_arena *a, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) return NULL;
	char *out = gremlind_arena_alloc(a, (size_t)n + 1);
	if (out == NULL) return NULL;
	va_start(ap, fmt);
	vsnprintf(out, (size_t)n + 1, fmt, ap);
	va_end(ap);
	return out;
}

/* Logic names get their own scope, checked against C names, so they never take
 * a C function's name; Frama-C keeps the two apart. */
static const char *
claim(struct gremlinc_name_scope *s, const struct gremlinc_name_scope *c,
      const char *a, const char *b, const char *d)
{
	const char *raw = dup(s->arena, "%s%s%s", a, b, d ? d : "");
	if (raw == NULL) return NULL;
	for (;;) {
		const char *n = gremlinc_name_scope_mangle(s, raw, strlen(raw));
		if (n == NULL || c == NULL || !gremlinc_name_scope_has(c, n)) return n;
	}
}

static unsigned
tag_bytes(const struct gremlind_field *f, unsigned wt)
{
	return (unsigned)gremlin_varint_size(((uint32_t)f->parsed.index << 3) | wt);
}

static bool
is_rep(enum ek k)
{
	return k == EK_REP_MSG || k == EK_REP_BYTES ||
	       k == EK_REP_VARINT || k == EK_REP_FIXED;
}

static bool
is_array(enum ek k)
{
	return k == EK_REP_BYTES || k == EK_REP_VARINT || k == EK_REP_FIXED;
}

static bool
has_sum(enum ek k)
{
	return k == EK_REP_MSG || k == EK_REP_BYTES || k == EK_REP_VARINT;
}

static bool
is_child(enum ek k)
{
	return k == EK_MSG || k == EK_REP_MSG;
}

/* Same C type as a `_size` cache (size_t is uint64_t here): in WP's typed
 * memory model only these can alias one, whatever the width of the rest. */
static bool
wide(const struct ep_field *f)
{
	return f->ctype != NULL && strcmp(f->ctype, "uint64_t") == 0;
}

/* Logic size of one varint-family value, e.g. "varint_size(m->f)". */
static const char *
value_size(struct gremlind_arena *a, enum scalar_kind k, bool is_enum, const char *ref)
{
	if (is_enum) return dup(a, "varint_size((uint64_t)(int64_t)%s)", ref);
	switch (k) {
	case SCALAR_INT32:	return dup(a, "varint_size((uint64_t)(int64_t)%s)", ref);
	case SCALAR_INT64:	return dup(a, "varint_size((uint64_t)%s)", ref);
	case SCALAR_UINT32:
	case SCALAR_UINT64:	return dup(a, "varint_size(%s)", ref);
	case SCALAR_FIXED32:
	case SCALAR_SFIXED32:	return "4";
	case SCALAR_FIXED64:
	case SCALAR_SFIXED64:	return "8";
	case SCALAR_BOOL:	return "1";
	default:		return NULL;
	}
}

static bool
scalar_supported(enum scalar_kind k)
{
	return k != SCALAR_SINT32 && k != SCALAR_SINT64 &&
	       k != SCALAR_FLOAT && k != SCALAR_DOUBLE;
}

/* ------------------------------------------------------------------------
 * Plan
 * ------------------------------------------------------------------------ */

static int
find_msg(const struct gremlinc_encode_plan *p, const struct gremlind_message *m,
	 size_t upto)
{
	for (size_t i = 0; i < upto; i++) {
		if (p->msgs[i].m == m) return (int)i;
	}
	return -1;
}

static bool
classify(const struct gremlinc_encode_plan *p, size_t self,
	 const struct gremlind_field *f, struct ep_field *out)
{
	memset(out, 0, sizeof *out);
	out->child = -1;
	bool rep = f->parsed.label == GREMLINP_FIELD_LABEL_REPEATED;

	switch (f->type.kind) {
	case GREMLIND_TYPE_REF_MAP:
		return false;
	case GREMLIND_TYPE_REF_MESSAGE:
		out->child = find_msg(p, f->type.u.message, self);
		if (out->child < 0 || !p->msgs[out->child].ok) return false;
		out->kind = rep ? EK_REP_MSG : EK_MSG;
		out->tagb = tag_bytes(f, WT_LEN_PREFIX_NUM);
		if (rep && p->msgs[out->child].rep) return false;
		/* A loop over elements with children of their own does not prove in time. */
		if (rep && p->msgs[out->child].kids) return false;
		return true;
	case GREMLIND_TYPE_REF_ENUM:
		if (rep) {
			out->kind = EK_REP_VARINT;
			out->ctype = f->type.u.enumeration->c_name;
			out->tag = out->ctype;
			out->tagb = tag_bytes(f, WT_LEN_PREFIX_NUM);
			out->utagb = tag_bytes(f, WT_VARINT_NUM);
			out->sk = SCALAR_INT32;
			return true;
		}
		out->kind = EK_ENUM;
		out->tagb = tag_bytes(f, WT_VARINT_NUM);
		return true;
	case GREMLIND_TYPE_REF_BUILTIN:
		break;
	default:
		return false;
	}

	if (gremlinc_builtin_is_bytes(f->type.u.builtin.start, f->type.u.builtin.length)) {
		/* The encoder compares with a [default] by memcmp; the logic size has no such test. */
		if (f->has_default) return false;
		out->tagb = tag_bytes(f, WT_LEN_PREFIX_NUM);
		if (rep) {
			out->kind = EK_REP_BYTES;
			out->ctype = "struct gremlin_bytes";
			out->tag = "bytes";
		} else {
			out->kind = EK_BYTES;
		}
		return true;
	}
	const struct scalar_info *si = gremlinc_scalar_lookup(
		f->type.u.builtin.start, f->type.u.builtin.length);
	if (si == NULL || !scalar_supported(si->kind)) return false;
	out->sk = si->kind;
	out->ctype = si->c_type;
	/* The encoder writes `m->f ? 1u : 0u` under `if (m->f != false)`: a dead arm. */
	if (!rep && si->kind == SCALAR_BOOL) return false;
	if (!rep) {
		out->kind = EK_SCALAR;
		out->tagb = tag_bytes(f, si->wire_type);
		return true;
	}
	out->tag = si->c_type;
	out->tagb = tag_bytes(f, WT_LEN_PREFIX_NUM);
	out->utagb = tag_bytes(f, si->wire_type);
	if (si->wire_type == WT_FIXED32_NUM || si->wire_type == WT_FIXED64_NUM) {
		out->kind = EK_REP_FIXED;
		out->width = si->wire_type == WT_FIXED32_NUM ? 4u : 8u;
	} else {
		out->kind = EK_REP_VARINT;
	}
	return true;
}

static bool
target_eq(const struct ep_target *a, const struct ep_target *b)
{
	if (a->msg != b->msg) return false;
	if (a->msg >= 0) return true;
	return strcmp(a->ctype, b->ctype) == 0;
}

static enum gremlinp_parsing_error
pair_add(struct gremlind_arena *arena, struct ep_pairs *ps, int x,
	 struct ep_target t, bool *changed)
{
	for (size_t i = 0; i < ps->count; i++) {
		if (ps->items[i].x == x && target_eq(&ps->items[i].t, &t)) return GREMLINP_OK;
	}
	if (ps->count == ps->cap) {
		size_t cap = ps->cap ? ps->cap * 2 : 16;
		struct ep_pair *n = gremlind_arena_alloc(arena, cap * sizeof *n);
		if (n == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		if (ps->count) memcpy(n, ps->items, ps->count * sizeof *n);
		ps->items = n;
		ps->cap = cap;
	}
	ps->items[ps->count].x = x;
	ps->items[ps->count].t = t;
	ps->items[ps->count].name = NULL;
	ps->count++;
	*changed = true;
	return GREMLINP_OK;
}

static struct ep_target
msg_target(int i)
{
	struct ep_target t = { i, NULL, NULL };
	return t;
}

static struct ep_target
array_target(const struct ep_field *f)
{
	struct ep_target t = { -1, f->ctype, f->tag };
	return t;
}

enum gremlinp_parsing_error
gremlinc_encode_plan_build(struct gremlind_arena *arena,
			   struct gremlinc_name_scope *logic,
			   const struct gremlinc_name_scope *reserved,
			   struct gremlind_message *const *order, size_t count,
			   struct gremlinc_encode_plan **out)
{
	enum gremlinp_parsing_error err;
	if (arena == NULL || logic == NULL || out == NULL || (count > 0 && order == NULL))
		return GREMLINP_ERROR_NULL_POINTER;
	struct gremlinc_encode_plan *p = gremlind_arena_alloc(arena, sizeof *p);
	if (p == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	memset(p, 0, sizeof *p);
	p->arena = arena;
	p->count = count;
	p->logic = logic;
	p->reserved = reserved;
	p->msgs = gremlind_arena_alloc(arena, (count ? count : 1) * sizeof *p->msgs);
	if (p->msgs == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	memset(p->msgs, 0, (count ? count : 1) * sizeof *p->msgs);

	size_t total = 0;
	for (size_t i = 0; i < count; i++) {
		struct ep_msg *e = &p->msgs[i];
		e->m = order[i];
		e->nf = order[i]->fields.count;
		e->f = gremlind_arena_alloc(arena, (e->nf ? e->nf : 1) * sizeof *e->f);
		if (e->f == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		e->ok = true;
		for (size_t j = 0; j < e->nf && e->ok; j++) {
			if (!classify(p, i, &order[i]->fields.items[j], &e->f[j])) e->ok = false;
		}
		if (!e->ok) continue;
		size_t cost = 0, slots = 0;
		bool rep_slot = false;
		e->weight = 1;
		for (size_t j = 0; j < e->nf; j++) {
			const struct ep_field *f = &e->f[j];
			if (f->kind == EK_REP_MSG) e->rep = true;
			if (f->kind == EK_MSG && p->msgs[f->child].rep) e->rep = true;
			if ((f->kind == EK_SCALAR && wide(f)) || f->kind == EK_BYTES || is_rep(f->kind))
				e->weight++;
			if (is_child(f->kind)) e->weight += p->msgs[f->child].weight;
			if (is_child(f->kind)) slots++;
			if (is_child(f->kind)) e->kids = true;
			if (f->kind == EK_REP_MSG) rep_slot = true;
		}
		/* A repeated message next to other message fields is not proved yet. */
		if (rep_slot && slots > 1) e->ok = false;
		for (size_t j = 0; j < e->nf; j++)
			if (is_child(e->f[j].kind) && e->weight <= WEIGHT_MAX)
				cost += p->msgs[e->f[j].child].weight * e->weight;
		/* The frame asserts grow with slots times subtree size; past this the
		 * message is left without contracts, like any unsupported one. */
		if (e->weight > WEIGHT_MAX || cost > COST_MAX || total + cost > FILE_MAX) e->ok = false;
		else if (e->ok) total += cost;
	}

	bool changed = false;
	for (size_t i = 0; i < count; i++) {
		struct ep_msg *e = &p->msgs[i];
		if (!e->ok) continue;
		for (size_t a = 0; a < e->nf; a++) {
			if (!is_child(e->f[a].kind)) continue;
			int c = e->f[a].child;
			if ((err = pair_add(arena, &p->avoid, c, msg_target((int)i), &changed))) return err;
			for (size_t b = 0; b < e->nf; b++) {
				if (!is_array(e->f[b].kind)) continue;
				if ((err = pair_add(arena, &p->avoid, c, array_target(&e->f[b]), &changed))) return err;
			}
			for (size_t b = a + 1; b < e->nf; b++) {
				if (!is_child(e->f[b].kind)) continue;
				if ((err = pair_add(arena, &p->navoid, c,
						    msg_target(e->f[b].child), &changed))) return err;
			}
			if (e->f[a].kind == EK_REP_MSG &&
			    (err = pair_add(arena, &p->navoid, c, msg_target(c), &changed))) return err;
		}
	}
	do {
		changed = false;
		for (size_t i = 0; i < p->avoid.count; i++) {
			struct ep_pair pr = p->avoid.items[i];
			struct ep_msg *x = &p->msgs[pr.x];
			for (size_t a = 0; a < x->nf; a++) {
				if (!is_child(x->f[a].kind)) continue;
				if ((err = pair_add(arena, &p->avoid, x->f[a].child, pr.t, &changed))) return err;
			}
		}
		for (size_t i = 0; i < p->navoid.count; i++) {
			struct ep_pair pr = p->navoid.items[i];
			struct ep_msg *x = &p->msgs[pr.x];
			if ((err = pair_add(arena, &p->avoid, pr.t.msg, msg_target(pr.x), &changed))) return err;
			for (size_t a = 0; a < x->nf; a++) {
				if (is_array(x->f[a].kind) &&
				    (err = pair_add(arena, &p->avoid, pr.t.msg,
						    array_target(&x->f[a]), &changed))) return err;
				if (is_child(x->f[a].kind) &&
				    (err = pair_add(arena, &p->navoid, x->f[a].child, pr.t, &changed))) return err;
			}
		}
	} while (changed);

	for (size_t i = 0; i < count; i++) {
		for (size_t j = 0; p->msgs[i].ok && j < p->msgs[i].nf; j++) {
			if (p->msgs[i].f[j].kind == EK_REP_MSG) p->msgs[p->msgs[i].f[j].child].elem = true;
		}
	}
	*out = p;
	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Per-message context
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_espec_begin(struct gremlinc_encode_plan *plan,
		     struct gremlinc_name_scope *scope,
		     const struct gremlind_message *m,
		     const char *const *fnames,
		     const char *const *defaults,
		     struct gremlinc_espec **out)
{
	*out = NULL;
	if (plan == NULL) return GREMLINP_OK;
	int idx = find_msg(plan, m, plan->count);
	if (idx < 0 || !plan->msgs[idx].ok) return GREMLINP_OK;

	struct gremlind_arena *a = plan->arena;
	struct ep_msg *e = &plan->msgs[idx];
	struct gremlinc_name_scope *L = plan->logic;
	const char *t = m->c_name;
	e->tname = t;
	e->tsize = claim(L, scope, t, "_tsize", NULL);
	e->own = claim(L, scope, t, "_own", NULL);
	e->dsame = claim(L, scope, t, "_dsame", NULL);
	e->csame = claim(L, scope, t, "_csame", NULL);
	e->sized = claim(L, scope, t, "_sized", NULL);
	e->valid = claim(L, scope, t, "_valid", NULL);
	e->bufsep = claim(L, scope, t, "_bufsep", NULL);
	e->tsize_frame = claim(L, scope, t, "_tsize_frame", NULL);
	e->sized_frame = claim(L, scope, t, "_sized_frame", NULL);
	e->dsame_trans = claim(L, scope, t, "_dsame_trans", NULL);
	e->bufsep_sub = claim(L, scope, t, "_bufsep_sub", NULL);
	e->nonneg = gremlinc_claim_avoiding(scope, plan->reserved, t, "_tsize_nonneg");
	e->pos = e->elem ? gremlinc_claim_avoiding(scope, plan->reserved, t, "_tsize_pos") : "";
	if (!e->tsize || !e->own || !e->dsame || !e->csame || !e->sized ||
	    !e->valid || !e->bufsep || !e->tsize_frame || !e->sized_frame ||
	    !e->dsame_trans || !e->bufsep_sub || !e->nonneg || !e->pos)
		return GREMLINP_ERROR_OUT_OF_MEMORY;

	for (size_t i = 0; i < e->nf; i++) {
		struct ep_field *f = &e->f[i];
		f->fname = fnames[i];
		f->def = defaults[i];
		const char *pre = dup(a, "%s_%s", t, fnames[i]);
		if (pre == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		f->t = claim(L, scope, pre, "_size", NULL);
		f->tf = claim(L, scope, pre, "_size_frame", NULL);
		if (f->t == NULL || f->tf == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		if (has_sum(f->kind)) {
			f->sum = claim(L, scope, pre, "_sum", NULL);
			f->frame = claim(L, scope, pre, "_sum_frame", NULL);
			f->ind = claim(L, scope, pre, "_sum_ind", NULL);
			f->mono = gremlinc_claim_avoiding(scope, plan->reserved, pre, "_mono");
			if (!f->sum || !f->frame || !f->ind || !f->mono)
				return GREMLINP_ERROR_OUT_OF_MEMORY;
		}
		if (f->kind == EK_REP_MSG) {
			f->term = claim(L, scope, pre, "_term", NULL);
			f->tframe = claim(L, scope, pre, "_term_frame", NULL);
			f->tnull = gremlinc_claim_avoiding(scope, plan->reserved, pre, "_term_null");
			if (!f->term || !f->tframe || !f->tnull) return GREMLINP_ERROR_OUT_OF_MEMORY;
		}
	}

	struct gremlinc_espec *sp = gremlind_arena_alloc(a, sizeof *sp);
	if (sp == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
	sp->plan = plan;
	sp->e = e;
	sp->idx = idx;
	*out = sp;
	return GREMLINP_OK;
}

/* ------------------------------------------------------------------------
 * Logic
 * ------------------------------------------------------------------------ */

static const struct ep_msg *
child_of(const struct gremlinc_espec *sp, const struct ep_field *f)
{
	return &sp->plan->msgs[f->child];
}

static const char *
avoid_name(const struct gremlinc_encode_plan *p, int x, struct ep_target t)
{
	for (size_t i = 0; i < p->avoid.count; i++) {
		if (p->avoid.items[i].x == x && target_eq(&p->avoid.items[i].t, &t))
			return p->avoid.items[i].name;
	}
	return NULL;
}

static const char *
navoid_name(const struct gremlinc_encode_plan *p, int x, int t)
{
	for (size_t i = 0; i < p->navoid.count; i++) {
		if (p->navoid.items[i].x == x && p->navoid.items[i].t.msg == t)
			return p->navoid.items[i].name;
	}
	return NULL;
}

/* `(m->f != \null ==> P((C *)m->f, rest))` or the \forall form over a
 * repeated field.  `pfx` is the expression holding the struct pointer. */
static enum gremlinp_parsing_error
each_child(struct gremlinc_writer *w, const char *pfx, const struct ep_field *f,
	   const char *child_t, const char *pred, const char *rest)
{
	enum gremlinp_parsing_error err;
	if (f->kind == EK_MSG) {
		F("(%s->%s != \\null ==> %s((%s *)%s->%s%s))",
		  pfx, f->fname, pred, child_t, pfx, f->fname, rest);
	} else {
		F("(\\forall integer k; 0 <= k < %s->%s_count && %s->%s[k] != \\null ==>\n"
		  "         %s((%s *)%s->%s[k]%s))",
		  pfx, f->fname, pfx, f->fname, pred, child_t, pfx, f->fname, rest);
	}
	return GREMLINP_OK;
}

/* Same, comparing two states: pointers read at L1. */
static enum gremlinp_parsing_error
each_child2(struct gremlinc_writer *w, const struct ep_field *f, const char *child_t,
	    const char *pred)
{
	enum gremlinp_parsing_error err;
	if (f->kind == EK_MSG) {
		F("(\\at(m->%s, L1) != \\null ==> %s{L1,L2}((%s *)\\at(m->%s, L1)))",
		  f->fname, pred, child_t, f->fname);
	} else {
		F("(\\forall integer k; 0 <= k < \\at(m->%s_count, L1) &&\n"
		  "         \\at(m->%s[k], L1) != \\null ==> %s{L1,L2}((%s *)\\at(m->%s[k], L1)))",
		  f->fname, f->fname, pred, child_t, f->fname);
	}
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_avoid(struct gremlinc_writer *w, struct gremlinc_name_scope *scope,
	   struct gremlinc_encode_plan *p, struct ep_pair *pr)
{
	enum gremlinp_parsing_error err;
	struct gremlinc_name_scope *L = p->logic;
	const struct ep_msg *x = &p->msgs[pr->x];
	const char *ttag = pr->t.msg >= 0 ? p->msgs[pr->t.msg].tname : pr->t.tag;
	pr->name = claim(L, scope, x->tname, "_avoid_", ttag);
	if (pr->name == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	const char *tgt, *rest;
	if (pr->t.msg >= 0) {
		F("/*@ predicate %s{L}(%s *m, %s *p) =\n      ",
		  pr->name, x->tname, p->msgs[pr->t.msg].tname);
		tgt = "p";
		rest = ", p";
		F("\\separated(p, m)");
	} else {
		F("/*@ predicate %s{L}(%s *m, %s *p, integer n) =\n      n <= 0 || (",
		  pr->name, x->tname, pr->t.ctype);
		tgt = "p + (0 .. n - 1)";
		rest = ", p, n";
		F("\\separated(p + (0 .. n - 1), m)");
	}
	for (size_t i = 0; i < x->nf; i++) {
		const struct ep_field *f = &x->f[i];
		if (is_array(f->kind)) {
			F(" &&\n      (m->%s_count == 0 || \\separated(%s, m->%s + (0 .. m->%s_count - 1)))",
			  f->fname, tgt, f->fname, f->fname);
		} else if (is_child(f->kind)) {
			const char *cn = avoid_name(p, f->child, pr->t);
			if (cn == NULL) return GREMLINP_ERROR_NULL_POINTER;
			F(" &&\n      ");
			if ((err = each_child(w, "m", f, p->msgs[f->child].tname, cn, rest))) return err;
		}
	}
	F(pr->t.msg >= 0 ? ";\n*/\n\n" : ");\n*/\n\n");
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_navoid(struct gremlinc_writer *w, struct gremlinc_name_scope *scope,
	    struct gremlinc_encode_plan *p, struct ep_pair *pr)
{
	enum gremlinp_parsing_error err;
	struct gremlinc_name_scope *L = p->logic;
	const struct ep_msg *x = &p->msgs[pr->x];
	const struct ep_msg *t = &p->msgs[pr->t.msg];
	pr->name = claim(L, scope, x->tname, "_navoid_", t->tname);
	if (pr->name == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;

	const char *self = avoid_name(p, pr->t.msg, msg_target(pr->x));
	if (self == NULL) return GREMLINP_ERROR_NULL_POINTER;
	F("/*@ predicate %s{L}(%s *m, %s *t) =\n      %s(t, m)",
	  pr->name, x->tname, t->tname, self);
	for (size_t i = 0; i < x->nf; i++) {
		const struct ep_field *f = &x->f[i];
		if (is_array(f->kind)) {
			const char *an = avoid_name(p, pr->t.msg, array_target(f));
			if (an == NULL) return GREMLINP_ERROR_NULL_POINTER;
			F(" &&\n      %s(t, m->%s, m->%s_count)", an, f->fname, f->fname);
		} else if (is_child(f->kind)) {
			const char *cn = navoid_name(p, f->child, pr->t.msg);
			if (cn == NULL) return GREMLINP_ERROR_NULL_POINTER;
			F(" &&\n      ");
			if ((err = each_child(w, "m", f, p->msgs[f->child].tname, cn, ", t"))) return err;
		}
	}
	F(";\n*/\n\n");
	return GREMLINP_OK;
}

static int
pair_pos(const struct ep_pair *pr)
{
	int t = pr->t.msg;
	return pr->x > t ? pr->x : t;
}

static enum gremlinp_parsing_error
emit_pairs(struct gremlinc_writer *w, struct gremlinc_name_scope *scope,
	   struct gremlinc_encode_plan *p, int z)
{
	enum gremlinp_parsing_error err;
	for (int x = 0; x <= z; x++) {
		for (size_t i = 0; i < p->avoid.count; i++) {
			struct ep_pair *pr = &p->avoid.items[i];
			if (pr->x != x || pair_pos(pr) != z) continue;
			if ((err = emit_avoid(w, scope, p, pr))) return err;
		}
	}
	for (int x = 0; x <= z; x++) {
		for (size_t i = 0; i < p->navoid.count; i++) {
			struct ep_pair *pr = &p->navoid.items[i];
			if (pr->x != x || pair_pos(pr) != z) continue;
			if ((err = emit_navoid(w, scope, p, pr))) return err;
		}
	}
	return GREMLINP_OK;
}

/* Logic size of one element of a packed array, `ref` naming it. */
static const char *
elem_size(struct gremlind_arena *a, const struct ep_field *f, const char *ref)
{
	/* Integer arithmetic, not a cast: the sum frame lemmas do not go through casts. */
	if (f->sk == SCALAR_INT32 || f->sk == SCALAR_INT64)
		return dup(a, "varint_size(%s < 0 ? %s + 18446744073709551616 : %s)", ref, ref, ref);
	return value_size(a, f->sk, false, ref);
}

static enum gremlinp_parsing_error
emit_field_logic(struct gremlinc_writer *w, struct gremlinc_espec *sp,
		 struct ep_field *f)
{
	enum gremlinp_parsing_error err;
	struct gremlind_arena *a = sp->plan->arena;
	const char *M = sp->e->tname;
	const char *fn = f->fname;
	const char *C = f->child >= 0 ? child_of(sp, f)->tname : NULL;
	const char *Cts = f->child >= 0 ? child_of(sp, f)->tsize : NULL;

	switch (f->kind) {
	case EK_REP_MSG:
		F("/*@ logic integer %s{L}(%s *e) =\n"
		  "      e != \\null ? %u + varint_size(%s(e)) + %s(e) : %u;\n\n",
		  f->term, C, f->tagb, Cts, Cts, f->tagb + 1);
		F("    lemma %s{L1,L2}: \\forall %s *e;\n"
		  "      (e != \\null ==> %s{L1,L2}(e)) ==> \\at(%s(e), L1) == \\at(%s(e), L2);\n\n",
		  f->tframe, C, child_of(sp, f)->dsame, f->term, f->term);
		F("    logic integer %s{L}(%s *m, integer n) =\n"
		  "      n <= 0 ? 0 : %s(m, n - 1) + %s((%s *)m->%s[n - 1]);\n\n",
		  f->sum, M, f->sum, f->term, C, fn);
		F("    lemma %s{L1,L2}: \\forall %s *m, integer n; 0 <= n ==>\n"
		  "      \\at(m->%s, L1) == \\at(m->%s, L2) ==>\n"
		  "      (\\forall integer k; 0 <= k < n ==>\n"
		  "         \\at(m->%s[k], L1) == \\at(m->%s[k], L2) &&\n"
		  "         \\at(%s((%s *)m->%s[k]), L1) == \\at(%s((%s *)m->%s[k]), L2)) ==>\n"
		  "      \\at(%s(m, n), L1) == \\at(%s(m, n), L2);\n*/\n",
		  f->frame, M, fn, fn, fn, fn, f->term, C, fn, f->term, C, fn, f->sum, f->sum);
		break;
	case EK_REP_BYTES:
		F("/*@ logic integer %s{L}(%s *m, integer n) =\n"
		  "      n <= 0 ? 0 :\n"
		  "      %s(m, n - 1) + %u + varint_size(m->%s[n - 1].len) + m->%s[n - 1].len;\n\n",
		  f->sum, M, f->sum, f->tagb, fn, fn);
		F("    lemma %s{L1,L2}: \\forall %s *m, integer n; 0 <= n ==>\n"
		  "      \\at(m->%s, L1) == \\at(m->%s, L2) ==>\n"
		  "      (\\forall integer k; 0 <= k < n ==> \\at(m->%s[k].len, L1) == \\at(m->%s[k].len, L2)) ==>\n"
		  "      \\at(%s(m, n), L1) == \\at(%s(m, n), L2);\n*/\n",
		  f->frame, M, fn, fn, fn, fn, f->sum, f->sum);
		break;
	case EK_REP_VARINT: {
		const char *ref = dup(a, "m->%s[n - 1]", fn);
		const char *es = ref ? elem_size(a, f, ref) : NULL;
		if (es == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		F("/*@ logic integer %s{L}(%s *m, integer n) =\n"
		  "      n <= 0 ? 0 : %s(m, n - 1) + %s;\n\n",
		  f->sum, M, f->sum, es);
		F("    lemma %s{L1,L2}: \\forall %s *m, integer n; 0 <= n ==>\n"
		  "      \\at(m->%s, L1) == \\at(m->%s, L2) ==>\n"
		  "      (\\forall integer k; 0 <= k < n ==> \\at(m->%s[k], L1) == \\at(m->%s[k], L2)) ==>\n"
		  "      \\at(%s(m, n), L1) == \\at(%s(m, n), L2);\n*/\n",
		  f->frame, M, fn, fn, fn, fn, f->sum, f->sum);
		break;
	}
	default:
		return GREMLINP_OK;
	}
	F("/*@ \\wp::strategy %s: \\tactic(\"Wp.induction\", \\pattern(0 <= n), \\select(n)); */\n", f->ind);
	F("/*@ \\wp::proof %s: %s; */\n\n", f->ind, f->frame);
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_field_size(struct gremlinc_writer *w, struct gremlinc_espec *sp,
		const struct ep_field *f)
{
	enum gremlinp_parsing_error err;
	struct gremlind_arena *a = sp->plan->arena;
	const char *fn = f->fname;
	const char *ref;

	F("    logic integer %s{L}(%s *m) =\n      ", f->t, sp->e->tname);
	switch (f->kind) {
	case EK_SCALAR:
	case EK_ENUM: {
		if ((ref = dup(a, "m->%s", fn)) == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		const char *vs = value_size(a, f->sk, f->kind == EK_ENUM, ref);
		if (vs == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		F("m->%s != %s ? %u + %s : 0;\n\n", fn, f->def, f->tagb, vs);
		break;
	}
	case EK_BYTES:
		F("m->%s.len > 0 ? %u + varint_size(m->%s.len) + m->%s.len : 0;\n\n",
		  fn, f->tagb, fn, fn);
		break;
	case EK_MSG: {
		const struct ep_msg *c = child_of(sp, f);
		F("m->%s != \\null ?\n"
		  "        %u + varint_size(%s((%s *)m->%s)) + %s((%s *)m->%s) : 0;\n\n",
		  fn, f->tagb, c->tsize, c->tname, fn, c->tsize, c->tname, fn);
		break;
	}
	case EK_REP_MSG:
	case EK_REP_BYTES:
		F("%s(m, m->%s_count);\n\n", f->sum, fn);
		break;
	case EK_REP_VARINT: {
		if ((ref = dup(a, "m->%s[0]", fn)) == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		const char *es = elem_size(a, f, ref);
		if (es == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		F("m->%s_count == 1 ? %u + %s :\n"
		  "      m->%s_count > 1 ? %u + varint_size(%s(m, m->%s_count)) + %s(m, m->%s_count) : 0;\n\n",
		  fn, f->utagb, es, fn, f->tagb, f->sum, fn, f->sum, fn);
		break;
	}
	case EK_REP_FIXED:
		F("m->%s_count == 1 ? %u + %u :\n"
		  "      m->%s_count > 1 ? %u + varint_size(m->%s_count * %u) + m->%s_count * %u : 0;\n\n",
		  fn, f->utagb, f->width, fn, f->tagb, fn, f->width, fn, f->width);
		break;
	}
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_own(struct gremlinc_writer *w, const struct ep_msg *e)
{
	enum gremlinp_parsing_error err;
	const char *sep = "";
	F("    predicate %s{L1,L2}(%s *m) =\n      ", e->own, e->tname);
	if (e->nf == 0) F("\\true");
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		const char *fn = f->fname;
		switch (f->kind) {
		case EK_SCALAR:
		case EK_ENUM:
		case EK_MSG:
			F("%s\\at(m->%s, L1) == \\at(m->%s, L2)", sep, fn, fn);
			break;
		case EK_BYTES:
			F("%s\\at(m->%s.len, L1) == \\at(m->%s.len, L2) &&"
			  "\n      \\at(m->%s.data, L1) == \\at(m->%s.data, L2)", sep, fn, fn, fn, fn);
			break;
		default:
			F("%s\\at(m->%s, L1) == \\at(m->%s, L2) &&"
			  " \\at(m->%s_count, L1) == \\at(m->%s_count, L2)",
			  sep, fn, fn, fn, fn);
			F(" &&\n      (\\forall integer k; 0 <= k < \\at(m->%s_count, L1) ==>\n         ", fn);
			if (f->kind == EK_REP_BYTES) {
				F("\\at(m->%s[k].len, L1) == \\at(m->%s[k].len, L2) &&"
				  " \\at(m->%s[k].data, L1) == \\at(m->%s[k].data, L2))", fn, fn, fn, fn);
			} else {
				F("\\at(m->%s[k], L1) == \\at(m->%s[k], L2))", fn, fn);
			}
			break;
		}
		sep = " &&\n      ";
	}
	F(";\n\n");
	return GREMLINP_OK;
}

/* Each clause of e's valid on node X, joined into the predicate. */
static enum gremlinp_parsing_error
valid_clauses(struct gremlinc_writer *w, const struct gremlinc_encode_plan *p,
	      const struct ep_msg *e, int idx, const char *X)
{
	enum gremlinp_parsing_error err;
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		const char *fn = f->fname;
		if (is_rep(f->kind)) {
			F(" &&\n      ");
			F("(%s->%s_count == 0 || \\valid_read(%s->%s + (0 .. %s->%s_count - 1)))",
			  X, fn, X, fn, X, fn);
		}
		if (f->kind == EK_BYTES) {
			F(" &&\n      ");
			F("(%s->%s.len == 0 || \\valid_read(%s->%s.data + (0 .. %s->%s.len - 1)))",
			  X, fn, X, fn, X, fn);
		}
		if (f->kind == EK_REP_BYTES) {
			F(" &&\n      ");
			F("(\\forall integer k; 0 <= k < %s->%s_count ==>\n"
			  "         %s->%s[k].len == 0 || \\valid_read(%s->%s[k].data + (0 .. %s->%s[k].len - 1)))",
			  X, fn, X, fn, X, fn, X, fn);
		}
		if (is_array(f->kind)) {
			F(" &&\n      ");
			F("(%s->%s_count == 0 || \\separated(%s, %s->%s + (0 .. %s->%s_count - 1)))",
			  X, fn, X, X, fn, X, fn);
			for (size_t j = i + 1; j < e->nf; j++) {
				const struct ep_field *g = &e->f[j];
				if (!is_array(g->kind)) continue;
				F(" &&\n      ");
				F("(%s->%s_count == 0 || %s->%s_count == 0 ||\n"
				  "       \\separated(%s->%s + (0 .. %s->%s_count - 1), %s->%s + (0 .. %s->%s_count - 1)))",
				  X, fn, X, g->fname, X, fn, X, fn, X, g->fname, X, g->fname);
			}
		}
	}
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (!is_child(f->kind)) continue;
		const struct ep_msg *c = &p->msgs[f->child];
		const char *self = avoid_name(p, f->child, msg_target(idx));
		const char *rest;
		if (self == NULL) return GREMLINP_ERROR_NULL_POINTER;
		F(" &&\n      ");
		if ((err = each_child(w, X, f, c->tname, c->valid, ""))) return err;
		if ((rest = dup(p->arena, ", %s", X)) == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
		F(" &&\n      ");
		if ((err = each_child(w, X, f, c->tname, self, rest))) return err;
		for (size_t j = 0; j < e->nf; j++) {
			const struct ep_field *g = &e->f[j];
			if (!is_array(g->kind)) continue;
			const char *an = avoid_name(p, f->child, array_target(g));
			if (an == NULL) return GREMLINP_ERROR_NULL_POINTER;
			rest = dup(p->arena, ", %s->%s, %s->%s_count", X, g->fname, X, g->fname);
			if (rest == NULL) return GREMLINP_ERROR_OUT_OF_MEMORY;
			F(" &&\n      ");
			if ((err = each_child(w, X, f, c->tname, an, rest))) return err;
		}
	}
	/* Sibling subtrees, and distinct elements of one repeated field. */
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (!is_child(f->kind)) continue;
		const char *C = p->msgs[f->child].tname;
		for (size_t j = i; j < e->nf; j++) {
			const struct ep_field *g = &e->f[j];
			if (!is_child(g->kind)) continue;
			if (j == i && f->kind != EK_REP_MSG) continue;
			const char *D = p->msgs[g->child].tname;
			const char *nn = navoid_name(p, f->child, g->child);
			if (nn == NULL) return GREMLINP_ERROR_NULL_POINTER;
			const char *fn = f->fname, *gn = g->fname;
			F(" &&\n      ");
			if (j == i) {
				F("(\\forall integer i, j; 0 <= i < %s->%s_count && 0 <= j < %s->%s_count &&\n"
				  "         i != j && %s->%s[i] != \\null && %s->%s[j] != \\null ==>\n"
				  "         %s((%s *)%s->%s[i], (%s *)%s->%s[j]))",
				  X, fn, X, fn, X, fn, X, fn, nn, C, X, fn, D, X, fn);
			} else if (f->kind == EK_MSG && g->kind == EK_MSG) {
				F("(%s->%s != \\null && %s->%s != \\null ==>\n"
				  "         %s((%s *)%s->%s, (%s *)%s->%s))",
				  X, fn, X, gn, nn, C, X, fn, D, X, gn);
			} else if (f->kind == EK_MSG) {
				F("(\\forall integer j; 0 <= j < %s->%s_count &&\n"
				  "         %s->%s != \\null && %s->%s[j] != \\null ==>\n"
				  "         %s((%s *)%s->%s, (%s *)%s->%s[j]))",
				  X, gn, X, fn, X, gn, nn, C, X, fn, D, X, gn);
			} else if (g->kind == EK_MSG) {
				F("(\\forall integer i; 0 <= i < %s->%s_count &&\n"
				  "         %s->%s[i] != \\null && %s->%s != \\null ==>\n"
				  "         %s((%s *)%s->%s[i], (%s *)%s->%s))",
				  X, fn, X, fn, X, gn, nn, C, X, fn, D, X, gn);
			} else {
				F("(\\forall integer i, j; 0 <= i < %s->%s_count && 0 <= j < %s->%s_count &&\n"
				  "         %s->%s[i] != \\null && %s->%s[j] != \\null ==>\n"
				  "         %s((%s *)%s->%s[i], (%s *)%s->%s[j]))",
				  X, fn, X, gn, X, fn, X, gn, nn, C, X, fn, D, X, gn);
			}
		}
	}
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_valid(struct gremlinc_writer *w, struct gremlinc_espec *sp)
{
	enum gremlinp_parsing_error err;
	F("    predicate %s{L}(%s *m) =\n      \\valid(m)", sp->e->valid, sp->e->tname);
	if ((err = valid_clauses(w, sp->plan, sp->e, sp->idx, "m"))) return err;
	F(";\n\n");
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_bufsep(struct gremlinc_writer *w, struct gremlinc_espec *sp)
{
	enum gremlinp_parsing_error err;
	const struct ep_msg *e = sp->e;
	F("    predicate %s{L}(%s *m, uint8_t *b, integer lo, integer hi) =\n"
	  "      hi < lo || (\\separated(b + (lo .. hi), m)", e->bufsep, e->tname);
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (is_rep(f->kind)) {
			F(" &&\n      (m->%s_count == 0 || \\separated(b + (lo .. hi), m->%s + (0 .. m->%s_count - 1)))",
			  f->fname, f->fname, f->fname);
		}
		/* memcpy from the payload into b needs the two apart. */
		if (f->kind == EK_BYTES) {
			F(" &&\n      (m->%s.len == 0 || \\separated(b + (lo .. hi), m->%s.data + (0 .. m->%s.len - 1)))",
			  f->fname, f->fname, f->fname);
		}
		if (f->kind == EK_REP_BYTES) {
			F(" &&\n      (\\forall integer i; 0 <= i < m->%s_count ==> m->%s[i].len == 0 ||\n"
			  "         \\separated(b + (lo .. hi), m->%s[i].data + (0 .. m->%s[i].len - 1)))",
			  f->fname, f->fname, f->fname, f->fname);
		}
		if (is_child(f->kind)) {
			const struct ep_msg *c = child_of(sp, f);
			F(" &&\n      ");
			if ((err = each_child(w, "m", f, c->tname, c->bufsep, ", b, lo, hi"))) return err;
		}
	}
	F(");\n\n");
	return GREMLINP_OK;
}

static enum gremlinp_parsing_error
emit_ghosts(struct gremlinc_writer *w, struct gremlinc_espec *sp)
{
	enum gremlinp_parsing_error err;
	const struct ep_msg *e = sp->e;
	const char *M = e->tname;

	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		const char *fn = f->fname;
		if (f->kind == EK_REP_MSG) {
			F("/*@ ghost\n"
			  "  /@ assigns \\nothing;\n"
			  "     ensures %s((%s *)\\null) == %u;\n"
			  "  @/\n"
			  "  void %s(void) { }\n*/\n\n",
			  f->term, child_of(sp, f)->tname, f->tagb + 1, f->tnull);
		}
		if (!has_sum(f->kind)) continue;
		F("/*@ ghost\n"
		  "  /@ requires %s(m);\n"
		  "     assigns \\nothing;\n"
		  "     ensures \\forall integer j; 0 <= j <= m->%s_count ==>\n"
		  "               0 <= %s(m, j) <= %s(m, m->%s_count);\n"
		  "  @/\n"
		  "  void %s(%s *m)\n"
		  "  {\n"
		  "    size_t i = 0;\n"
		  "    /@ loop invariant 0 <= i <= m->%s_count;\n"
		  "       loop invariant \\forall integer j; 0 <= j <= i ==> 0 <= %s(m, j) <= %s(m, i);\n"
		  "       loop assigns i;\n"
		  "       loop variant m->%s_count - i;\n"
		  "    @/\n"
		  "    while (i < m->%s_count) {\n",
		  e->valid, fn, f->sum, f->sum, fn, f->mono, M, fn, f->sum, f->sum, fn, fn);
		if (f->kind == EK_REP_MSG) {
			const struct ep_msg *c = child_of(sp, f);
			F("      if (m->%s[i]) %s((%s *)m->%s[i]);\n", fn, c->pos, c->tname, fn);
		}
		F("      i++;\n"
		  "    }\n"
		  "  }\n*/\n\n");
	}

	F("/*@ ghost\n"
	  "  /@ requires %s(m);\n"
	  "     assigns \\nothing;\n", e->valid);
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (f->kind == EK_MSG) {
			const struct ep_msg *c = child_of(sp, f);
			F("     ensures m->%s != \\null ==> %s((%s *)m->%s) >= 0;\n",
			  f->fname, c->tsize, c->tname, f->fname);
		}
		if (has_sum(f->kind)) {
			F("     ensures \\forall integer j; 0 <= j <= m->%s_count ==>\n"
			  "               0 <= %s(m, j) <= %s(m, m->%s_count);\n",
			  f->fname, f->sum, f->sum, f->fname);
		}
	}
	F("     ensures %s(m) >= 0;\n"
	  "  @/\n"
	  "  void %s(%s *m)\n"
	  "  {\n", e->tsize, e->nonneg, M);
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (f->kind == EK_MSG) {
			const struct ep_msg *c = child_of(sp, f);
			F("    if (m->%s) %s((%s *)m->%s);\n", f->fname, c->nonneg, c->tname, f->fname);
		}
		if (has_sum(f->kind)) F("    %s(m);\n", f->mono);
	}
	F("  }\n*/\n\n");
	if (e->elem) {
		F("/*@ ghost\n"
		  "  /@ requires %s(m);\n"
		  "     assigns \\nothing;\n"
		  "     ensures %s(m) >= 0;\n"
		  "  @/\n"
		  "  void %s(%s *m)\n"
		  "  {\n"
		  "    %s(m);\n"
		  "  }\n*/\n\n", e->valid, e->tsize, e->pos, M, e->nonneg);
	}
	return GREMLINP_OK;
}

enum gremlinp_parsing_error
gremlinc_espec_emit_logic(struct gremlinc_writer *w, struct gremlinc_name_scope *scope,
			  struct gremlinc_espec *sp)
{
	enum gremlinp_parsing_error err;
	if (sp == NULL) return GREMLINP_OK;
	struct ep_msg *e = sp->e;
	const char *M = e->tname;

	if ((err = emit_pairs(w, scope, sp->plan, sp->idx))) return err;
	for (size_t i = 0; i < e->nf; i++) {
		if ((err = emit_field_logic(w, sp, &e->f[i]))) return err;
	}

	F("/*@\n");
	for (size_t i = 0; i < e->nf; i++) {
		if ((err = emit_field_size(w, sp, &e->f[i]))) return err;
	}
	F("    logic integer %s{L}(%s *m) =\n      ", e->tsize, M);
	if (e->nf == 0) F("0");
	for (size_t i = 0; i < e->nf; i++) {
		F("%s%s(m)", i ? " +\n      " : "", e->f[i].t);
	}
	F(";\n\n");

	if ((err = emit_own(w, e))) return err;

	F("    predicate %s{L1,L2}(%s *m) =\n      %s{L1,L2}(m)", e->dsame, M, e->own);
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (!is_child(f->kind)) continue;
		F(" &&\n      ");
		if ((err = each_child2(w, f, child_of(sp, f)->tname, child_of(sp, f)->dsame))) return err;
	}
	F(";\n\n");

	F("    predicate %s{L1,L2}(%s *m) =\n      \\at(m->_size, L1) == \\at(m->_size, L2)", e->csame, M);
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (!is_child(f->kind)) continue;
		F(" &&\n      ");
		if ((err = each_child2(w, f, child_of(sp, f)->tname, child_of(sp, f)->csame))) return err;
	}
	F(";\n\n");

	F("    predicate %s{L}(%s *m) =\n      m->_size == %s(m)", e->sized, M, e->tsize);
	for (size_t i = 0; i < e->nf; i++) {
		const struct ep_field *f = &e->f[i];
		if (!is_child(f->kind)) continue;
		F(" &&\n      ");
		if ((err = each_child(w, "m", f, child_of(sp, f)->tname, child_of(sp, f)->sized, ""))) return err;
	}
	F(";\n\n");

	if ((err = emit_valid(w, sp))) return err;
	if ((err = emit_bufsep(w, sp))) return err;

	for (size_t i = 0; i < e->nf; i++)
		F("    lemma %s{L1,L2}: \\forall %s *m;\n"
		  "      %s{L1,L2}(m) ==> \\at(%s(m), L1) == \\at(%s(m), L2);\n\n",
		  e->f[i].tf, M, e->dsame, e->f[i].t, e->f[i].t);
	F("    lemma %s{L1,L2}: \\forall %s *m;\n"
	  "      %s{L1,L2}(m) ==> \\at(%s(m), L1) == \\at(%s(m), L2);\n\n",
	  e->tsize_frame, M, e->dsame, e->tsize, e->tsize);
	F("    lemma %s{L1,L2}: \\forall %s *m;\n"
	  "      %s{L1,L2}(m) ==> %s{L1,L2}(m) ==> %s{L1}(m) ==> %s{L2}(m);\n\n",
	  e->sized_frame, M, e->dsame, e->csame, e->sized, e->sized);
	F("    lemma %s{L1,L2,L3}: \\forall %s *m;\n"
	  "      %s{L1,L2}(m) ==> %s{L2,L3}(m) ==> %s{L1,L3}(m);\n\n",
	  e->dsame_trans, M, e->dsame, e->dsame, e->dsame);
	F("    lemma %s{L}: \\forall %s *m, uint8_t *b, integer lo, hi, lo2, hi2;\n"
	  "      lo <= lo2 ==> hi2 <= hi ==> %s(m, b, lo, hi) ==> %s(m, b, lo2, hi2);\n*/\n\n",
	  e->bufsep_sub, M, e->bufsep, e->bufsep);

	return emit_ghosts(w, sp);
}

/* ------------------------------------------------------------------------
 * Contracts and code annotations
 * ------------------------------------------------------------------------ */

enum gremlinp_parsing_error
gremlinc_espec_cached_size_contract(struct gremlinc_writer *w, const struct gremlinc_espec *sp)
{
	enum gremlinp_parsing_error err;
	if (sp == NULL) return GREMLINP_OK;
	F("/*@ requires \\valid_read(m);\n"
	  "    assigns \\nothing;\n"
	  "    ensures \\result == m->_size;\n*/\n");
	return GREMLINP_OK;
}
