SHELL := /bin/bash
OPAM_ENV := eval $$(opam env)
FRAMA_C := frama-c
CPPFLAGS := -I include

WP_FLAGS := -wp -wp-rte -wp-timeout 15 -wp-prover alt-ergo,z3 -wp-par 16

BUFFER_WP_FCTS := gremlinp_parser_buffer_init,is_whitespace,gremlinp_parser_buffer_check_str_and_shift,gremlinp_parser_buffer_check_str_with_space_and_shift,gremlinp_parser_buffer_skip_spaces,gremlinp_parser_buffer_check_and_shift,gremlinp_parser_buffer_char,gremlinp_parser_buffer_should_shift_next,gremlinp_parser_buffer_semicolon,gremlinp_parser_buffer_assignment,gremlinp_parser_buffer_open_bracket,gremlinp_parser_buffer_close_bracket,gremlinp_parser_buffer_calc_line_number,gremlinp_parser_buffer_calc_line_start,gremlinp_parser_buffer_calc_line_end

BUFFER_SRC := src/entries/buffer.c

LEXEMS_WP_FCTS := is_octal_digit,is_hex_digit,is_identifier_char,gremlinp_lexems_is_valid_identifier,gremlinp_lexems_is_builtin_type,gremlinp_lexems_is_valid_map_key_type,parse_unicode_escape,parse_extended_unicode_escape,parse_hex_escape,parse_octal_escape,parse_escape_sequence,str_lit_single,parse_decimal_digits,parse_octal_or_hex,parse_exponent,gremlinp_lexems_parse_boolean_literal,gremlinp_lexems_parse_string_literal,gremlinp_lexems_parse_identifier,gremlinp_lexems_parse_full_identifier,gremlinp_lexems_parse_map_type,gremlinp_lexems_parse_const_value,gremlinp_lexems_parse_integer_literal,gremlinp_lexems_parse_uint_literal,gremlinp_lexems_parse_float_literal,gremlinp_lexems_parse_identifier,gremlinp_lexems_parse_field_number,gremlinp_lexems_parse_enum_value_number,gremlinp_lexems_parse_range

LEXEMS_SRC := src/entries/lexems.c src/entries/buffer.c
SYNTAX_WP_FCTS := gremlinp_syntax_parse,gremlinp_edition_parse,gremlinp_package_parse,gremlinp_parsing_error_to_string,gremlinp_option_parse,gremlinp_option_item_parse,gremlinp_option_list_consume,gremlinp_import_parse,gremlinp_extensions_parse,gremlinp_reserved_parse,gremlinp_service_parse,gremlinp_group_parse,gremlinp_enum_parse,gremlinp_enum_field_parse,gremlinp_enum_next_entry,gremlinp_enum_next_entry,gremlinp_field_type_parse,gremlinp_field_parse,gremlinp_oneof_field_parse,gremlinp_oneof_next_entry,gremlinp_oneof_parse,gremlinp_extend_parse,gremlinp_message_parse,gremlinp_message_next_entry,gremlinp_file_next_entry

SYNTAX_SRC := src/entries/syntax.c src/entries/edition.c src/entries/package.c src/entries/option.c src/entries/options.c src/entries/import.c src/entries/extensions.c src/entries/reserved.c src/entries/service.c src/entries/group.c src/entries/enum.c src/entries/field_type.c src/entries/field.c src/entries/oneof.c src/entries/extend.c src/entries/message.c src/entries/proto_file.c src/entries/errors.c src/entries/lexems.c src/entries/buffer.c
.PHONY: verify verify-wp-buffer verify-wp-lexems verify-wp-syntax

verify: verify-wp-buffer verify-wp-lexems verify-wp-syntax
	@echo "=== All verified ==="

verify-wp-buffer:
	@echo "=== WP: buffer ==="
	$(OPAM_ENV) && $(FRAMA_C) $(WP_FLAGS) -cpp-extra-args="$(CPPFLAGS)" \
		-wp-fct $(BUFFER_WP_FCTS) $(BUFFER_SRC)

verify-wp-lexems:
	@echo "=== WP: lexems ==="
	$(OPAM_ENV) && $(FRAMA_C) $(WP_FLAGS) -cpp-extra-args="$(CPPFLAGS)" \
		-wp-fct $(LEXEMS_WP_FCTS) $(LEXEMS_SRC)

verify-wp-syntax:
	@echo "=== WP: syntax ==="
	$(OPAM_ENV) && $(FRAMA_C) $(WP_FLAGS) -cpp-extra-args="$(CPPFLAGS)" \
		-wp-fct $(SYNTAX_WP_FCTS) $(SYNTAX_SRC)

# TODO: Eva for heap verification (_new, _from_file, _free)
# $(OPAM_ENV) && $(FRAMA_C) -eva -eva-precision 9 -eva-builtins-auto ...
