/* Callers of the generated writers, proved with them: a precondition no
 * caller can meet shows up here as an unprovable call. */

static const uint8_t caller_payload[3] = { 1, 2, 3 };
static const uint8_t caller_text[5] = { 'h', 'e', 'l', 'l', 'o' };

/*@ assigns \nothing;
    ensures \result == 8;
*/
size_t
caller_size_leaf(void)
{
	wpw_Leaf m = { 300, { caller_payload, 3 }, 0 };

	return wpw_Leaf_size(&m);
}

/*@ assigns \nothing;
    ensures \result == 15;
*/
size_t
caller_size_list(void)
{
	wpw_Leaf a = { 1, { caller_payload, 3 }, 0 };
	wpw_Leaf b = { 2, { NULL, 0 }, 0 };
	const wpw_Leaf *items[2] = { &a, &b };
	wpw_List l = { 7, items, 2, 0 };

	return wpw_List_size(&l);
}

/*@ assigns \nothing;
    ensures \result == 12;
*/
size_t
caller_size_strings(void)
{
	struct gremlin_bytes g[2] = { { caller_text, 5 }, { caller_payload, 3 } };
	wpw_Packs m = { .g = g, .g_count = 2 };

	return wpw_Packs_size(&m);
}

/*@ assigns \nothing;
    ensures \result == 56;
*/
size_t
caller_size_top(void)
{
	wpw_Leaf l1 = { 1, { caller_payload, 3 }, 0 };
	wpw_Leaf l2 = { 2, { NULL, 0 }, 0 };
	wpw_Kind kinds[2] = { wpw_Kind_KIND_ONE, wpw_Kind_KIND_NONE };
	uint64_t stamps[1] = { 9 };
	wpw_Mid mid = { &l1, wpw_Kind_KIND_ONE, kinds, 2, stamps, 1, 0 };
	struct gremlin_bytes ch[2] = { { caller_text, 5 }, { caller_payload, 3 } };
	uint64_t vals[2] = { 1, 300 };
	wpw_Top t = { 7, { caller_text, 5 }, &mid, &l2, ch, 2, vals, 2, 0 };

	return wpw_Top_size(&t);
}

/*@ assigns \nothing;
    ensures \result == 18;
*/
size_t
caller_size_pairs(void)
{
	uint64_t v1[2] = { 5, 600 };
	wpw_Pair p1 = { 1, v1, 2, { caller_payload, 3 }, 0 };
	wpw_Pair p2 = { 2, NULL, 0, { NULL, 0 }, 0 };
	const wpw_Pair *ps[2] = { &p1, &p2 };
	wpw_Pairs m = { ps, 2, 0 };

	return wpw_Pairs_size(&m);
}

/*@ assigns \nothing; */
uint8_t
caller_encode_leaf(void)
{
	wpw_Leaf m = { 300, { caller_payload, 3 }, 0 };
	uint8_t buf[8];

	wpw_Leaf_size(&m);
	size_t end = wpw_Leaf_encode_at(&m, buf, 0);
	return buf[end - 1];
}

/*@ assigns \nothing; */
uint8_t
caller_encode_list(void)
{
	wpw_Leaf a = { 1, { caller_payload, 3 }, 0 };
	wpw_Leaf b = { 2, { NULL, 0 }, 0 };
	const wpw_Leaf *items[2] = { &a, &b };
	wpw_List l = { 7, items, 2, 0 };
	uint8_t buf[15];

	wpw_List_size(&l);
	size_t end = wpw_List_encode_at(&l, buf, 0);
	return buf[end - 1];
}

/*@ assigns \nothing; */
uint8_t
caller_encode_strings(void)
{
	struct gremlin_bytes g[2] = { { caller_text, 5 }, { caller_payload, 3 } };
	wpw_Packs m = { .g = g, .g_count = 2 };
	uint8_t buf[12];

	wpw_Packs_size(&m);
	size_t end = wpw_Packs_encode_at(&m, buf, 0);
	return buf[end - 1];
}

/*@ assigns \nothing; */
uint8_t
caller_encode_top(void)
{
	wpw_Leaf l1 = { 1, { caller_payload, 3 }, 0 };
	wpw_Leaf l2 = { 2, { NULL, 0 }, 0 };
	wpw_Kind kinds[2] = { wpw_Kind_KIND_ONE, wpw_Kind_KIND_NONE };
	uint64_t stamps[1] = { 9 };
	wpw_Mid mid = { &l1, wpw_Kind_KIND_ONE, kinds, 2, stamps, 1, 0 };
	struct gremlin_bytes ch[2] = { { caller_text, 5 }, { caller_payload, 3 } };
	uint64_t vals[2] = { 1, 300 };
	wpw_Top t = { 7, { caller_text, 5 }, &mid, &l2, ch, 2, vals, 2, 0 };
	uint8_t buf[56];

	wpw_Top_size(&t);
	size_t end = wpw_Top_encode_at(&t, buf, 0);
	return buf[end - 1];
}

/*@ assigns \nothing; */
uint8_t
caller_encode_pairs(void)
{
	uint64_t v1[2] = { 5, 600 };
	wpw_Pair p1 = { 1, v1, 2, { caller_payload, 3 }, 0 };
	wpw_Pair p2 = { 2, NULL, 0, { NULL, 0 }, 0 };
	const wpw_Pair *ps[2] = { &p1, &p2 };
	wpw_Pairs m = { ps, 2, 0 };
	uint8_t buf[18];

	wpw_Pairs_size(&m);
	size_t end = wpw_Pairs_encode_at(&m, buf, 0);
	return buf[end - 1];
}

/*@ assigns \nothing; */
size_t
caller_encode_writer(void)
{
	wpw_Leaf m = { 300, { caller_payload, 3 }, 0 };
	uint8_t buf[8];
	struct gremlin_writer w;

	gremlin_writer_init(&w, buf, sizeof buf);
	wpw_Leaf_size(&m);
	wpw_Leaf_encode(&m, &w);
	return w.offset;
}
