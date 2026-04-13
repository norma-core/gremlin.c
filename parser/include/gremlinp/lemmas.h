#ifndef _GREMLINP_LEMMAS_H_
#define _GREMLINP_LEMMAS_H_

#include "axioms.h"

/*
 * Provable facts about the recursive logic functions and predicates
 * declared in axioms.h. Anything stated here is an ACSL `lemma`, which
 * Frama-C / WP must DISCHARGE — unlike `axiom`, which is admitted
 * without proof.
 *
 * Why a separate header: anything in axioms.h is part of the trust
 * base. Lemmas live here so an audit can look at axioms.h alone to see
 * exactly what is admitted, and at lemmas.h to see what is *claimed but
 * checked*. A failed lemma is a verification failure, not an audit
 * comment.
 *
 * Conventions:
 *   - Lemmas WP can discharge with Alt-Ergo / Z3 go here.
 *   - Properties of recursive logic functions whose proof needs
 *     induction stay in axioms.h as admitted axioms; the SMT backends
 *     cannot do induction.
 *   - Facts about specific static const C arrays (KW_*) must NOT be
 *     written as value-reading axioms or asserts: doing so poisons WP's
 *     typed memory model. Express such facts via explicit-length
 *     parameters at call sites, or via `\valid_read_string`.
 */

#endif /* !_GREMLINP_LEMMAS_H_ */
