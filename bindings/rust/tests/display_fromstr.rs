//! Display prints the pattern; FromStr is Regex::new. The FAIL: println!("{}", re)
//! and "\\d+".parse() did not compile. bytes::Regex carries the same pair.
use real_regex::Regex;

#[test]
fn display_prints_the_pattern() {
    let re = Regex::new(r"(\w+)@(\w+)").unwrap();
    assert_eq!(format!("{re}"), r"(\w+)@(\w+)");
    assert_eq!(re.to_string(), re.as_str());
}

#[test]
fn parse_compiles() {
    let re: Regex = r"\d+".parse().unwrap();
    assert!(re.is_match("x42"));
    assert!("(".parse::<Regex>().unwrap_err().to_string().contains("syntax"));
}

#[test]
fn bytes_display_and_parse() {
    let re: real_regex::bytes::Regex = r"\d+".parse().unwrap();
    assert_eq!(format!("{re}"), r"\d+");
    assert!(re.is_match(b"x42"));
    assert!("(".parse::<real_regex::bytes::Regex>().is_err());
}
