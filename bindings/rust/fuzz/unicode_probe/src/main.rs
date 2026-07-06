//! Reproducible audit: for `\w`, `\s`, `\d`, dump the code points where REAL (Python `re` semantics — its
//! contract) and the `regex` crate (UTS#18) disagree, counted by Unicode general category. Both sides are asked
//! of the actual engines, so no category table is hardcoded and the result self-updates with the crates'
//! Unicode version. This is the committed, category-annotated form of the differential fuzzer's word/space
//! delta mask; re-run it (`cargo run --release`) after a `regex`/`real-regex` Unicode bump and update the
//! README "Divergences" table if the counts move.
use std::collections::BTreeMap;

fn main() {
    // General categories a delta can fall into (letters/other are listed too, so a surprise category shows up
    // as itself rather than "??").
    let cat_names = [
        "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps", "Pe", "Pi", "Pf",
        "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn",
    ];
    let cats: Vec<(&str, regex::Regex)> = cat_names
        .iter()
        .filter_map(|n| regex::Regex::new(&format!(r"^\p{{{n}}}$")).ok().map(|re| (*n, re))) // skip any the crate declines (e.g. Cs)
        .collect();
    let category = |s: &str| -> &str { cats.iter().find(|(_, re)| re.is_match(s)).map(|(n, _)| *n).unwrap_or("??") };

    for (name, pat) in [("\\w", r"^\w$"), ("\\s", r"^\s$"), ("\\d", r"^\d$")] {
        let rust = regex::Regex::new(pat).unwrap();
        let real = real_regex::Regex::new(pat).unwrap();
        let mut rust_only: BTreeMap<&str, u32> = BTreeMap::new(); // regex matches, REAL does not (UTS#18 superset)
        let mut real_only: BTreeMap<&str, u32> = BTreeMap::new(); // REAL matches, regex does not (CPython superset)
        let mut first_rust_only: Option<u32> = None;
        let mut first_real_only: Option<u32> = None;

        for cp in 0u32..=0x0010_FFFF {
            let Some(c) = char::from_u32(cp) else { continue };
            let s = c.to_string();
            let r = rust.is_match(&s);
            let o = real.is_match(&s);
            if r == o {
                continue;
            }
            let cat = category(&s);
            if r {
                *rust_only.entry(cat).or_default() += 1;
                first_rust_only.get_or_insert(cp);
            } else {
                *real_only.entry(cat).or_default() += 1;
                first_real_only.get_or_insert(cp);
            }
        }

        let total = |m: &BTreeMap<&str, u32>| -> u32 { m.values().sum() };
        println!("{name}:");
        println!(
            "  regex-only (UTS#18 ⊃), {} pts, first U+{:04X}: {:?}",
            total(&rust_only),
            first_rust_only.unwrap_or(0),
            rust_only
        );
        println!(
            "  REAL-only  (CPython ⊃), {} pts, first U+{:04X}: {:?}",
            total(&real_only),
            first_real_only.unwrap_or(0),
            real_only
        );
    }
}
