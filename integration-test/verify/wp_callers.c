/* Callers of the generated readers, proved with them: what a contract
 * leaves out shows up here as an unprovable read. */

/*@ requires \valid_read(r) && wp_All_reader_ok(r, r->src_len);
    requires r->src_len == 0 || \valid_read(r->src + (0 .. r->src_len - 1));
    assigns \nothing;
*/
uint8_t
caller_first_raw_byte(const wp_All_reader *r)
{
	struct gremlin_bytes b = wp_All_reader_get_raw(r);

	if (b.len > 0)
		return b.data[0];
	return 0;
}

/*@ requires \valid_read(r) && wd_Defaults_reader_ok(r, r->src_len);
    requires r->src_len == 0 || \valid_read(r->src + (0 .. r->src_len - 1));
    assigns \nothing;
*/
uint8_t
caller_default_last_byte(const wd_Defaults_reader *r)
{
	struct gremlin_bytes b = wd_Defaults_reader_get_b(r);

	if (b.len > 0)
		return b.data[b.len - 1];
	return 0;
}

/*@ requires \valid_read(r) && wp_All_reader_ok(r, r->src_len);
    requires r->src_len == 0 || \valid_read(r->src + (0 .. r->src_len - 1));
    assigns \nothing;
*/
uint8_t
caller_inner_first_byte(const wp_All_reader *r)
{
	wp_Inner_reader in;

	if (wp_All_reader_get_inner(r, &in) != GREMLIN_OK)
		return 0;
	struct gremlin_bytes s = wp_Inner_reader_get_s(&in);
	if (s.len > 0)
		return s.data[0];
	return 0;
}

/*@ requires \valid_read(r) && wp_All_reader_ok(r, r->src_len);
    requires r->src_len == 0 || \valid_read(r->src + (0 .. r->src_len - 1));
    assigns \nothing;
*/
uint32_t
caller_iterate_all(const wp_All_reader *r)
{
	uint32_t sum = 0;

	wp_All_reader_r_raw_iter ri = wp_All_reader_r_raw_begin(r);
	/*@ loop invariant wp_All_reader_r_raw_iter_ok(ri);
	    loop assigns ri, sum;
	    loop variant ri.count_remaining;
	*/
	while (ri.count_remaining > 0) {
		struct gremlin_bytes b;
		if (wp_All_reader_r_raw_next(&ri, &b) != GREMLIN_OK)
			break;
		if (b.len > 0)
			sum += b.data[0];
	}

	wp_All_reader_r_inner_iter ii = wp_All_reader_r_inner_begin(r);
	/*@ loop invariant wp_All_reader_r_inner_iter_ok(ii);
	    loop assigns ii, sum;
	    loop variant ii.count_remaining;
	*/
	while (ii.count_remaining > 0) {
		wp_Inner_reader in;
		if (wp_All_reader_r_inner_next(&ii, &in) != GREMLIN_OK)
			break;
		struct gremlin_bytes s = wp_Inner_reader_get_s(&in);
		if (s.len > 0)
			sum += s.data[0];
	}

	wp_All_reader_m_scalar_iter si = wp_All_reader_m_scalar_begin(r);
	/*@ loop invariant wp_All_reader_m_scalar_iter_ok(si);
	    loop assigns si, sum;
	    loop variant si.count_remaining;
	*/
	while (si.count_remaining > 0) {
		struct gremlin_bytes k;
		int32_t v;
		if (wp_All_reader_m_scalar_next(&si, &k, &v) != GREMLIN_OK)
			break;
		if (k.len > 0)
			sum += k.data[0];
	}

	wp_All_reader_m_msg_iter mi = wp_All_reader_m_msg_begin(r);
	/*@ loop invariant wp_All_reader_m_msg_iter_ok(mi);
	    loop assigns mi, sum;
	    loop variant mi.count_remaining;
	*/
	while (mi.count_remaining > 0) {
		int64_t k;
		wp_Inner_reader in;
		if (wp_All_reader_m_msg_next(&mi, &k, &in) != GREMLIN_OK)
			break;
		struct gremlin_bytes s = wp_Inner_reader_get_s(&in);
		if (s.len > 0)
			sum += s.data[0];
	}
	return sum;
}

static const uint8_t caller_table[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

/*@ assigns \nothing;
    ensures \result == 8;
*/
uint32_t
caller_absent_values(void)
{
	int32_t n = wd_Defaults_reader_get_n(NULL);
	size_t c = wp_All_reader_r_raw_count(NULL);

	return caller_table[n] + (uint32_t)c;
}
