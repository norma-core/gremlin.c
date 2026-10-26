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
