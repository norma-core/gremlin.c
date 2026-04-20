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
	                  + gremlin_fixed64_size();

	/* encoders */
	gremlin_varint_encode(&w, 0);
	gremlin_fixed32_encode(&w, 0);
	gremlin_fixed64_encode(&w, 0);

	/* decoders */
	volatile uint64_t d  = gremlin_varint_decode(buf, sizeof buf).value;
	volatile uint32_t d4 = gremlin_fixed32_decode(buf, sizeof buf).value;
	volatile uint64_t d8 = gremlin_fixed64_decode(buf, sizeof buf).value;
	volatile size_t   ds = gremlin_skip_data(buf, sizeof buf, GREMLIN_WIRE_VARINT).consumed;

	/* 32-bit varint variants */
	volatile size_t   vz32 = gremlin_varint32_size(0u);
	gremlin_varint32_encode(&w, 0u);
	volatile uint32_t vd32 = gremlin_varint32_decode(buf, sizeof buf).value;

	/* bytes encode / decode */
	gremlin_write_bytes(&w, buf, 0);
	volatile size_t   bc = gremlin_bytes_decode(buf, sizeof buf).consumed;

	/* _at variants — keep WP seeing them so their bodies are verified. */
	volatile size_t ao1 = gremlin_varint_encode_at(buf, 0, 0);
	volatile size_t ao2 = gremlin_varint32_encode_at(buf, 0, 0u);
	volatile size_t ao3 = gremlin_fixed32_encode_at(buf, 0, 0u);
	volatile size_t ao4 = gremlin_fixed64_encode_at(buf, 0, 0ull);
	volatile size_t ao5 = gremlin_write_bytes_at(buf, 0, buf, 0);
	(void)ao1; (void)ao2; (void)ao3; (void)ao4; (void)ao5;

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

	(void)z;  (void)d;  (void)d4; (void)d8; (void)ds;
	(void)vz32; (void)vd32; (void)bc;
	(void)zz32; (void)uz32; (void)zz64; (void)uz64;
	(void)fb32; (void)fv32; (void)fb64; (void)fv64;
	return 0;
}
