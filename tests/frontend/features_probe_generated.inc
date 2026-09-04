// GENERATED from docs/site/data/features.yaml — DO NOT EDIT — regenerate: python3 tools/gen_features.py
//
// One TEST() per features.yaml row with a non-null `pattern`: supported/extension rows
// assert real::regex(pattern) compiles without throwing; excluded-by-design rows assert it
// throws real::regex_error (the moat, proven executable). `planned` rows and rows with
// `pattern: null` are skipped — see features.yaml's own header for why no pattern is
// fabricated to fill those in. #include this fragment from test_features_probe.cpp only.

// --- category 0: Core ---

TEST(features_probe_00_00_core_literals_concatenation_alternation)
{
  // Core / Literals, concatenation & alternation — status: supported (must compile)
  const real::regex re(R"RRX(abc|xyz)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(abc|xyz)RRX"));
}

TEST(features_probe_00_01_core_any_character_ecmascript_semantics)
{
  // Core / `.` any character (ECMAScript semantics) — status: supported (must compile)
  const real::regex re(R"RRX(a.b)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(a.b)RRX"));
}

TEST(features_probe_00_02_core_character_classes_ranges_a_z)
{
  // Core / Character classes & ranges `[a-z]` — status: supported (must compile)
  const real::regex re(R"RRX([a-z0-9_]+)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX([a-z0-9_]+)RRX"));
}

TEST(features_probe_00_03_core_d_w_s_shorthand_classes_negations)
{
  // Core / `\d \w \s` shorthand classes (+ negations) — status: supported (must compile)
  const real::regex re(R"RRX(\d+\w*\s?)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(\d+\w*\s?)RRX"));
}

TEST(features_probe_00_04_core_anchors_b_b)
{
  // Core / Anchors `^ $ \b \B` — status: supported (must compile)
  const real::regex re(R"RRX(^\bfoo\b$)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(^\bfoo\b$)RRX"));
}

TEST(features_probe_00_05_core_quantifiers_m_n_greedy_lazy)
{
  // Core / Quantifiers `* + ? {m,n}` (greedy/lazy) — status: supported (must compile)
  const real::regex re(R"RRX(a+?b*)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(a+?b*)RRX"));
}

TEST(features_probe_00_06_core_groups_capturing_non_capturing_named)
{
  // Core / Groups (capturing, non-capturing, named) — status: supported (must compile)
  const real::regex re(R"RRX((?P<x>a)(?:b))RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?P<x>a)(?:b))RRX"));
}

TEST(features_probe_00_07_core_lookahead_lookbehind_bounded)
{
  // Core / Lookahead & lookbehind (bounded) — status: supported (must compile)
  const real::regex re(R"RRX((?<=a)b(?=c))RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?<=a)b(?=c))RRX"));
}

TEST(features_probe_00_08_core_possessive_quantifiers_a_a_a_a_n_m)
{
  // Core / Possessive quantifiers `a*+ a++ a?+ a{n,m}+` — status: supported (must compile)
  const real::regex re(R"RRX(a++b)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(a++b)RRX"));
}

TEST(features_probe_00_09_core_atomic_groups)
{
  // Core / Atomic groups `(?>...)` — status: supported (must compile)
  const real::regex re(R"RRX((?>ab)c)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?>ab)c)RRX"));
}

TEST(features_probe_00_10_core_ascii_case_insensitive_matching_icase)
{
  // Core / ASCII case-insensitive matching (`icase`) — status: supported (must compile)
  const real::regex re(R"RRX((?i)ABC)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?i)ABC)RRX"));
}

TEST(features_probe_00_11_core_multiline_mode_per_line)
{
  // Core / Multiline mode (`^`/`$` per line) — status: supported (must compile)
  const real::regex re(R"RRX((?m)^a$)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?m)^a$)RRX"));
}

// --- category 1: Unicode ---

TEST(features_probe_01_00_unicode_p_property_classes)
{
  // Unicode / `\p{…}` property classes — status: supported (must compile)
  const real::regex re(R"RRX(\p{L}+)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(\p{L}+)RRX"));
}
// skipped (pattern: null): `\N{NAME}` named characters

TEST(features_probe_01_02_unicode_n_u_xxxx_scalar_form)
{
  // Unicode / `\N{U+XXXX}` scalar form — status: extension (must compile)
  const real::regex re(R"RRX(\N{U+1F600})RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(\N{U+1F600})RRX"));
}

TEST(features_probe_01_03_unicode_u_braced_hex_escape)
{
  // Unicode / `\u{…}` braced hex escape — status: extension (must compile)
  const real::regex re(R"RRX(\u{1F600})RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(\u{1F600})RRX"));
}

// --- category 2: Anchors & lookaround extensions ---

TEST(features_probe_02_00_anchors_lookaround_extensions_z_end_of_text_anchor)
{
  // Anchors & lookaround extensions / `\z` end-of-text anchor — status: supported (must compile)
  const real::regex re(R"RRX(abc\z)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(abc\z)RRX"));
}

TEST(features_probe_02_01_anchors_lookaround_extensions_variable_width_lookbehind_a_bb)
{
  // Anchors & lookaround extensions / Variable-width lookbehind `(?<=a|bb)` — status: extension (must compile)
  const real::regex re(R"RRX((?<=a|bb)x)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?<=a|bb)x)RRX"));
}

TEST(features_probe_02_02_anchors_lookaround_extensions_n_with_nothing_to_repeat_is_literal)
{
  // Anchors & lookaround extensions / `{n}` with nothing to repeat is literal — status: extension (must compile)
  const real::regex re(R"RRX({2}a)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX({2}a)RRX"));
}

TEST(features_probe_02_03_anchors_lookaround_extensions_named_group_net_spelling_name)
{
  // Anchors & lookaround extensions / Named group, .NET spelling `(?<name>…)` — status: extension (must compile)
  const real::regex re(R"RRX((?<n>a))RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?<n>a))RRX"));
}

TEST(features_probe_02_04_anchors_lookaround_extensions_word_edge_anchors)
{
  // Anchors & lookaround extensions / Word-edge anchors `\< \>` — status: extension (must compile)
  const real::regex re(R"RRX(\<word\>)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX(\<word\>)RRX"));
}

// --- category 3: Escapes & classes ---

TEST(features_probe_03_00_escapes_classes_octal_escapes_in_a_class_1_12)
{
  // Escapes & classes / Octal escapes in a class `[\1]` `[\12]` — status: supported (must compile)
  const real::regex re(R"RRX([\12])RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX([\12])RRX"));
}

TEST(features_probe_03_01_escapes_classes_posix_alpha_classes)
{
  // Escapes & classes / POSIX `[[:alpha:]]` classes — status: supported (must compile)
  const real::regex re(R"RRX([[:alpha:]]+)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX([[:alpha:]]+)RRX"));
}

// --- category 4: Flags ---

TEST(features_probe_04_00_flags_global_flags_i_m_s_x_a_and_imsxa_prefix)
{
  // Flags / Global flags `i m s x a` (and `(?imsxa)` prefix) — status: supported (must compile)
  const real::regex re(R"RRX((?imsxa)a.b)RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?imsxa)a.b)RRX"));
}

TEST(features_probe_04_01_flags_scoped_inline_flags_imsxa)
{
  // Flags / Scoped inline flags `(?imsxa:…)` / `(?-…:…)` / `(?…-…:…)` — status: supported (must compile)
  const real::regex re(R"RRX((?i:abc)(?-i:def))RRX");
  EXPECT_EQ(re.pattern(), std::string_view(R"RRX((?i:abc)(?-i:def))RRX"));
}

// --- category 5: Excluded by design ---

TEST(features_probe_05_00_excluded_by_design_backreferences_a_1_p_n)
{
  // Excluded by design / Backreferences `(a)\1`, `(?P=n)` — status: excluded-by-design (must throw real::regex_error)
  EXPECT_THROWS(real::regex(R"RRX((a)\1)RRX"), real::regex_error);
}

TEST(features_probe_05_01_excluded_by_design_conditional_groups_id)
{
  // Excluded by design / Conditional groups `(?(id)…)` — status: excluded-by-design (must throw real::regex_error)
  EXPECT_THROWS(real::regex(R"RRX((?(1)a|b))RRX"), real::regex_error);
}

TEST(features_probe_05_02_excluded_by_design_pattern_recursion_r_1_1)
{
  // Excluded by design / Pattern recursion `(?R)`, `(?1)`, `(?-1)` — status: excluded-by-design (must throw real::regex_error)
  EXPECT_THROWS(real::regex(R"RRX(a(?R)b)RRX"), real::regex_error);
}

TEST(features_probe_05_03_excluded_by_design_subroutine_calls_name_p_name)
{
  // Excluded by design / Subroutine calls `(?&name)`, `(?P>name)` — status: excluded-by-design (must throw real::regex_error)
  EXPECT_THROWS(real::regex(R"RRX((?P<n>a)(?&n))RRX"), real::regex_error);
}

TEST(features_probe_05_04_excluded_by_design_callouts_c_c1)
{
  // Excluded by design / Callouts `(?C)`, `(?C1)` — status: excluded-by-design (must throw real::regex_error)
  EXPECT_THROWS(real::regex(R"RRX(a(?C1)b)RRX"), real::regex_error);
}

// 24 compile-assertions, 5 throw-assertions, 1 skipped (planned / pattern: null).
