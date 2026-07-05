# real-regex

Linear-time, ReDoS-safe regular expressions with **bounded lookarounds** — Rust bindings to the
[REAL](https://github.com/RECHE23/real-regex) C++ engine.

Every pattern that compiles matches in time linear in the input. There is no backtracking, so no
catastrophic blow-up: the pathological `(a+)+b` that hangs a backtracking engine runs in microseconds here.
The engine is **strict by design** — a construct it cannot run linearly (a backreference, an unbounded
lookaround) is rejected at compile time, never silently made non-linear.

```rust
use real_regex::Regex;

let re = Regex::new(r"(\w+)@(\w+)").unwrap();
for m in re.find_iter("a@b cd@ef") {
    println!("{:?} / {:?}", m.get(1), m.get(2));
}
```

Unlike RE2 and the `regex` crate, REAL supports bounded lookahead and lookbehind — in linear time. The
engine and this crate share one calendar version.

## License

MIT.
