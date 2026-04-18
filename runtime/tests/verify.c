/*
 * verify.c — compilation unit for Frama-C WP.
 *
 * Static-inline functions with no caller are elided by the compiler, so
 * each runtime function needs at least one reference for WP to find it.
 * Nothing below actually runs — the `volatile` sinks just keep the
 * compiler from optimising the calls away.
 */
#include "gremlin.h"

int
main(void)
{
	uint8_t buf[16] = {0};
	struct gremlin_writer w;

	gremlin_writer_init(&w, buf, sizeof buf);

	/* size helpers */
	volatile size_t z = gremlin_varint_size(0)
	                  + gremlin_fixed32_size()
	                  + gremlin_fixed64_size()
	                  + gremlin_tag_size(1, GREMLIN_WIRE_VARINT);

	/* encoders */
	gremlin_varint_encode(&w, 0);
	gremlin_fixed32_encode(&w, 0);
	gremlin_fixed64_encode(&w, 0);
	gremlin_tag_encode(&w, 1, GREMLIN_WIRE_VARINT);

	/* decoders */
	volatile uint64_t d  = gremlin_varint_decode(buf, sizeof buf).value;
	volatile uint32_t d4 = gremlin_fixed32_decode(buf, sizeof buf).value;
	volatile uint64_t d8 = gremlin_fixed64_decode(buf, sizeof buf).value;
	volatile uint32_t dt = gremlin_tag_decode(buf, sizeof buf).tag.field_num;

	/* zigzag */
	volatile uint32_t zz32 = gremlin_zigzag32(0);
	volatile int32_t  uz32 = gremlin_unzigzag32(0);
	volatile uint64_t zz64 = gremlin_zigzag64(0);
	volatile int64_t  uz64 = gremlin_unzigzag64(0);

	/* float / double bit-cast */
	volatile uint32_t fb32 = gremlin_f32_bits(0.0f);
	volatile float    fv32 = gremlin_f32_from_bits(0u);
	volatile uint64_t fb64 = gremlin_f64_bits(0.0);
	volatile double   fv64 = gremlin_f64_from_bits(0ull);

	(void)z;  (void)d;  (void)d4; (void)d8; (void)dt;
	(void)zz32; (void)uz32; (void)zz64; (void)uz64;
	(void)fb32; (void)fv32; (void)fb64; (void)fv64;
	return 0;
}
