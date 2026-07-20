// REAL — Python extension module (CPython Limited API, abi3).
//
// Exposes the C++ engine with an `re`-compatible surface:
//   _real.compile(pattern, flags) -> Pattern
//   Pattern.match/fullmatch/search/findall/finditer/split/sub/subn
//   Match.group/groups/groupdict/start/end/span/__getitem__
//
// Both str and bytes patterns are supported, never mixed (like re).
// Matching runs on UTF-8 bytes; REAL guarantees codepoint-aligned match
// boundaries, so group texts are decoded straight from byte slices, and
// byte offsets are converted to character indices only for start/end/span
// (free when the subject is pure ASCII: byte == character).

#define PY_SSIZE_T_CLEAN
#define Py_LIMITED_API 0x030A0000
#include <Python.h>

#include <real/real.hpp>
#include <real/regex_set.hpp>
#include <sciforge/binding/error.hpp>
#include <sciforge/binding/gil.hpp>

#include <algorithm>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Module state (single-phase init: simple globals)
// ---------------------------------------------------------------------------

PyObject* error_type = nullptr;    // real.error
PyObject* pattern_type = nullptr;  // real.Pattern
PyObject* match_type = nullptr;    // real.Match
PyObject* match_iterator_type = nullptr;  // real.MatchIterator (internal: created, not exposed)
PyObject* regex_set_type = nullptr;  // real._RegexSet (internal: wrapped by real.RegexSet)

// RAII GIL release (restores on every exit, including a throw): the shared
// sciforge::binding::gil_release, kept under the local name the call sites use.
using GilRelease = sciforge::binding::gil_release;

// Releasing the GIL costs a thread-state save/restore (plus re-acquire contention)
// that only pays off once the pure-C++ work outlasts it; subject size is the a-priori
// proxy for that work, so below a threshold the GIL is simply kept. Two thresholds,
// because the two paths have different serial tails (measured — see BENCHMARKS.md):
//
//  - Single-shot match/fullmatch/search returns ONE match, so essentially all the
//    work is the parallelisable scan. 512 B is where the scan first outlasts the
//    toggle.
//  - findall/split then BUILD O(matches) Python objects under the GIL. That serial
//    tail both caps scaling (~2x for fast-scanning patterns) and, on small
//    match-dense subjects, makes the frequent toggling *regress* multi-thread
//    throughput (the per-call toggle/contention dwarfs a sub-millisecond call). The
//    no-regression point sits near 2 KB for the fastest-scanning patterns; 4 KB keeps
//    a margin and a gain across thread counts.
constexpr Py_ssize_t gil_release_min_bytes         = 512;   //!< single-shot scan (run_single)
constexpr Py_ssize_t gil_release_collect_min_bytes = 4096;  //!< findall/split collect-spans phase

// Python re flag values.
constexpr unsigned long PYFLAG_IGNORECASE = 2;
constexpr unsigned long PYFLAG_LOCALE = 4;
constexpr unsigned long PYFLAG_MULTILINE = 8;
constexpr unsigned long PYFLAG_DOTALL = 16;
constexpr unsigned long PYFLAG_UNICODE = 32;
constexpr unsigned long PYFLAG_VERBOSE = 64;
constexpr unsigned long PYFLAG_DEBUG = 128;
constexpr unsigned long PYFLAG_ASCII = 256;

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

struct PatternObject {
    PyObject_HEAD
    PyObject* pattern_obj;  // original str or bytes
    real::regex* rx;
    unsigned long py_flags;
    int is_bytes;
};

struct MatchObject {
    PyObject_HEAD
    PyObject* subject;  // str or bytes searched
    PyObject* pattern;  // owning PatternObject
    // 2*(groups+1) entries, -1 for unset. byte_spans index the UTF-8 data;
    // char_spans are what Python sees (equal for bytes and ASCII subjects).
    std::vector<Py_ssize_t>* byte_spans;
    std::vector<Py_ssize_t>* char_spans;
    // Effective (clamped) pos/endpos of the matching call, as re exposes them: character
    // offsets for a str subject, byte offsets for bytes.
    Py_ssize_t pos;
    Py_ssize_t endpos;
};

PatternObject* as_pattern(PyObject* obj) { return reinterpret_cast<PatternObject*>(obj); }
MatchObject* as_match(PyObject* obj) { return reinterpret_cast<MatchObject*>(obj); }

void Pattern_dealloc(PyObject* self) {
    PyTypeObject* tp = Py_TYPE(self);
    PatternObject* pattern = as_pattern(self);
    delete pattern->rx;
    Py_XDECREF(pattern->pattern_obj);
    PyObject_Free(self);
    Py_DECREF(reinterpret_cast<PyObject*>(tp));
}

void Match_dealloc(PyObject* self) {
    PyTypeObject* tp = Py_TYPE(self);
    MatchObject* match = as_match(self);
    delete match->byte_spans;
    delete match->char_spans;
    Py_XDECREF(match->subject);
    Py_XDECREF(match->pattern);
    PyObject_Free(self);
    Py_DECREF(reinterpret_cast<PyObject*>(tp));
}

// ---------------------------------------------------------------------------
// Subject handling
// ---------------------------------------------------------------------------

struct subject_view {
    const char* data;
    Py_ssize_t len;
    bool char_is_byte;  // bytes subject, or pure-ASCII str

    [[nodiscard]] std::string_view view() const {
        return {data, static_cast<std::size_t>(len)};
    }
};

// The C++ lazy match cursor for the dynamic-storage regex: exactly what
// find_iter().begin() yields. basic_match_iterator owns its VM scratch (state_)
// and cursor (pos_/done_/current_) and reads only an immutable program view, so
// operator++ touches NO shared Pattern state (scratch_slots) — independent
// iterators over the same Pattern are reentrant by construction (see real.hpp).
using match_iter_t =
    decltype(std::declval<const real::regex&>().find_iter(std::string_view {}).begin());

struct MatchIteratorObject {
    PyObject_HEAD
    PyObject* pattern;   // owning PatternObject: keeps the regex program + pattern text alive
    PyObject* subject;   // str or bytes: keeps the scanned bytes alive
    subject_view sv;     // by value: a non-owning view into `subject`'s bytes
    Py_ssize_t pos;      // the iterator's pos/endpos (char for str, byte for bytes), copied
    Py_ssize_t endpos;   // onto each yielded Match's .pos/.endpos
    match_iter_t* cur;   // heap cursor; its text_/prog_ borrow `subject` and the regex
};

void MatchIterator_dealloc(PyObject* self) {
    PyTypeObject* tp = Py_TYPE(self);
    auto* it = reinterpret_cast<MatchIteratorObject*>(self);
    delete it->cur;  // delete the cursor BEFORE releasing the refs it borrows from
    Py_XDECREF(it->subject);
    Py_XDECREF(it->pattern);
    PyObject_Free(self);
    Py_DECREF(reinterpret_cast<PyObject*>(tp));
}

// `is_bytes` rather than a PatternObject*: shared by Pattern (pat->is_bytes) and RegexSet
// (rs->is_bytes) — both just need to know which of str/bytes the subject must be.
int get_subject(int is_bytes, PyObject* obj, subject_view* out) {
    if (is_bytes != 0) {
        if (!PyBytes_Check(obj)) {
            PyErr_SetString(PyExc_TypeError,
                            "cannot use a bytes pattern on a string-like object");
            return -1;
        }
        char* data = nullptr;
        Py_ssize_t len = 0;
        if (PyBytes_AsStringAndSize(obj, &data, &len) < 0) {
            return -1;
        }
        *out = {data, len, true};
        return 0;
    }
    if (!PyUnicode_Check(obj)) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot use a string pattern on a bytes-like object");
        return -1;
    }
    Py_ssize_t len = 0;
    const char* data = PyUnicode_AsUTF8AndSize(obj, &len);
    if (data == nullptr) {
        return -1;
    }
    *out = {data, len, PyUnicode_GetLength(obj) == len};
    return 0;
}

// Builds Python-visible spans from byte spans: identity when chars are
// bytes, otherwise one pass over the subject counting codepoints.
void compute_char_spans(const subject_view& sv, const std::vector<Py_ssize_t>& byte_spans,
                        std::vector<Py_ssize_t>& out) {
    out = byte_spans;
    if (sv.char_is_byte) {
        return;
    }
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < byte_spans.size(); ++i) {
        if (byte_spans[i] >= 0) {
            order.push_back(i);
        }
    }
    for (std::size_t a = 1; a < order.size(); ++a) {  // insertion sort: tiny n
        const std::size_t key = order[a];
        std::size_t b = a;
        while (b > 0 && byte_spans[order[b - 1]] > byte_spans[key]) {
            order[b] = order[b - 1];
            --b;
        }
        order[b] = key;
    }
    Py_ssize_t byte_at = 0;
    Py_ssize_t chars = 0;
    for (const std::size_t slot : order) {
        const Py_ssize_t target = byte_spans[slot];
        while (byte_at < target) {
            chars += (static_cast<unsigned char>(sv.data[byte_at]) & 0xC0) != 0x80 ? 1 : 0;
            ++byte_at;
        }
        out[slot] = chars;
    }
}

// Decodes subject bytes [s, e) as the right Python type (str or bytes).
PyObject* slice_subject(PatternObject* pat, const subject_view& sv, Py_ssize_t start,
                        Py_ssize_t end) {
    if (pat->is_bytes != 0) {
        return PyBytes_FromStringAndSize(sv.data + start, end - start);
    }
    return PyUnicode_DecodeUTF8(sv.data + start, end - start, nullptr);
}

PyObject* empty_like(PatternObject* pat) {
    return pat->is_bytes != 0 ? PyBytes_FromStringAndSize("", 0)
                              : PyUnicode_FromStringAndSize("", 0);
}

// ---------------------------------------------------------------------------
// Match construction
// ---------------------------------------------------------------------------

// Convert the C++ exception currently being handled into a Python error and return nullptr.
// Call ONLY from inside a catch block; it keeps any C++ exception from crossing a CPython
// frame (undefined behaviour): bad_alloc -> MemoryError, anything else -> real.error with its
// message. The int-returning call sites ignore the nullptr and return their own -1.
PyObject* set_cpp_error() { return sciforge::binding::set_cpp_error(error_type); }

PyObject* make_match(PatternObject* pat, PyObject* subject, const auto& match,
                     Py_ssize_t pos, Py_ssize_t endpos) {
    auto* obj = PyObject_New(MatchObject, reinterpret_cast<PyTypeObject*>(match_type));
    if (obj == nullptr) {
        return nullptr;
    }
    // Initialise every owned field before anything that can throw, so a partial failure
    // unwinds safely through Match_dealloc (delete on a nullptr span is fine).
    obj->byte_spans = nullptr;
    // char_spans is computed lazily (nullptr until the first .start()/.end()/.span()).
    // .group()/__getitem__ read byte_spans, so a finditer that reads only .group() never
    // pays compute_char_spans, which walks byte 0 -> match offset = O(position) per match
    // (quadratic over a non-ASCII scan). See ensure_char_spans.
    obj->char_spans = nullptr;
    obj->pos = pos;
    obj->endpos = endpos;
    obj->subject = Py_NewRef(subject);
    obj->pattern = Py_NewRef(reinterpret_cast<PyObject*>(pat));
    try {
        obj->byte_spans = new std::vector<Py_ssize_t>(2 * match.size());
    } catch (...) {
        Py_DECREF(obj);  // dealloc frees the two refs; both spans are nullptr
        return set_cpp_error();
    }
    auto& bytes = *obj->byte_spans;
    for (std::size_t group = 0; group < match.size(); ++group) {
        const std::size_t start = match.start(group);
        bytes[2 * group] = start == real::npos ? -1 : static_cast<Py_ssize_t>(start);
        bytes[(2 * group) + 1] = start == real::npos ? -1 : static_cast<Py_ssize_t>(match.end(group));
    }
    return reinterpret_cast<PyObject*>(obj);
}

// ---------------------------------------------------------------------------
// Match methods
// ---------------------------------------------------------------------------

// Group argument -> group number, or -1 with an exception set.
Py_ssize_t resolve_group(MatchObject* match, PyObject* arg) {
    PatternObject* pat = as_pattern(match->pattern);
    if (PyLong_Check(arg)) {
        const Py_ssize_t group = PyLong_AsSsize_t(arg);
        if (group == -1 && PyErr_Occurred() != nullptr) {
            return -1;
        }
        if (group < 0 || static_cast<std::size_t>(group) > pat->rx->group_count()) {
            PyErr_SetString(PyExc_IndexError, "no such group");
            return -1;
        }
        return group;
    }
    if (PyUnicode_Check(arg)) {
        Py_ssize_t len = 0;
        const char* name = PyUnicode_AsUTF8AndSize(arg, &len);
        if (name == nullptr) {
            return -1;
        }
        const std::size_t group =
            pat->rx->group_index(std::string_view(name, static_cast<std::size_t>(len)));
        if (group == real::npos) {
            PyErr_SetString(PyExc_IndexError, "no such group");
            return -1;
        }
        return static_cast<Py_ssize_t>(group);
    }
    PyErr_SetString(PyExc_IndexError, "no such group");
    return -1;
}

PyObject* group_value(MatchObject* match, Py_ssize_t group, PyObject* default_value) {
    PatternObject* pat = as_pattern(match->pattern);
    const Py_ssize_t start = (*match->byte_spans)[2 * group];
    if (start < 0) {
        return Py_NewRef(default_value);
    }
    const Py_ssize_t end = (*match->byte_spans)[(2 * group) + 1];
    subject_view sv;
    if (get_subject(pat->is_bytes, match->subject, &sv) < 0) {
        return nullptr;
    }
    return slice_subject(pat, sv, start, end);
}

PyObject* Match_group(PyObject* self, PyObject* args) {
    MatchObject* match = as_match(self);
    const Py_ssize_t nargs = PyTuple_Size(args);
    if (nargs == 0) {
        return group_value(match, 0, Py_None);
    }
    if (nargs == 1) {
        const Py_ssize_t group = resolve_group(match, PyTuple_GetItem(args, 0));
        return group < 0 ? nullptr : group_value(match, group, Py_None);
    }
    PyObject* out = PyTuple_New(nargs);
    if (out == nullptr) {
        return nullptr;
    }
    for (Py_ssize_t i = 0; i < nargs; ++i) {
        const Py_ssize_t group = resolve_group(match, PyTuple_GetItem(args, i));
        PyObject* value = group < 0 ? nullptr : group_value(match, group, Py_None);
        if (value == nullptr) {
            Py_DECREF(out);
            return nullptr;
        }
        PyTuple_SetItem(out, i, value);
    }
    return out;
}

PyObject* Match_subscript(PyObject* self, PyObject* key) {
    MatchObject* match = as_match(self);
    const Py_ssize_t group = resolve_group(match, key);
    return group < 0 ? nullptr : group_value(match, group, Py_None);
}

PyObject* Match_groups(PyObject* self, PyObject* args, PyObject* kwargs) {
    MatchObject* match = as_match(self);
    PyObject* default_value = Py_None;
    static const char* const keywords[] = {"default", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O",
                                     const_cast<char**>(keywords), &default_value)) {
        return nullptr;
    }
    const auto group_count = static_cast<Py_ssize_t>(as_pattern(match->pattern)->rx->group_count());
    PyObject* out = PyTuple_New(group_count);
    if (out == nullptr) {
        return nullptr;
    }
    for (Py_ssize_t group = 1; group <= group_count; ++group) {
        PyObject* value = group_value(match, group, default_value);
        if (value == nullptr) {
            Py_DECREF(out);
            return nullptr;
        }
        PyTuple_SetItem(out, group - 1, value);
    }
    return out;
}

PyObject* Match_groupdict(PyObject* self, PyObject* args, PyObject* kwargs) {
    MatchObject* match = as_match(self);
    PyObject* default_value = Py_None;
    static const char* const keywords[] = {"default", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O",
                                     const_cast<char**>(keywords), &default_value)) {
        return nullptr;
    }
    PyObject* out = PyDict_New();
    if (out == nullptr) {
        return nullptr;
    }
    for (const auto& [name, index] : as_pattern(match->pattern)->rx->named_groups()) {
        PyObject* value = group_value(match, static_cast<Py_ssize_t>(index), default_value);
        if (value == nullptr ||
            PyDict_SetItemString(out, std::string(name).c_str(), value) < 0) {
            Py_XDECREF(value);
            Py_DECREF(out);
            return nullptr;
        }
        Py_DECREF(value);
    }
    return out;
}

// Computes and caches char_spans on first use (lazy). make_match leaves it nullptr;
// only .start()/.end()/.span() need it, so a finditer reading only .group() (which uses
// byte_spans) never pays compute_char_spans -- O(match offset) per match, i.e. quadratic
// over a non-ASCII scan. Runs under the GIL (every Match method does), so the
// check-compute-cache is serialized and idempotent; sv is re-derived like group_value.
int ensure_char_spans(MatchObject* match) {
    if (match->char_spans != nullptr) {
        return 0;
    }
    subject_view sv;
    if (get_subject(as_pattern(match->pattern)->is_bytes, match->subject, &sv) < 0) {
        return -1;
    }
    try {
        match->char_spans = new std::vector<Py_ssize_t>();
        compute_char_spans(sv, *match->byte_spans, *match->char_spans);
    } catch (...) {
        delete match->char_spans;     // nullptr (new threw) or the partly-built vector
        match->char_spans = nullptr;  // leave it recomputable and dealloc-safe
        set_cpp_error();
        return -1;
    }
    return 0;
}

enum class span_part : std::uint8_t { start, end, both };

PyObject* match_position(PyObject* self, PyObject* args, span_part part) {
    MatchObject* match = as_match(self);
    PyObject* arg = nullptr;
    if (!PyArg_ParseTuple(args, "|O", &arg)) {
        return nullptr;
    }
    Py_ssize_t group = 0;
    if (arg != nullptr) {
        group = resolve_group(match, arg);
        if (group < 0) {
            return nullptr;
        }
    }
    if (ensure_char_spans(match) < 0) {
        return nullptr;
    }
    const Py_ssize_t start = (*match->char_spans)[2 * group];
    const Py_ssize_t end = (*match->char_spans)[(2 * group) + 1];
    switch (part) {
        case span_part::start:
            return PyLong_FromSsize_t(start);
        case span_part::end:
            return PyLong_FromSsize_t(end);
        case span_part::both:
            return Py_BuildValue("(nn)", start, end);
    }
    return nullptr;  // unreachable
}

PyObject* Match_start(PyObject* self, PyObject* args) {
    return match_position(self, args, span_part::start);
}
PyObject* Match_end(PyObject* self, PyObject* args) {
    return match_position(self, args, span_part::end);
}
PyObject* Match_span(PyObject* self, PyObject* args) {
    return match_position(self, args, span_part::both);
}

PyObject* Match_get_re(PyObject* self, void*) { return Py_NewRef(as_match(self)->pattern); }
PyObject* Match_get_string(PyObject* self, void*) {
    return Py_NewRef(as_match(self)->subject);
}
PyObject* Match_get_pos(PyObject* self, void*) { return PyLong_FromSsize_t(as_match(self)->pos); }
PyObject* Match_get_endpos(PyObject* self, void*) { return PyLong_FromSsize_t(as_match(self)->endpos); }

// Defined after the replacement-template machinery (apply_template / parse_template).
PyObject* Match_expand(PyObject* self, PyObject* template_arg);

PyMethodDef match_methods[] = {
    {"group", Match_group, METH_VARARGS,
     "group($self, /, *groups)\n--\n\n"
     "Return the matched substring or subgroups.\n\n"
     "Args:\n"
     "    group (int or str, optional): Group number or name. Defaults to 0\n"
     "        (the whole match).\n\n"
     "Returns:\n"
     "    str or bytes or None: The matched text, or None if the group did\n"
     "        not participate. Multiple arguments return a tuple."},
    {"groups", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Match_groups)),
     METH_VARARGS | METH_KEYWORDS,
     "groups($self, default=None)\n--\n\n"
     "Return a tuple of all subgroup strings.\n\n"
     "Args:\n"
     "    default: Value for groups that did not participate.\n\n"
     "Returns:\n"
     "    tuple: One entry per capturing group (group 1 onwards)."},
    {"groupdict", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Match_groupdict)),
     METH_VARARGS | METH_KEYWORDS,
     "groupdict($self, default=None)\n--\n\n"
     "Return a dictionary mapping group names to matched strings.\n\n"
     "Args:\n"
     "    default: Value for groups that did not participate.\n\n"
     "Returns:\n"
     "    dict: {name: matched_text} for all named groups."},
    {"start", Match_start, METH_VARARGS,
     "start($self, group=0, /)\n--\n\n"
     "Return the start index of a group in the original string.\n\n"
     "Args:\n"
     "    group (int or str, optional): Group number or name. Defaults to 0.\n\n"
     "Returns:\n"
     "    int: Character index where the group starts."},
    {"end", Match_end, METH_VARARGS,
     "end($self, group=0, /)\n--\n\n"
     "Return the end index of a group in the original string.\n\n"
     "Args:\n"
     "    group (int or str, optional): Group number or name. Defaults to 0.\n\n"
     "Returns:\n"
     "    int: Character index where the group ends."},
    {"span", Match_span, METH_VARARGS,
     "span($self, group=0, /)\n--\n\n"
     "Return the (start, end) indices of a group.\n\n"
     "Args:\n"
     "    group (int or str, optional): Group number or name. Defaults to 0.\n\n"
     "Returns:\n"
     "    tuple: (start, end) character indices."},
    {"expand", Match_expand, METH_O,
     "expand($self, template, /)\n--\n\n"
     "Return the string obtained by backslash-substituting the template, exactly\n"
     "as sub() would for this match.\n\n"
     "Args:\n"
     "    template (str or bytes): A template with \\1, \\g<name>, \\g<1>, \\g<0>\n"
     "        (the whole match) and escapes. Must match the pattern's str/bytes\n"
     "        type.\n\n"
     "Returns:\n"
     "    str or bytes: The expanded template. A group that did not participate\n"
     "        contributes nothing."},
    {nullptr, nullptr, 0, nullptr},
};

// re.Match.lastindex: index of the last capturing group to CLOSE in this match -- the re
// semantics are "last marked", NOT the highest index (((a)(b)) -> 1, not 3). Read it off the
// program's close-save offsets: scanning in offset order, the last participating close-save
// (an odd slot, group >= 1) wins. This is exact (it also resolves zero-width cases, where the
// spans alone cannot tell nesting from sequence). -1 means no group matched (Python None).
Py_ssize_t match_lastindex_value(MatchObject* match) {
    PatternObject*                   pat   = as_pattern(match->pattern);
    const real::detail::program_view prog  = pat->rx->raw_program();
    const std::vector<Py_ssize_t>&   spans = *match->byte_spans;
    Py_ssize_t                       last  = -1;
    for (const real::detail::instr& in : prog.code) {
        if (in.op != real::detail::opcode::save) {
            continue;
        }
        const unsigned slot = in.arg16;
        if ((slot & 1U) == 0U || slot == 1U) {
            continue;  // an opening save, or group 0's closing save
        }
        const std::size_t group = (slot - 1U) / 2U;
        if ((2U * group) < spans.size() && spans[2U * group] >= 0) {
            last = static_cast<Py_ssize_t>(group);  // participated; a later offset overrides
        }
    }
    return last;
}

PyObject* Match_get_lastindex(PyObject* self, void* /*closure*/) {
    const Py_ssize_t index = match_lastindex_value(as_match(self));
    if (index < 0) {
        Py_RETURN_NONE;
    }
    return PyLong_FromSsize_t(index);
}

PyObject* Match_get_lastgroup(PyObject* self, void* /*closure*/) {
    MatchObject*     match = as_match(self);
    const Py_ssize_t index = match_lastindex_value(match);
    if (index < 0) {
        Py_RETURN_NONE;
    }
    for (const auto& [name, group] : as_pattern(match->pattern)->rx->named_groups()) {
        if (static_cast<Py_ssize_t>(group) == index) {
            return PyUnicode_FromStringAndSize(name.data(), static_cast<Py_ssize_t>(name.size()));
        }
    }
    Py_RETURN_NONE;  // the last group is unnamed
}

// re.Match.regs: ((start0, end0), (start1, end1), ...) over group 0 and every group, in the
// character offsets Python sees; a group that did not participate is (-1, -1).
PyObject* Match_get_regs(PyObject* self, void* /*closure*/) {
    MatchObject* match = as_match(self);
    if (ensure_char_spans(match) < 0) {
        return nullptr;
    }
    const std::vector<Py_ssize_t>& spans = *match->char_spans;
    const Py_ssize_t               count = static_cast<Py_ssize_t>(spans.size() / 2);
    PyObject*                      regs  = PyTuple_New(count);
    if (regs == nullptr) {
        return nullptr;
    }
    for (Py_ssize_t group = 0; group < count; ++group) {
        PyObject* pair = Py_BuildValue("(nn)", spans[2 * group], spans[(2 * group) + 1]);
        if (pair == nullptr || PyTuple_SetItem(regs, group, pair) < 0) {
            Py_DECREF(regs);
            return nullptr;
        }
    }
    return regs;
}

// re.Match repr: <real.Match object; span=(s, e), match=REPR> with the match repr truncated
// to 50 characters, like re (which uses "%.50R").
PyObject* Match_repr(PyObject* self) {
    MatchObject* match = as_match(self);
    if (ensure_char_spans(match) < 0) {
        return nullptr;
    }
    PyObject* whole = group_value(match, 0, Py_None);  // group 0 always participates
    if (whole == nullptr) {
        return nullptr;
    }
    PyObject* result = PyUnicode_FromFormat("<real.Match object; span=(%zd, %zd), match=%.50R>",
                                            (*match->char_spans)[0], (*match->char_spans)[1], whole);
    Py_DECREF(whole);
    return result;
}

PyGetSetDef match_getset[] = {
    {"re", Match_get_re, nullptr, "The Pattern object that produced this match.", nullptr},
    {"string", Match_get_string, nullptr, "The string or bytes that was searched.", nullptr},
    {"pos", Match_get_pos, nullptr, "Effective pos passed to the matching call (clamped; default 0).", nullptr},
    {"endpos", Match_get_endpos, nullptr, "Effective endpos passed to the matching call (clamped; default len).", nullptr},
    {"lastindex", Match_get_lastindex, nullptr, "Index of the last matched capturing group, or None.", nullptr},
    {"lastgroup", Match_get_lastgroup, nullptr, "Name of the last matched capturing group, or None.", nullptr},
    {"regs", Match_get_regs, nullptr, "Tuple of (start, end) spans for the whole match and each group.", nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyType_Slot match_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(Match_dealloc)},
    {Py_tp_repr, reinterpret_cast<void*>(Match_repr)},
    {Py_tp_methods, static_cast<void*>(match_methods)},
    {Py_tp_getset, static_cast<void*>(match_getset)},
    {Py_mp_subscript, reinterpret_cast<void*>(Match_subscript)},
    {Py_tp_doc,
     const_cast<char*>("The result of a successful match, with the re.Match API.\n\n"
                       "Returned by Pattern match/fullmatch/search/finditer -- not instantiable\n"
                       "directly. Supports m[group] subscripting; always truthy.")},
    {0, nullptr},
};

PyType_Spec match_spec = {
    "real.Match",
    sizeof(MatchObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    match_slots,
};

// ---------------------------------------------------------------------------
// Pattern: simple run methods
// ---------------------------------------------------------------------------

// char offset -> byte offset within sv. For a bytes subject or a pure-ASCII str
// (char_is_byte), char == byte; otherwise walk the UTF-8 the way compute_char_spans
// counts codepoints. `char_idx` must already be clamped to [0, char_len]; a char offset
// always lands on a codepoint boundary, so the byte offset is exact.
std::size_t char_to_byte(const subject_view& sv, Py_ssize_t char_idx) {
    if (sv.char_is_byte) {
        return static_cast<std::size_t>(char_idx);
    }
    std::size_t byte = 0;
    const auto len = static_cast<std::size_t>(sv.len);
    for (Py_ssize_t chars = 0; chars < char_idx && byte < len; ++chars) {
        ++byte;  // the lead byte
        while (byte < len && (static_cast<unsigned char>(sv.data[byte]) & 0xC0U) == 0x80U) {
            ++byte;  // skip UTF-8 continuation bytes
        }
    }
    return byte;
}

// Backs Pattern.match/search/fullmatch with the re signature (string, pos=0,
// endpos=sys.maxsize). pos/endpos are PER-CALL (no stored state); they are CHARACTER
// offsets for a str subject, BYTE offsets for bytes. The attempt runs over text[0:endpos]
// starting at pos: pos is the VM start, NOT a slice (so \A and ^ without MULTILINE fail
// at pos>0); endpos truncates the subject to a view. Capture offsets are absolute.
PyObject* run_region(PyObject* self, PyObject* args, PyObject* kwargs, real::detail::run_mode mode) {
    PatternObject* pat = as_pattern(self);
    PyObject* string = nullptr;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = PY_SSIZE_T_MAX;
    static const char* const keywords[] = {"string", "pos", "endpos", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nn", const_cast<char**>(keywords),
                                     &string, &pos, &endpos)) {
        return nullptr;
    }
    subject_view sv;
    if (get_subject(pat->is_bytes, string, &sv) < 0) {
        return nullptr;
    }
    // Clamp char offsets to [0, char_len] (re clamps silently — out of range never errors),
    // then convert to byte offsets.
    const Py_ssize_t char_len = sv.char_is_byte ? sv.len : PyUnicode_GetLength(string);
    pos = std::clamp(pos, Py_ssize_t {0}, char_len);
    endpos = std::clamp(endpos, Py_ssize_t {0}, char_len);
    const std::size_t pos_byte = char_to_byte(sv, pos);
    const std::size_t end_byte = char_to_byte(sv, endpos);
    const std::string_view region = sv.view().substr(0, end_byte);  // endpos truncation (a view)

    // Per-call VM scratch (no shared Pattern state) → reentrant; the scan may run with the
    // GIL released. A reused (thread-local) state is NOT safe: pike_vm caches the class
    // lookup table inside the state keyed by the per-PROGRAM class index, so reuse across
    // patterns would serve a stale table. Subject bytes stay valid while released: immutable
    // str/bytes, ref held, UTF-8 already materialised. Released only above the size threshold
    // (below it the toggle would dominate a sub-microsecond scan).
    const real::detail::program_view prog = pat->rx->raw_program();
    real::detail::pike_state         state;
    std::vector<std::size_t>         slots;
    real::detail::pike_vm            vm(prog, state);
    const std::size_t scan_len = pos_byte < end_byte ? end_byte - pos_byte : 0;
    try {
        bool matched = false;
        if (scan_len >= static_cast<std::size_t>(gil_release_min_bytes)) {
            const GilRelease unlocked;  // released ONLY around the pure-C++ scan
            matched = vm.run(region, pos_byte, mode, slots);
        }
        else {
            matched = vm.run(region, pos_byte, mode, slots);  // small: toggle would cost more
        }
        if (!matched) {
            Py_RETURN_NONE;
        }
        // Built over the FULL subject so capture offsets are absolute (slots are in [0, endpos)).
        const real::match_result match(sv.view(), slots, true, pat->rx->pattern(), prog.names);
        return make_match(pat, string, match, pos, endpos);  // .pos/.endpos = the clamped offsets
    } catch (...) {
        return set_cpp_error();  // e.g. bad_alloc growing the scratch -> Python error, never UB
    }
}

PyObject* Pattern_match(PyObject* self, PyObject* args, PyObject* kwargs) {
    return run_region(self, args, kwargs, real::detail::run_mode::prefix);
}
PyObject* Pattern_fullmatch(PyObject* self, PyObject* args, PyObject* kwargs) {
    return run_region(self, args, kwargs, real::detail::run_mode::full);
}
PyObject* Pattern_search(PyObject* self, PyObject* args, PyObject* kwargs) {
    return run_region(self, args, kwargs, real::detail::run_mode::search);
}

// ---------------------------------------------------------------------------
// Pattern: findall / finditer / split
// ---------------------------------------------------------------------------

// Phase one of the two-phase findall / split on large subjects: walk the matches
// with the GIL released and record each match's group byte spans into a flat buffer
// with stride 2*(ngroups+1) — [start0,end0, start1,end1, ...] (group 0 is the whole
// match; an unmatched optional group stores real::npos). This is pure C++ and
// reentrant: the iterator owns its VM scratch and only reads an immutable program
// view, so threads collect concurrently on a shared Pattern. The subject bytes stay
// valid while released (immutable str/bytes, ref held, UTF-8 already materialised).
// `max_matches == 0` means no limit (findall); split passes its maxsplit. Returns
// false only on a C++ allocation failure — the GIL is already re-acquired by then.
bool collect_match_spans(PatternObject* pat, const subject_view& sv, std::size_t ngroups,
                         std::size_t max_matches, std::vector<std::size_t>& spans,
                         std::size_t pos_byte, std::size_t endpos_byte) {
    try {
        const GilRelease unlocked;
        std::size_t count = 0;
        for (const auto& match : pat->rx->find_iter(sv.view(), pos_byte, endpos_byte)) {
            if (max_matches != 0 && count == max_matches) {
                break;
            }
            for (std::size_t group = 0; group <= ngroups; ++group) {
                spans.push_back(match.start(group));
                spans.push_back(match.end(group));
            }
            ++count;
        }
    } catch (...) {
        return false;  // ~GilRelease re-acquired the GIL during unwinding
    }
    return true;
}

PyObject* Pattern_findall(PyObject* self, PyObject* args, PyObject* kwargs) {
    PatternObject* pat = as_pattern(self);
    PyObject* string = nullptr;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = PY_SSIZE_T_MAX;
    static const char* const keywords[] = {"string", "pos", "endpos", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nn", const_cast<char**>(keywords),
                                     &string, &pos, &endpos)) {
        return nullptr;
    }
    subject_view sv;
    if (get_subject(pat->is_bytes, string, &sv) < 0) {
        return nullptr;
    }
    const Py_ssize_t char_len = sv.char_is_byte ? sv.len : PyUnicode_GetLength(string);
    pos = std::clamp(pos, Py_ssize_t {0}, char_len);
    endpos = std::clamp(endpos, Py_ssize_t {0}, char_len);
    const std::size_t pos_byte = char_to_byte(sv, pos);
    const std::size_t end_byte = char_to_byte(sv, endpos);
    PyObject* out = PyList_New(0);
    if (out == nullptr) {
        return nullptr;
    }
    const std::size_t ngroups = pat->rx->group_count();
    // Builds one findall item from a match's group spans (g == 0 is the whole match;
    // an unmatched optional group has start == npos). `span(g)` returns {start, end}.
    const auto build_item = [&](auto span) -> PyObject* {
        if (ngroups == 0) {
            const auto [s, e] = span(0);
            return slice_subject(pat, sv, static_cast<Py_ssize_t>(s), static_cast<Py_ssize_t>(e));
        }
        if (ngroups == 1) {
            const auto [s, e] = span(1);
            return s == real::npos
                       ? empty_like(pat)
                       : slice_subject(pat, sv, static_cast<Py_ssize_t>(s), static_cast<Py_ssize_t>(e));
        }
        PyObject* item = PyTuple_New(static_cast<Py_ssize_t>(ngroups));
        if (item == nullptr) {
            return nullptr;
        }
        for (std::size_t group = 1; group <= ngroups; ++group) {
            const auto [s, e] = span(group);
            PyObject* part = s == real::npos
                                 ? empty_like(pat)
                                 : slice_subject(pat, sv, static_cast<Py_ssize_t>(s),
                                                 static_cast<Py_ssize_t>(e));
            if (part == nullptr) {
                Py_DECREF(item);
                return nullptr;
            }
            PyTuple_SetItem(item, static_cast<Py_ssize_t>(group) - 1, part);
        }
        return item;
    };
    const auto append_item = [&](PyObject* item) -> bool {
        if (item == nullptr || PyList_Append(out, item) < 0) {
            Py_XDECREF(item);
            return false;
        }
        Py_DECREF(item);
        return true;
    };

    const std::size_t scan_len = pos_byte < end_byte ? end_byte - pos_byte : 0;
    if (scan_len >= static_cast<std::size_t>(gil_release_collect_min_bytes)) {
        // Large region: collect spans with the GIL released, then build under the GIL.
        std::vector<std::size_t> spans;
        if (!collect_match_spans(pat, sv, ngroups, 0, spans, pos_byte, end_byte)) {
            Py_DECREF(out);
            return PyErr_NoMemory();
        }
        const std::size_t stride = 2 * (ngroups + 1);
        for (std::size_t base = 0; base < spans.size(); base += stride) {
            if (!append_item(build_item([&](std::size_t group) {
                return std::pair {spans[base + (2 * group)], spans[base + (2 * group) + 1]};
            }))) {
                Py_DECREF(out);
                return nullptr;
            }
        }
        return out;
    }

    // Small region: interleaved scan under the held GIL (releasing it would cost more
    // than the sub-microsecond walk).
    try {
        for (const auto& match : pat->rx->find_iter(sv.view(), pos_byte, end_byte)) {
            if (!append_item(build_item([&](std::size_t group) {
                return std::pair {match.start(group), match.end(group)};
            }))) {
                Py_DECREF(out);
                return nullptr;
            }
        }
    } catch (...) {
        Py_DECREF(out);
        return set_cpp_error();
    }
    return out;
}

// Matching-only count: once-per-walk TrailingLA dispatch when eligible (see
// real::regex::count_matches). No Match / Python objects are materialised — the
// path Python callers need for trailing-LA class+ throughput (finditer stays pure).
PyObject* Pattern_count_matches(PyObject* self, PyObject* args, PyObject* kwargs) {
    PatternObject* pat = as_pattern(self);
    PyObject* string = nullptr;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = PY_SSIZE_T_MAX;
    static const char* const keywords[] = {"string", "pos", "endpos", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nn", const_cast<char**>(keywords),
                                     &string, &pos, &endpos)) {
        return nullptr;
    }
    subject_view sv;
    if (get_subject(pat->is_bytes, string, &sv) < 0) {
        return nullptr;
    }
    const Py_ssize_t char_len = sv.char_is_byte ? sv.len : PyUnicode_GetLength(string);
    pos = std::clamp(pos, Py_ssize_t {0}, char_len);
    endpos = std::clamp(endpos, Py_ssize_t {0}, char_len);
    const std::size_t pos_byte = char_to_byte(sv, pos);
    const std::size_t end_byte = char_to_byte(sv, endpos);
    const std::size_t scan_len = pos_byte < end_byte ? end_byte - pos_byte : 0;
    try {
        std::size_t n = 0;
        if (scan_len >= static_cast<std::size_t>(gil_release_min_bytes)) {
            const GilRelease unlocked;
            n = pat->rx->count_matches(sv.view(), pos_byte, end_byte);
        } else {
            n = pat->rx->count_matches(sv.view(), pos_byte, end_byte);
        }
        return PyLong_FromSize_t(n);
    } catch (...) {
        return set_cpp_error();
    }
}

PyObject* MatchIterator_iter(PyObject* self) {
    return Py_NewRef(self);
}

PyObject* MatchIterator_iternext(PyObject* self) {
    auto* it = reinterpret_cast<MatchIteratorObject*>(self);
    if (*it->cur == match_iter_t {}) {  // default-constructed == end sentinel: exhausted
        return nullptr;                 // NULL with no exception set => StopIteration
    }
    PyObject* obj = nullptr;
    try {
        obj = make_match(as_pattern(it->pattern), it->subject, **it->cur, it->pos, it->endpos);
        if (obj == nullptr) {
            return nullptr;
        }
        ++(*it->cur);  // advances the lazy scan; may grow the scratch (bad_alloc)
    } catch (...) {
        Py_XDECREF(obj);
        return set_cpp_error();
    }
    return obj;
}

PyType_Slot match_iterator_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(MatchIterator_dealloc)},
    {Py_tp_iter, reinterpret_cast<void*>(MatchIterator_iter)},
    {Py_tp_iternext, reinterpret_cast<void*>(MatchIterator_iternext)},
    {Py_tp_doc,
     const_cast<char*>("Lazy iterator over the non-overlapping matches of a region.\n\n"
                       "Returned by Pattern.finditer() -- not instantiable directly. Yields\n"
                       "Match objects left to right.")},
    {0, nullptr},
};

PyType_Spec match_iterator_spec = {
    "real.MatchIterator",
    sizeof(MatchIteratorObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    match_iterator_slots,
};

// Lazy: holds the C++ match cursor and yields one Match per __next__, so peak
// memory is O(1) in the number of matches (findall stays eager — a list is correct
// there). The cursor borrows the regex program and the subject bytes; both are
// pinned by the pattern/subject refs and the stored subject_view.
PyObject* Pattern_finditer(PyObject* self, PyObject* args, PyObject* kwargs) {
    PatternObject* pat = as_pattern(self);
    PyObject* string = nullptr;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = PY_SSIZE_T_MAX;
    static const char* const keywords[] = {"string", "pos", "endpos", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nn", const_cast<char**>(keywords),
                                     &string, &pos, &endpos)) {
        return nullptr;
    }
    subject_view sv;
    if (get_subject(pat->is_bytes, string, &sv) < 0) {
        return nullptr;
    }
    const Py_ssize_t char_len = sv.char_is_byte ? sv.len : PyUnicode_GetLength(string);
    pos = std::clamp(pos, Py_ssize_t {0}, char_len);
    endpos = std::clamp(endpos, Py_ssize_t {0}, char_len);
    auto* it = PyObject_New(MatchIteratorObject, reinterpret_cast<PyTypeObject*>(match_iterator_type));
    if (it == nullptr) {
        return nullptr;
    }
    it->pattern = Py_NewRef(self);
    it->subject = Py_NewRef(string);
    it->sv = sv;
    it->pos = pos;        // char/byte offsets, copied onto each yielded Match's .pos/.endpos
    it->endpos = endpos;
    it->cur = nullptr;
    try {
        it->cur = new match_iter_t(
            pat->rx->find_iter(it->sv.view(), char_to_byte(sv, pos), char_to_byte(sv, endpos)).begin());
    } catch (...) {
        Py_DECREF(reinterpret_cast<PyObject*>(it));  // dealloc frees the refs; cur is null
        return set_cpp_error();  // bad_alloc -> MemoryError, otherwise real.error
    }
    return reinterpret_cast<PyObject*>(it);
}

PyObject* Pattern_split(PyObject* self, PyObject* args, PyObject* kwargs) {
    PatternObject* pat = as_pattern(self);
    PyObject* string = nullptr;
    Py_ssize_t maxsplit = 0;
    static const char* const keywords[] = {"string", "maxsplit", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|n", const_cast<char**>(keywords),
                                     &string, &maxsplit)) {
        return nullptr;
    }
    subject_view sv;
    if (get_subject(pat->is_bytes, string, &sv) < 0) {
        return nullptr;
    }
    PyObject* out = PyList_New(0);
    if (out == nullptr) {
        return nullptr;
    }
    const auto append = [&](PyObject* item) {
        if (item == nullptr || PyList_Append(out, item) < 0) {
            Py_XDECREF(item);
            return false;
        }
        Py_DECREF(item);
        return true;
    };
    const std::size_t ngroups = pat->rx->group_count();
    Py_ssize_t        last    = 0;
    // Emits the segment before one match followed by its captured-group pieces, then
    // advances `last` past the match. `span(g)` returns {start, end} byte offsets
    // (g == 0 is the whole match; an unmatched optional group has start == npos).
    const auto emit_match = [&](auto span) -> bool {
        const auto [ms, me] = span(0);
        if (!append(slice_subject(pat, sv, last, static_cast<Py_ssize_t>(ms)))) {
            return false;
        }
        for (std::size_t group = 1; group <= ngroups; ++group) {
            const auto [gs, ge] = span(group);
            PyObject* piece = gs == real::npos
                                  ? Py_NewRef(Py_None)
                                  : slice_subject(pat, sv, static_cast<Py_ssize_t>(gs),
                                                  static_cast<Py_ssize_t>(ge));
            if (!append(piece)) {
                return false;
            }
        }
        last = static_cast<Py_ssize_t>(me);
        return true;
    };

    if (sv.len >= gil_release_collect_min_bytes) {
        // Large subject: collect spans with the GIL released, then build under the GIL.
        std::vector<std::size_t> spans;
        const std::size_t max_matches = maxsplit > 0 ? static_cast<std::size_t>(maxsplit) : 0;
        if (!collect_match_spans(pat, sv, ngroups, max_matches, spans, 0, static_cast<std::size_t>(sv.len))) {
            Py_DECREF(out);
            return PyErr_NoMemory();
        }
        const std::size_t stride = 2 * (ngroups + 1);
        for (std::size_t base = 0; base < spans.size(); base += stride) {
            if (!emit_match([&](std::size_t group) {
                return std::pair {spans[base + (2 * group)], spans[base + (2 * group) + 1]};
            })) {
                Py_DECREF(out);
                return nullptr;
            }
        }
    }
    else {
        // Small subject: interleaved scan under the held GIL. Byte-identical behaviour.
        try {
            Py_ssize_t done = 0;
            for (const auto& match : pat->rx->find_iter(sv.view())) {
                if (maxsplit != 0 && done == maxsplit) {
                    break;
                }
                if (!emit_match([&](std::size_t group) {
                    return std::pair {match.start(group), match.end(group)};
                })) {
                    Py_DECREF(out);
                    return nullptr;
                }
                ++done;
            }
        } catch (...) {
            Py_DECREF(out);
            return set_cpp_error();
        }
    }
    // Trailing segment after the last match (common to both paths).
    if (!append(slice_subject(pat, sv, last, sv.len))) {
        Py_DECREF(out);
        return nullptr;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pattern: sub / subn
// ---------------------------------------------------------------------------

// One parsed piece of a replacement template: literal bytes, or a group.
struct repl_segment {
    std::string literal;
    Py_ssize_t group = -1;
};

void set_error(const char* message) { PyErr_SetString(error_type, message); }

// Parses Python's replacement template syntax (\1, \group<name>, escapes).
int parse_template(PatternObject* pat, std::string_view repl,
                   std::vector<repl_segment>& out) {
    std::string literal;
    const auto flush_group = [&](Py_ssize_t group) {
        out.push_back({.literal = literal, .group = -1});
        literal.clear();
        out.push_back({.literal = std::string(), .group = group});
    };
    std::size_t i = 0;
    while (i < repl.size()) {
        const char ch = repl[i];
        if (ch != '\\') {
            literal.push_back(ch);
            ++i;
            continue;
        }
        ++i;
        if (i >= repl.size()) {
            set_error("bad escape (end of pattern)");
            return -1;
        }
        const char next_ch = repl[i];
        if (next_ch >= '0' && next_ch <= '9') {
            // Octal-vs-group-reference decoding is shared with the pattern parser
            // (real::detail::decode_digit_escape, ast.hpp) so the two never drift. Here a group
            // reference resolves to the matched group's text; in a pattern it is a back-reference.
            // For str the octal byte becomes chr(value) in UTF-8 (values >= 128 round-trip);
            // for bytes it is one raw byte.
            const auto push_char_code = [&](unsigned value) {
                if (pat->is_bytes != 0 || value < 0x80U) {
                    literal.push_back(static_cast<char>(value));  // one byte (bytes, or ASCII str)
                }
                else {
                    literal.push_back(static_cast<char>(0xC0U | (value >> 6U)));  // chr(value) as UTF-8
                    literal.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
                }
            };
            const real::detail::digit_escape_result decoded {real::detail::decode_digit_escape(repl, i)};
            i += decoded.length;
            if (decoded.kind == real::detail::digit_escape_kind::octal_overflow) {
                set_error("octal escape value outside of range 0-0o377");
                return -1;
            }
            if (decoded.kind == real::detail::digit_escape_kind::octal) {
                push_char_code(decoded.value);
                continue;
            }
            const Py_ssize_t group {static_cast<Py_ssize_t>(decoded.value)};  // decimal group reference
            if (static_cast<std::size_t>(group) > pat->rx->group_count()) {
                set_error("invalid group reference");
                return -1;
            }
            flush_group(group);
            continue;
        }
        if (next_ch == 'g') {
            ++i;
            if (i >= repl.size() || repl[i] != '<') {
                set_error("missing < in \\g");
                return -1;
            }
            const std::size_t name_begin = ++i;
            while (i < repl.size() && repl[i] != '>') {
                ++i;
            }
            if (i == repl.size() || i == name_begin) {
                set_error("missing group name in \\g<>");
                return -1;
            }
            const std::string_view name = repl.substr(name_begin, i - name_begin);
            ++i;  // consume '>'
            Py_ssize_t group = -1;
            if (name[0] >= '0' && name[0] <= '9') {
                group = 0;
                for (const char digit : name) {
                    if (digit < '0' || digit > '9') {
                        set_error("bad character in group name");
                        return -1;
                    }
                    group = (group * 10) + (digit - '0');
                }
            } else {
                const std::size_t named_group_index = pat->rx->group_index(name);
                if (named_group_index == real::npos) {
                    set_error("unknown group name");
                    return -1;
                }
                group = static_cast<Py_ssize_t>(named_group_index);
            }
            if (static_cast<std::size_t>(group) > pat->rx->group_count()) {
                set_error("invalid group reference");
                return -1;
            }
            flush_group(group);
            continue;
        }
        ++i;
        switch (next_ch) {
            case 'n': literal.push_back('\n'); break;
            case 't': literal.push_back('\t'); break;
            case 'r': literal.push_back('\r'); break;
            case 'f': literal.push_back('\f'); break;
            case 'v': literal.push_back('\v'); break;
            case 'a': literal.push_back('\a'); break;
            case 'b': literal.push_back('\b'); break;
            case '\\': literal.push_back('\\'); break;
            default:
                // Like Python: unknown letter escapes are errors, escaped
                // punctuation keeps the backslash.
                if ((next_ch >= 'A' && next_ch <= 'Z') || (next_ch >= 'a' && next_ch <= 'z')) {
                    set_error("bad escape in replacement");
                    return -1;
                }
                literal.push_back('\\');
                literal.push_back(next_ch);
                break;
        }
    }
    out.push_back({.literal = literal, .group = -1});
    return 0;
}

// repl text (str or bytes, matching the pattern type) -> UTF-8 view.
int get_repl_text(PatternObject* pat, PyObject* repl, std::string_view* out) {
    if (pat->is_bytes != 0) {
        if (!PyBytes_Check(repl)) {
            PyErr_SetString(PyExc_TypeError, "expected bytes replacement");
            return -1;
        }
        char* data = nullptr;
        Py_ssize_t len = 0;
        if (PyBytes_AsStringAndSize(repl, &data, &len) < 0) {
            return -1;
        }
        *out = {data, static_cast<std::size_t>(len)};
        return 0;
    }
    if (!PyUnicode_Check(repl)) {
        PyErr_SetString(PyExc_TypeError, "expected str replacement");
        return -1;
    }
    Py_ssize_t len = 0;
    const char* data = PyUnicode_AsUTF8AndSize(repl, &len);
    if (data == nullptr) {
        return -1;
    }
    *out = {data, static_cast<std::size_t>(len)};
    return 0;
}

// Applies parsed replacement segments to `out` — the single non-callable template
// application path, shared by sub and Match.expand so the two cannot diverge. `span(g)`
// gives group g's [begin, end) byte range in `subject_data`, or nullopt when the group
// did not participate (it then contributes nothing — REAL's sub/expand semantics).
template <typename SpanFn>
void apply_template(const std::vector<repl_segment>& segs, const char* subject_data,
                    SpanFn span, std::string& out) {
    for (const repl_segment& seg : segs) {
        if (seg.group < 0) {
            out.append(seg.literal);
        } else if (const auto range = span(static_cast<std::size_t>(seg.group))) {
            out.append(subject_data + range->first, range->second - range->first);
        }
    }
}

// Pure C++ (no Python C-API): applies a parsed non-callable sub template across the
// whole subject -- find_iter + append + apply_template, exactly sub_impl's non-callable
// loop. Safe to run with the GIL released (callable subs never reach here; the subject
// bytes are pinned by the caller's str/bytes ref). `done` receives the replacement count.
void run_template_sub(const real::regex& rx, const subject_view& sv,
                      const std::vector<repl_segment>& segments, Py_ssize_t count,
                      std::string& result, Py_ssize_t& done) {
    Py_ssize_t last = 0;
    done = 0;
    for (const auto& match : rx.find_iter(sv.view())) {
        if (count != 0 && done == count) {
            break;
        }
        result.append(sv.data + last, static_cast<std::size_t>(match.start()) - last);
        apply_template(segments, sv.data,
                       [&](std::size_t g) -> std::optional<std::pair<std::size_t, std::size_t>> {
                           const std::size_t s = match.start(g);
                           return s == real::npos
                                      ? std::nullopt
                                      : std::optional {std::pair {s, match.end(g)}};
                       },
                       result);
        last = static_cast<Py_ssize_t>(match.end());
        ++done;
    }
    result.append(sv.data + last, static_cast<std::size_t>(sv.len - last));
}

PyObject* sub_impl(PyObject* self, PyObject* args, PyObject* kwargs, bool with_count) {
    PatternObject* pat = as_pattern(self);
    PyObject* repl = nullptr;
    PyObject* string = nullptr;
    Py_ssize_t count = 0;
    static const char* const keywords[] = {"repl", "string", "count", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|n", const_cast<char**>(keywords),
                                     &repl, &string, &count)) {
        return nullptr;
    }
    subject_view sv;
    if (get_subject(pat->is_bytes, string, &sv) < 0) {
        return nullptr;
    }

    const bool callable = PyCallable_Check(repl) != 0;
    std::vector<repl_segment> segments;
    if (!callable) {
        std::string_view repl_text;
        if (get_repl_text(pat, repl, &repl_text) < 0 ||
            parse_template(pat, repl_text, segments) < 0) {
            return nullptr;
        }
    }

    std::string result;
    Py_ssize_t done = 0;
    if (!callable) {
        // Non-callable: the scan is pure C++ (run_template_sub). On a large subject release
        // the GIL so threads scan in parallel -- the only Python object is the final string,
        // built below under the GIL (no O(matches) build under the GIL, unlike findall/split).
        try {
            if (sv.len >= gil_release_collect_min_bytes) {
                const GilRelease unlocked;
                run_template_sub(*pat->rx, sv, segments, count, result, done);
            } else {
                run_template_sub(*pat->rx, sv, segments, count, result, done);  // small: keep the GIL
            }
        } catch (...) {
            return set_cpp_error();  // ~GilRelease re-acquired the GIL during unwinding
        }
    } else {
        // Callable: each replacement re-enters Python, so the GIL is held throughout.
        // sub has no pos/endpos, so each Match spans the whole subject (.pos=0, .endpos=len).
        // A C++ throw here (scratch/string growth under OOM) is converted, never propagated.
        try {
            const Py_ssize_t full_len = sv.char_is_byte ? sv.len : PyUnicode_GetLength(string);
            Py_ssize_t       last     = 0;
            for (const auto& match : pat->rx->find_iter(sv.view())) {
                if (count != 0 && done == count) {
                    break;
                }
                result.append(sv.data + last, static_cast<std::size_t>(match.start()) - last);
                PyObject* match_obj = make_match(pat, string, match, 0, full_len);
                if (match_obj == nullptr) {
                    return nullptr;
                }
                PyObject* value = PyObject_CallFunctionObjArgs(repl, match_obj, nullptr);
                Py_DECREF(match_obj);
                if (value == nullptr) {
                    return nullptr;
                }
                std::string_view text;
                if (get_repl_text(pat, value, &text) < 0) {
                    Py_DECREF(value);
                    return nullptr;
                }
                result.append(text);
                Py_DECREF(value);
                last = static_cast<Py_ssize_t>(match.end());
                ++done;
            }
            result.append(sv.data + last, static_cast<std::size_t>(sv.len - last));
        } catch (...) {
            return set_cpp_error();
        }
    }

    PyObject* out = pat->is_bytes != 0
                        ? PyBytes_FromStringAndSize(result.data(),
                                                    static_cast<Py_ssize_t>(result.size()))
                        : PyUnicode_DecodeUTF8(result.data(),
                                               static_cast<Py_ssize_t>(result.size()),
                                               nullptr);
    if (out == nullptr || !with_count) {
        return out;
    }
    PyObject* pair = Py_BuildValue("(Nn)", out, done);
    return pair;
}

// Match.expand(template): the string sub() would produce for THIS match. Shares the
// segment machinery (parse_template + apply_template) with sub, so they never diverge.
PyObject* Match_expand(PyObject* self, PyObject* template_arg) {
    MatchObject* match = as_match(self);
    PatternObject* pat = as_pattern(match->pattern);

    std::string_view repl_text;
    if (get_repl_text(pat, template_arg, &repl_text) < 0) {  // imposes str/bytes, like sub
        return nullptr;
    }
    std::vector<repl_segment> segments;
    if (parse_template(pat, repl_text, segments) < 0) {
        return nullptr;
    }
    subject_view sv;
    if (get_subject(pat->is_bytes, match->subject, &sv) < 0) {  // re-derive the UTF-8 view, like group_value
        return nullptr;
    }

    std::string result;
    apply_template(segments, sv.data,
                   [&](std::size_t g) -> std::optional<std::pair<std::size_t, std::size_t>> {
                       const Py_ssize_t start = (*match->byte_spans)[2 * g];
                       return start < 0
                                  ? std::nullopt
                                  : std::optional {std::pair {static_cast<std::size_t>(start),
                                                              static_cast<std::size_t>((*match->byte_spans)[(2 * g) + 1])}};
                   },
                   result);

    return pat->is_bytes != 0
               ? PyBytes_FromStringAndSize(result.data(), static_cast<Py_ssize_t>(result.size()))
               : PyUnicode_DecodeUTF8(result.data(), static_cast<Py_ssize_t>(result.size()), nullptr);
}

PyObject* Pattern_sub(PyObject* self, PyObject* args, PyObject* kwargs) {
    return sub_impl(self, args, kwargs, false);
}
PyObject* Pattern_subn(PyObject* self, PyObject* args, PyObject* kwargs) {
    return sub_impl(self, args, kwargs, true);
}

// ---------------------------------------------------------------------------
// Pattern properties
// ---------------------------------------------------------------------------

PyObject* Pattern_get_pattern(PyObject* self, void*) {
    return Py_NewRef(as_pattern(self)->pattern_obj);
}
PyObject* Pattern_get_engine(PyObject* /*self*/, void*) {
    // A natively compiled pattern always runs on the linear real engine. The fallback proxy (Python side,
    // policy=fallback) reports "re" instead — so pattern.engine tells the backend in either policy.
    return PyUnicode_FromString("real");
}
PyObject* Pattern_get_flags(PyObject* self, void*) {
    return PyLong_FromUnsignedLong(as_pattern(self)->py_flags);
}
PyObject* Pattern_get_groups(PyObject* self, void*) {
    return PyLong_FromSize_t(as_pattern(self)->rx->group_count());
}
PyObject* Pattern_get_groupindex(PyObject* self, void*) {
    PyObject* out = PyDict_New();
    if (out == nullptr) {
        return nullptr;
    }
    for (const auto& [name, index] : as_pattern(self)->rx->named_groups()) {
        PyObject* value = PyLong_FromSize_t(index);
        if (value == nullptr ||
            PyDict_SetItemString(out, std::string(name).c_str(), value) < 0) {
            Py_XDECREF(value);
            Py_DECREF(out);
            return nullptr;
        }
        Py_DECREF(value);
    }
    return out;
}

PyMethodDef pattern_methods[] = {
    {"match", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_match)),
     METH_VARARGS | METH_KEYWORDS,
     "match($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Try to match at position pos in the string.\n\n"
     "Args:\n"
     "    string (str or bytes): Text to match.\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "        Not a slice: \\A and ^ (without MULTILINE) still fail at pos > 0.\n"
     "    endpos (int): Where the string is treated as ending ($ and \\Z see it).\n\n"
     "Returns:\n"
     "    Match or None: Match object on success, None otherwise.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"fullmatch", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_fullmatch)),
     METH_VARARGS | METH_KEYWORDS,
     "fullmatch($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Try to match the whole region [pos, endpos) of the string.\n\n"
     "Args:\n"
     "    string (str or bytes): Text to match.\n"
     "    pos (int): Start. Character offset for str, byte offset for bytes.\n"
     "    endpos (int): End of the region the match must span ($ and \\Z see it).\n\n"
     "Returns:\n"
     "    Match or None: Match object on success, None otherwise.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"search", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_search)),
     METH_VARARGS | METH_KEYWORDS,
     "search($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Scan the region [pos, endpos) for the leftmost match.\n\n"
     "Args:\n"
     "    string (str or bytes): Text to search.\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "        Not a slice: \\A and ^ (without MULTILINE) still fail at pos > 0.\n"
     "    endpos (int): Where the string is treated as ending ($ and \\Z see it).\n\n"
     "Returns:\n"
     "    Match or None: Match object on success, None otherwise.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"findall", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_findall)),
     METH_VARARGS | METH_KEYWORDS,
     "findall($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Return all non-overlapping matches in the region [pos, endpos).\n\n"
     "Args:\n"
     "    string (str or bytes): Text to search.\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "        Not a slice: \\A and ^ (without MULTILINE) still fail at pos > 0.\n"
     "    endpos (int): Where the string is treated as ending; matches stop there.\n\n"
     "Returns:\n"
     "    list: List of strings, bytes, or tuples depending on groups.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"count_matches", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_count_matches)),
     METH_VARARGS | METH_KEYWORDS,
     "count_matches($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Count non-overlapping matches in [pos, endpos) without building Match objects.\n\n"
     "Extension beyond re.\n\n"
     "Matching-only: uses the trailing-LA class+ fast path when eligible (unlike\n"
     "finditer, which stays on the pure monomorphic walk). Prefer this over\n"
     "len(findall(...)) or sum(1 for _ in finditer(...)) for throughput.\n\n"
     "Args:\n"
     "    string (str or bytes): Text to search.\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "        Not a slice: \\A and ^ (without MULTILINE) still fail at pos > 0.\n"
     "    endpos (int): Where the string is treated as ending; counting stops there.\n\n"
     "Returns:\n"
     "    int: Number of non-overlapping matches.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"finditer", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_finditer)),
     METH_VARARGS | METH_KEYWORDS,
     "finditer($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Return an iterator yielding Match objects for the region [pos, endpos).\n\n"
     "Args:\n"
     "    string (str or bytes): Text to search.\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "        Not a slice: \\A and ^ (without MULTILINE) still fail at pos > 0.\n"
     "    endpos (int): Where the string is treated as ending; iteration stops there.\n\n"
     "Returns:\n"
     "    iterator: Iterator over all matches (each carries the region's .pos/.endpos).\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"split", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_split)),
     METH_VARARGS | METH_KEYWORDS,
     "split($self, string, maxsplit=0)\n--\n\n"
     "Split the string by occurrences of the pattern.\n\n"
     "Args:\n"
     "    string (str or bytes): Text to split.\n"
     "    maxsplit (int, optional): Maximum number of splits. 0 means no limit.\n\n"
     "Returns:\n"
     "    list: Substrings with captured groups interleaved.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"sub", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_sub)),
     METH_VARARGS | METH_KEYWORDS,
     "sub($self, repl, string, count=0)\n--\n\n"
     "Replace occurrences of the pattern in the string.\n\n"
     "Args:\n"
     "    repl (str, bytes, or callable): Replacement template or callable\n"
     "        accepting a Match object.\n"
     "    string (str or bytes): Text to modify.\n"
     "    count (int, optional): Maximum replacements. 0 means all.\n\n"
     "Returns:\n"
     "    str or bytes: Result after replacements.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {"subn", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_subn)),
     METH_VARARGS | METH_KEYWORDS,
     "subn($self, repl, string, count=0)\n--\n\n"
     "Replace occurrences and return the result plus the count.\n\n"
     "Args:\n"
     "    repl (str, bytes, or callable): Replacement template or callable.\n"
     "    string (str or bytes): Text to modify.\n"
     "    count (int, optional): Maximum replacements. 0 means all.\n\n"
     "Returns:\n"
     "    tuple: (result, number_of_substitutions).\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) -- guaranteed linear; never backtracks (ReDoS-safe)."},
    {nullptr, nullptr, 0, nullptr},
};

PyGetSetDef pattern_getset[] = {
    {"pattern", Pattern_get_pattern, nullptr, "The pattern string or bytes used for compilation.", nullptr},
    {"engine", Pattern_get_engine, nullptr,
     "The backend -- \"real\" (linear, ReDoS-safe) or \"re\" (fallback). Extension beyond re.",
     nullptr},
    {"flags", Pattern_get_flags, nullptr, "The compilation flags as passed to compile().", nullptr},
    {"groups", Pattern_get_groups, nullptr, "Number of capturing groups (excluding group 0).", nullptr},
    {"groupindex", Pattern_get_groupindex, nullptr, "Mapping from group name to group number.",
     nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

// re.Pattern is a value type: two patterns compiled from the same text with the same flags
// compare equal even when they are distinct objects. Equality keys on (pattern text, flags);
// the str/bytes type difference falls out of the pattern comparison. Comparing to a non-Pattern
// yields NotImplemented (no crash).
PyObject* Pattern_richcompare(PyObject* self, PyObject* other, int op) {
    if ((op != Py_EQ && op != Py_NE) ||
        PyObject_TypeCheck(other, reinterpret_cast<PyTypeObject*>(pattern_type)) == 0) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    PatternObject* lhs       = as_pattern(self);
    PatternObject* rhs       = as_pattern(other);
    const int      same_text = PyObject_RichCompareBool(lhs->pattern_obj, rhs->pattern_obj, Py_EQ);
    if (same_text < 0) {
        return nullptr;
    }
    const bool equal = (same_text == 1) && (lhs->py_flags == rhs->py_flags);
    return PyBool_FromLong(static_cast<long>(equal == (op == Py_EQ)));
}

// Consistent with Pattern_richcompare: hash((pattern, flags)).
Py_hash_t Pattern_hash(PyObject* self) {
    PatternObject* pat = as_pattern(self);
    PyObject*      key = Py_BuildValue("(Ok)", pat->pattern_obj, pat->py_flags);
    if (key == nullptr) {
        return -1;
    }
    const Py_hash_t hash = PyObject_Hash(key);
    Py_DECREF(key);
    return hash;
}

PyType_Slot pattern_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(Pattern_dealloc)},
    {Py_tp_richcompare, reinterpret_cast<void*>(Pattern_richcompare)},
    {Py_tp_hash, reinterpret_cast<void*>(Pattern_hash)},
    {Py_tp_methods, static_cast<void*>(pattern_methods)},
    {Py_tp_getset, static_cast<void*>(pattern_getset)},
    {Py_tp_doc,
     const_cast<char*>("A compiled REAL pattern, with the re.Pattern API.\n\n"
                       "Created by real.compile() -- not instantiable directly. Matching is\n"
                       "O(len(string)): guaranteed linear, never backtracks (ReDoS-safe).")},
    {0, nullptr},
};

PyType_Spec pattern_spec = {
    "real.Pattern",
    sizeof(PatternObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    pattern_slots,
};

// ---------------------------------------------------------------------------
// RegexSet: real::regex_set (Stage-2 fused which-matched) wired in directly, not through the
// C ABI (bindings/c) -- that ABI is byte-oriented and would force a str/bytes round-trip this
// binding does not need. real.RegexSet (real/__init__.py) is a thin Python wrapper around this
// internal type: it exists only so RegexSet keeps its historical public constructor shape
// (RegexSet(patterns, flags=0), directly instantiable) while Pattern/Match's convention --
// DISALLOW_INSTANTIATION plus a module-level factory -- still holds for the type this file
// owns. No span/group reporting: same scope as real::regex_set itself (rerun the individual
// Pattern if group data is needed).
// ---------------------------------------------------------------------------

struct RegexSetObject {
    PyObject_HEAD
    real::regex_set* set;
    int is_bytes;
};

RegexSetObject* as_regex_set(PyObject* obj) { return reinterpret_cast<RegexSetObject*>(obj); }

void RegexSet_dealloc(PyObject* self) {
    PyTypeObject* tp = Py_TYPE(self);
    RegexSetObject* rs = as_regex_set(self);
    delete rs->set;
    PyObject_Free(self);
    Py_DECREF(reinterpret_cast<PyObject*>(tp));
}

Py_ssize_t RegexSet_len(PyObject* self) {
    return static_cast<Py_ssize_t>(as_regex_set(self)->set->size());
}

// Parses (string, pos=0, endpos=None) -- same region convention as Pattern's run_region -- and
// converts to a byte region against `rs`'s own is_bytes (RegexSet has no per-pattern PatternObject
// to key off; is_bytes is a set-wide property, fixed at construction).
int regex_set_parse_region(RegexSetObject* rs, PyObject* args, PyObject* kwargs,
                           subject_view* sv, std::size_t* pos_byte, std::size_t* end_byte) {
    PyObject* string = nullptr;
    Py_ssize_t pos = 0;
    Py_ssize_t endpos = PY_SSIZE_T_MAX;
    static const char* const keywords[] = {"string", "pos", "endpos", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|nn", const_cast<char**>(keywords),
                                     &string, &pos, &endpos)) {
        return -1;
    }
    // Whether the CALLER passed endpos at all, checked before it gets clamped down to char_len
    // below (which would otherwise erase the distinction): real::regex_set::matches/is_match only
    // take the fused single-pass DFA route when endpos == real::npos EXACTLY (its own "no region"
    // sentinel) -- resolving an unspecified endpos to the concrete text length here would silently
    // force every call onto the slower N-walks path, whether or not the caller asked for a region.
    const bool endpos_given = (endpos != PY_SSIZE_T_MAX);
    if (get_subject(rs->is_bytes, string, sv) < 0) {
        return -1;
    }
    const Py_ssize_t char_len = sv->char_is_byte ? sv->len : PyUnicode_GetLength(string);
    pos = std::clamp(pos, Py_ssize_t {0}, char_len);
    *pos_byte = char_to_byte(*sv, pos);
    if (endpos_given) {
        endpos = std::clamp(endpos, Py_ssize_t {0}, char_len);
        *end_byte = char_to_byte(*sv, endpos);
    } else {
        *end_byte = real::npos;
    }
    return 0;
}

PyObject* RegexSet_is_match(PyObject* self, PyObject* args, PyObject* kwargs) {
    RegexSetObject* rs = as_regex_set(self);
    subject_view sv;
    std::size_t pos_byte = 0;
    std::size_t end_byte = 0;
    if (regex_set_parse_region(rs, args, kwargs, &sv, &pos_byte, &end_byte) < 0) {
        return nullptr;
    }
    try {
        const bool hit = rs->set->is_match(sv.view(), pos_byte, end_byte);
        if (hit) {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    } catch (...) {
        return set_cpp_error();
    }
}

PyObject* RegexSet_matches(PyObject* self, PyObject* args, PyObject* kwargs) {
    RegexSetObject* rs = as_regex_set(self);
    subject_view sv;
    std::size_t pos_byte = 0;
    std::size_t end_byte = 0;
    if (regex_set_parse_region(rs, args, kwargs, &sv, &pos_byte, &end_byte) < 0) {
        return nullptr;
    }
    std::vector<bool> hit;
    try {
        hit = rs->set->matches(sv.view(), pos_byte, end_byte);
    } catch (...) {
        return set_cpp_error();
    }
    PyObject* list = PyList_New(static_cast<Py_ssize_t>(hit.size()));
    if (list == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < hit.size(); ++i) {
        PyObject* b = hit[i] ? Py_True : Py_False;
        Py_INCREF(b);  // PyList_SetItem steals the reference (limited-API safe; no _SET_ITEM macro)
        if (PyList_SetItem(list, static_cast<Py_ssize_t>(i), b) < 0) {
            Py_DECREF(list);
            return nullptr;
        }
    }
    return list;
}

PyObject* RegexSet_which(PyObject* self, PyObject* args, PyObject* kwargs) {
    RegexSetObject* rs = as_regex_set(self);
    subject_view sv;
    std::size_t pos_byte = 0;
    std::size_t end_byte = 0;
    if (regex_set_parse_region(rs, args, kwargs, &sv, &pos_byte, &end_byte) < 0) {
        return nullptr;
    }
    std::vector<std::size_t> ids;
    try {
        ids = rs->set->which(sv.view(), pos_byte, end_byte);
    } catch (...) {
        return set_cpp_error();
    }
    PyObject* list = PyList_New(static_cast<Py_ssize_t>(ids.size()));
    if (list == nullptr) {
        return nullptr;
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        PyObject* n = PyLong_FromSize_t(ids[i]);
        if (n == nullptr) {
            Py_DECREF(list);
            return nullptr;
        }
        // PyList_SetItem always consumes the `n` reference, success or failure -- never decref
        // it separately (i stays within [0, size), so failure is not expected in practice).
        if (PyList_SetItem(list, static_cast<Py_ssize_t>(i), n) < 0) {
            Py_DECREF(list);
            return nullptr;
        }
    }
    return list;
}

PyMethodDef regex_set_methods[] = {
    {"is_match", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(RegexSet_is_match)),
     METH_VARARGS | METH_KEYWORDS,
     "is_match($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "True if any pattern matches (stops at the first hit).\n\n"
     "Args:\n"
     "    string (str or bytes): Text to search (the set's own str/bytes type).\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "    endpos (int): Where the string is treated as ending.\n\n"
     "Returns:\n"
     "    bool: True if at least one pattern matches.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) per pattern -- guaranteed linear; never backtracks\n"
     "    (ReDoS-safe)."},
    {"matches", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(RegexSet_matches)),
     METH_VARARGS | METH_KEYWORDS,
     "matches($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Which patterns match at least once (construction-order list of bool).\n\n"
     "Args:\n"
     "    string (str or bytes): Text to search (the set's own str/bytes type).\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "    endpos (int): Where the string is treated as ending.\n\n"
     "Returns:\n"
     "    list: One bool per pattern, in construction order.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) per pattern -- guaranteed linear; never backtracks\n"
     "    (ReDoS-safe)."},
    {"which", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(RegexSet_which)),
     METH_VARARGS | METH_KEYWORDS,
     "which($self, string, pos=0, endpos=sys.maxsize)\n--\n\n"
     "Indices of matching patterns (ascending, construction order).\n\n"
     "Args:\n"
     "    string (str or bytes): Text to search (the set's own str/bytes type).\n"
     "    pos (int): Where to start. Character offset for str, byte offset for bytes.\n"
     "    endpos (int): Where the string is treated as ending.\n\n"
     "Returns:\n"
     "    list: Indices of the matching patterns, ascending.\n\n"
     "Complexity:\n"
     "    Matching is O(len(string)) per pattern -- guaranteed linear; never backtracks\n"
     "    (ReDoS-safe)."},
    {nullptr, nullptr, 0, nullptr},
};

PyType_Slot regex_set_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(RegexSet_dealloc)},
    {Py_sq_length, reinterpret_cast<void*>(RegexSet_len)},
    {Py_tp_methods, static_cast<void*>(regex_set_methods)},
    {Py_tp_doc,
     const_cast<char*>("Native multi-pattern which-matched set (backs real.RegexSet).\n\n"
                       "Created by real._compile_set() -- not instantiable directly. Extension\n"
                       "beyond re.")},
    {0, nullptr},
};

PyType_Spec regex_set_spec = {
    "real._RegexSet",
    sizeof(RegexSetObject),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    regex_set_slots,
};

// real._compile_set(patterns, flags=0) -> _RegexSet -- the factory real.RegexSet.__init__ calls.
// Every pattern must share the same str-vs-bytes-ness (mixed raises, like mixing them across
// separate real.compile calls would eventually surface anyway) -- checked once here rather than
// per-member, so the error names the whole set, not an arbitrary first divergent index.
PyObject* real_compile_set(PyObject*, PyObject* args, PyObject* kwargs) {
    PyObject* patterns_obj = nullptr;
    unsigned long py_flags = 0;
    static const char* const keywords[] = {"patterns", "flags", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|k", const_cast<char**>(keywords),
                                     &patterns_obj, &py_flags)) {
        return nullptr;
    }
    // Iterator protocol, not PySequence_Fast + its _GET_ITEM/_GET_SIZE macros: those macros reach
    // into the object's internals directly and are excluded from the limited API (Py_LIMITED_API
    // above) -- and the iterator protocol also preserves the original pure-Python RegexSet's
    // "any iterable of patterns" contract (a generator included), not just a sequence.
    PyObject* iterator = PyObject_GetIter(patterns_obj);
    if (iterator == nullptr) {
        return nullptr;
    }
    std::vector<std::string> owned;
    int is_bytes = -1;
    PyObject* p = nullptr;
    while ((p = PyIter_Next(iterator)) != nullptr) {
        const char* data = nullptr;
        Py_ssize_t len = 0;
        int this_is_bytes = 0;
        if (PyUnicode_Check(p)) {
            data = PyUnicode_AsUTF8AndSize(p, &len);
            if (data == nullptr) {
                Py_DECREF(p);
                Py_DECREF(iterator);
                return nullptr;
            }
            owned.emplace_back(data, static_cast<std::size_t>(len));
        } else if (PyBytes_Check(p)) {
            char* raw = nullptr;
            if (PyBytes_AsStringAndSize(p, &raw, &len) < 0) {
                Py_DECREF(p);
                Py_DECREF(iterator);
                return nullptr;
            }
            this_is_bytes = 1;
            owned.emplace_back(raw, static_cast<std::size_t>(len));
        } else {
            Py_DECREF(p);
            Py_DECREF(iterator);
            PyErr_SetString(PyExc_TypeError, "patterns must be str or bytes");
            return nullptr;
        }
        Py_DECREF(p);
        if (is_bytes == -1) {
            is_bytes = this_is_bytes;
        } else if (is_bytes != this_is_bytes) {
            Py_DECREF(iterator);
            PyErr_SetString(PyExc_TypeError, "cannot mix str and bytes patterns in a RegexSet");
            return nullptr;
        }
    }
    Py_DECREF(iterator);
    if (PyErr_Occurred() != 0) {
        return nullptr;  // the iterator itself raised mid-iteration
    }

    if ((py_flags & (PYFLAG_LOCALE | PYFLAG_DEBUG)) != 0) {
        set_error("re.L and re.DEBUG are not supported by real");
        return nullptr;
    }
    constexpr unsigned long known = PYFLAG_IGNORECASE | PYFLAG_MULTILINE | PYFLAG_DOTALL |
                                    PYFLAG_UNICODE | PYFLAG_ASCII | PYFLAG_VERBOSE;
    if ((py_flags & ~known) != 0) {
        set_error("unknown flag passed to real.compile_set");
        return nullptr;
    }
    real::flags compile_flags = real::flags::none;
    if (is_bytes == 1) {
        compile_flags = compile_flags | real::flags::bytes;
    }
    if ((py_flags & PYFLAG_IGNORECASE) != 0) {
        compile_flags = compile_flags | real::flags::icase;
    }
    if ((py_flags & PYFLAG_MULTILINE) != 0) {
        compile_flags = compile_flags | real::flags::multiline;
    }
    if ((py_flags & PYFLAG_DOTALL) != 0) {
        compile_flags = compile_flags | real::flags::dotall;
    }
    if ((py_flags & PYFLAG_VERBOSE) != 0) {
        compile_flags = compile_flags | real::flags::verbose;
    }
    if ((py_flags & PYFLAG_ASCII) != 0) {
        compile_flags = compile_flags | real::flags::ascii;
    }

    std::vector<std::string_view> views;
    views.reserve(owned.size());
    for (const std::string& s : owned) {
        views.emplace_back(s);
    }

    real::regex_set* set = nullptr;
    try {
        set = new real::regex_set(std::span<const std::string_view> {views}, compile_flags);
    } catch (const real::regex_error& ex) {
        set_error(ex.what());
        return nullptr;
    } catch (const std::bad_alloc&) {
        return PyErr_NoMemory();
    } catch (const std::exception& ex) {
        PyErr_SetString(PyExc_RuntimeError, ex.what());
        return nullptr;
    }

    auto* obj = PyObject_New(RegexSetObject, reinterpret_cast<PyTypeObject*>(regex_set_type));
    if (obj == nullptr) {
        delete set;
        return nullptr;
    }
    obj->set = set;
    obj->is_bytes = (is_bytes == 1) ? 1 : 0;
    return reinterpret_cast<PyObject*>(obj);
}

// ---------------------------------------------------------------------------
// compile()
// ---------------------------------------------------------------------------

PyObject* real_compile(PyObject*, PyObject* args, PyObject* kwargs) {
    PyObject* pattern = nullptr;
    unsigned long py_flags = 0;
    static const char* const keywords[] = {"pattern", "flags", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|k", const_cast<char**>(keywords),
                                     &pattern, &py_flags)) {
        return nullptr;
    }

    const char* data = nullptr;
    Py_ssize_t len = 0;
    int is_bytes = 0;
    if (PyUnicode_Check(pattern)) {
        data = PyUnicode_AsUTF8AndSize(pattern, &len);
        if (data == nullptr) {
            return nullptr;
        }
    } else if (PyBytes_Check(pattern)) {
        char* raw = nullptr;
        if (PyBytes_AsStringAndSize(pattern, &raw, &len) < 0) {
            return nullptr;
        }
        data = raw;
        is_bytes = 1;
    } else {
        PyErr_SetString(PyExc_TypeError, "pattern must be a str or bytes");
        return nullptr;
    }

    if ((py_flags & (PYFLAG_LOCALE | PYFLAG_DEBUG)) != 0) {
        set_error("re.L and re.DEBUG are not supported by real");
        return nullptr;
    }
    constexpr unsigned long known = PYFLAG_IGNORECASE | PYFLAG_MULTILINE | PYFLAG_DOTALL |
                                    PYFLAG_UNICODE | PYFLAG_ASCII | PYFLAG_VERBOSE;
    if ((py_flags & ~known) != 0) {
        set_error("unknown flag passed to real.compile");
        return nullptr;
    }
    real::flags compile_flags = real::flags::none;
    if (is_bytes != 0) {
        compile_flags = compile_flags | real::flags::bytes;  // raw-byte semantics, like re on bytes
    }
    if ((py_flags & PYFLAG_IGNORECASE) != 0) {
        compile_flags = compile_flags | real::flags::icase;
    }
    if ((py_flags & PYFLAG_MULTILINE) != 0) {
        compile_flags = compile_flags | real::flags::multiline;
    }
    if ((py_flags & PYFLAG_DOTALL) != 0) {
        compile_flags = compile_flags | real::flags::dotall;
    }
    if ((py_flags & PYFLAG_VERBOSE) != 0) {
        compile_flags = compile_flags | real::flags::verbose;
    }
    if ((py_flags & PYFLAG_ASCII) != 0) {
        compile_flags = compile_flags | real::flags::ascii;  // re.A: keep \d \w \s and icase ASCII
    }
    // PYFLAG_UNICODE (re.U) stays a no-op: Unicode is already the str-mode default.

    real::regex* rx = nullptr;
    try {
        rx = new real::regex(std::string_view(data, static_cast<std::size_t>(len)), compile_flags);
    } catch (const real::regex_error& ex) {
        set_error(ex.what());
        return nullptr;
    } catch (const std::bad_alloc&) {
        return PyErr_NoMemory();
    } catch (const std::exception& ex) {
        PyErr_SetString(PyExc_RuntimeError, ex.what());
        return nullptr;
    }

    auto* obj = PyObject_New(PatternObject, reinterpret_cast<PyTypeObject*>(pattern_type));
    if (obj == nullptr) {
        delete rx;
        return nullptr;
    }
    obj->pattern_obj = Py_NewRef(pattern);
    obj->rx = rx;
    obj->py_flags = py_flags;
    obj->is_bytes = is_bytes;
    return reinterpret_cast<PyObject*>(obj);
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

PyMethodDef module_methods[] = {
    {"compile", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(real_compile)),
     METH_VARARGS | METH_KEYWORDS,
     "compile(pattern, flags=0)\n--\n\n"
     "Compile a regular expression pattern into a real.Pattern object.\n\n"
     "Args:\n"
     "    pattern (str or bytes): The regular expression pattern.\n"
     "    flags (int, optional): Bitwise OR of re-compatible flags. Defaults to 0.\n\n"
     "Returns:\n"
     "    Pattern: Compiled pattern object.\n\n"
     "Raises:\n"
     "    error: If the pattern is invalid or unsupported."},
    {"_compile_set", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(real_compile_set)),
     METH_VARARGS | METH_KEYWORDS,
     "_compile_set(patterns, flags=0)\n--\n\n"
     "Compile patterns into an internal native set object (RegexSet's own factory --\n"
     "not public API; use real.RegexSet)."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT, "_real",
    "REAL regex engine C++ core (linear-time Thompson NFA simulation).", -1,
    module_methods,        nullptr, nullptr,                         nullptr, nullptr,
};

}  // namespace

PyMODINIT_FUNC PyInit__real() {  // PyMODINIT_FUNC already says extern "C"
    PyObject* module = PyModule_Create(&module_def);
    if (module == nullptr) {
        return nullptr;
    }
    // Subclass re.error so `except re.error:` catches REAL's errors too (re-compatibility).
    // PyErr_NewException does not steal the base reference. If re is unavailable, fall back
    // to a standalone exception (base = Exception).
    PyObject* re_mod = PyImport_ImportModule("re");
    PyObject* re_err = re_mod != nullptr ? PyObject_GetAttrString(re_mod, "error") : nullptr;
    if (re_err == nullptr) {
        PyErr_Clear();
    }
    error_type = PyErr_NewExceptionWithDoc(
        "real.error",
        "Exception raised when a pattern is invalid or unsupported.\n\n"
        "Subclasses re.error when re is available, so `except re.error:` also\n"
        "catches REAL's errors.",
        re_err, nullptr);
    Py_XDECREF(re_err);
    Py_XDECREF(re_mod);
    pattern_type = PyType_FromSpec(&pattern_spec);
    match_type = PyType_FromSpec(&match_spec);
    match_iterator_type = PyType_FromSpec(&match_iterator_spec);  // internal: created, not exposed
    regex_set_type = PyType_FromSpec(&regex_set_spec);            // internal: created, not exposed
    if (error_type == nullptr || pattern_type == nullptr || match_type == nullptr ||
        match_iterator_type == nullptr || regex_set_type == nullptr ||
        PyModule_AddObject(module, "error", Py_NewRef(error_type)) < 0 ||
        PyModule_AddObject(module, "Pattern", Py_NewRef(pattern_type)) < 0 ||
        PyModule_AddObject(module, "Match", Py_NewRef(match_type)) < 0) {
        Py_DECREF(module);
        return nullptr;
    }
    return module;
}
