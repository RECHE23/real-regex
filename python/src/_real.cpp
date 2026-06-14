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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Module state (single-phase init: simple globals)
// ---------------------------------------------------------------------------

PyObject* error_type = nullptr;    // real.error
PyObject* pattern_type = nullptr;  // real.Pattern
PyObject* match_type = nullptr;    // real.Match

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
    // Reusable VM scratch: single runs allocate nothing once warm. Safe
    // because the GIL serializes access (the binding never releases it).
    real::detail::pike_state* scratch;
    std::vector<std::size_t>* scratch_slots;
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
};

PatternObject* as_pattern(PyObject* o) { return reinterpret_cast<PatternObject*>(o); }
MatchObject* as_match(PyObject* o) { return reinterpret_cast<MatchObject*>(o); }

void Pattern_dealloc(PyObject* self) {
    PyTypeObject* tp = Py_TYPE(self);
    PatternObject* p = as_pattern(self);
    delete p->rx;
    delete p->scratch;
    delete p->scratch_slots;
    Py_XDECREF(p->pattern_obj);
    PyObject_Free(self);
    Py_DECREF(reinterpret_cast<PyObject*>(tp));
}

void Match_dealloc(PyObject* self) {
    PyTypeObject* tp = Py_TYPE(self);
    MatchObject* m = as_match(self);
    delete m->byte_spans;
    delete m->char_spans;
    Py_XDECREF(m->subject);
    Py_XDECREF(m->pattern);
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

int get_subject(PatternObject* pat, PyObject* obj, subject_view* out) {
    if (pat->is_bytes != 0) {
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
PyObject* slice_subject(PatternObject* pat, const subject_view& sv, Py_ssize_t s,
                        Py_ssize_t e) {
    if (pat->is_bytes != 0) {
        return PyBytes_FromStringAndSize(sv.data + s, e - s);
    }
    return PyUnicode_DecodeUTF8(sv.data + s, e - s, nullptr);
}

PyObject* empty_like(PatternObject* pat) {
    return pat->is_bytes != 0 ? PyBytes_FromStringAndSize("", 0)
                              : PyUnicode_FromStringAndSize("", 0);
}

// ---------------------------------------------------------------------------
// Match construction
// ---------------------------------------------------------------------------

PyObject* make_match(PatternObject* pat, PyObject* subject, const subject_view& sv,
                     const auto& m) {
    auto* obj = PyObject_New(MatchObject, reinterpret_cast<PyTypeObject*>(match_type));
    if (obj == nullptr) {
        return nullptr;
    }
    obj->subject = Py_NewRef(subject);
    obj->pattern = Py_NewRef(reinterpret_cast<PyObject*>(pat));
    obj->byte_spans = new std::vector<Py_ssize_t>(2 * m.size());
    obj->char_spans = new std::vector<Py_ssize_t>();
    auto& bytes = *obj->byte_spans;
    for (std::size_t g = 0; g < m.size(); ++g) {
        const std::size_t s = m.start(g);
        bytes[2 * g] = s == real::npos ? -1 : static_cast<Py_ssize_t>(s);
        bytes[(2 * g) + 1] = s == real::npos ? -1 : static_cast<Py_ssize_t>(m.end(g));
    }
    compute_char_spans(sv, bytes, *obj->char_spans);
    return reinterpret_cast<PyObject*>(obj);
}

// ---------------------------------------------------------------------------
// Match methods
// ---------------------------------------------------------------------------

// Group argument -> group number, or -1 with an exception set.
Py_ssize_t resolve_group(MatchObject* m, PyObject* arg) {
    PatternObject* pat = as_pattern(m->pattern);
    if (PyLong_Check(arg)) {
        const Py_ssize_t g = PyLong_AsSsize_t(arg);
        if (g == -1 && PyErr_Occurred() != nullptr) {
            return -1;
        }
        if (g < 0 || static_cast<std::size_t>(g) > pat->rx->group_count()) {
            PyErr_SetString(PyExc_IndexError, "no such group");
            return -1;
        }
        return g;
    }
    if (PyUnicode_Check(arg)) {
        Py_ssize_t len = 0;
        const char* name = PyUnicode_AsUTF8AndSize(arg, &len);
        if (name == nullptr) {
            return -1;
        }
        const std::size_t g =
            pat->rx->group_index(std::string_view(name, static_cast<std::size_t>(len)));
        if (g == real::npos) {
            PyErr_SetString(PyExc_IndexError, "no such group");
            return -1;
        }
        return static_cast<Py_ssize_t>(g);
    }
    PyErr_SetString(PyExc_IndexError, "no such group");
    return -1;
}

PyObject* group_value(MatchObject* m, Py_ssize_t g, PyObject* default_value) {
    PatternObject* pat = as_pattern(m->pattern);
    const Py_ssize_t s = (*m->byte_spans)[2 * g];
    if (s < 0) {
        return Py_NewRef(default_value);
    }
    const Py_ssize_t e = (*m->byte_spans)[(2 * g) + 1];
    subject_view sv;
    if (get_subject(pat, m->subject, &sv) < 0) {
        return nullptr;
    }
    return slice_subject(pat, sv, s, e);
}

PyObject* Match_group(PyObject* self, PyObject* args) {
    MatchObject* m = as_match(self);
    const Py_ssize_t nargs = PyTuple_Size(args);
    if (nargs == 0) {
        return group_value(m, 0, Py_None);
    }
    if (nargs == 1) {
        const Py_ssize_t g = resolve_group(m, PyTuple_GetItem(args, 0));
        return g < 0 ? nullptr : group_value(m, g, Py_None);
    }
    PyObject* out = PyTuple_New(nargs);
    if (out == nullptr) {
        return nullptr;
    }
    for (Py_ssize_t i = 0; i < nargs; ++i) {
        const Py_ssize_t g = resolve_group(m, PyTuple_GetItem(args, i));
        PyObject* value = g < 0 ? nullptr : group_value(m, g, Py_None);
        if (value == nullptr) {
            Py_DECREF(out);
            return nullptr;
        }
        PyTuple_SetItem(out, i, value);
    }
    return out;
}

PyObject* Match_subscript(PyObject* self, PyObject* key) {
    MatchObject* m = as_match(self);
    const Py_ssize_t g = resolve_group(m, key);
    return g < 0 ? nullptr : group_value(m, g, Py_None);
}

PyObject* Match_groups(PyObject* self, PyObject* args, PyObject* kwargs) {
    MatchObject* m = as_match(self);
    PyObject* default_value = Py_None;
    static const char* const keywords[] = {"default", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O",
                                     const_cast<char**>(keywords), &default_value)) {
        return nullptr;
    }
    const auto n = static_cast<Py_ssize_t>(as_pattern(m->pattern)->rx->group_count());
    PyObject* out = PyTuple_New(n);
    if (out == nullptr) {
        return nullptr;
    }
    for (Py_ssize_t g = 1; g <= n; ++g) {
        PyObject* value = group_value(m, g, default_value);
        if (value == nullptr) {
            Py_DECREF(out);
            return nullptr;
        }
        PyTuple_SetItem(out, g - 1, value);
    }
    return out;
}

PyObject* Match_groupdict(PyObject* self, PyObject* args, PyObject* kwargs) {
    MatchObject* m = as_match(self);
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
    for (const auto& [name, index] : as_pattern(m->pattern)->rx->named_groups()) {
        PyObject* value = group_value(m, static_cast<Py_ssize_t>(index), default_value);
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

enum class span_part : std::uint8_t { start, end, both };

PyObject* match_position(PyObject* self, PyObject* args, span_part part) {
    MatchObject* m = as_match(self);
    PyObject* arg = nullptr;
    if (!PyArg_ParseTuple(args, "|O", &arg)) {
        return nullptr;
    }
    Py_ssize_t g = 0;
    if (arg != nullptr) {
        g = resolve_group(m, arg);
        if (g < 0) {
            return nullptr;
        }
    }
    const Py_ssize_t s = (*m->char_spans)[2 * g];
    const Py_ssize_t e = (*m->char_spans)[(2 * g) + 1];
    switch (part) {
        case span_part::start:
            return PyLong_FromSsize_t(s);
        case span_part::end:
            return PyLong_FromSsize_t(e);
        case span_part::both:
            return Py_BuildValue("(nn)", s, e);
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

PyMethodDef match_methods[] = {
    {"group", Match_group, METH_VARARGS, "Return one or more subgroups."},
    {"groups", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Match_groups)),
     METH_VARARGS | METH_KEYWORDS, "Return a tuple of all subgroups."},
    {"groupdict", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Match_groupdict)),
     METH_VARARGS | METH_KEYWORDS, "Return a dict of named subgroups."},
    {"start", Match_start, METH_VARARGS, "Index of the start of a group."},
    {"end", Match_end, METH_VARARGS, "Index of the end of a group."},
    {"span", Match_span, METH_VARARGS, "(start, end) of a group."},
    {nullptr, nullptr, 0, nullptr},
};

PyGetSetDef match_getset[] = {
    {"re", Match_get_re, nullptr, "The pattern object.", nullptr},
    {"string", Match_get_string, nullptr, "The searched string.", nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyType_Slot match_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(Match_dealloc)},
    {Py_tp_methods, static_cast<void*>(match_methods)},
    {Py_tp_getset, static_cast<void*>(match_getset)},
    {Py_mp_subscript, reinterpret_cast<void*>(Match_subscript)},
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

PyObject* run_single(PyObject* self, PyObject* string, real::detail::run_mode mode) {
    PatternObject* pat = as_pattern(self);
    subject_view sv;
    if (get_subject(pat, string, &sv) < 0) {
        return nullptr;
    }
    // Drive the VM directly with the pattern's reusable scratch: a single
    // call allocates nothing until a match is found.
    const real::detail::program_view prog = pat->rx->raw_program();
    real::detail::pike_vm vm(prog, *pat->scratch);
    if (!vm.run(sv.view(), 0, mode, *pat->scratch_slots)) {
        Py_RETURN_NONE;
    }
    const real::match_result m(sv.view(), *pat->scratch_slots, true, pat->rx->pattern(),
                               prog.names);
    return make_match(pat, string, sv, m);
}

PyObject* Pattern_match(PyObject* self, PyObject* string) {
    return run_single(self, string, real::detail::run_mode::prefix);
}
PyObject* Pattern_fullmatch(PyObject* self, PyObject* string) {
    return run_single(self, string, real::detail::run_mode::full);
}
PyObject* Pattern_search(PyObject* self, PyObject* string) {
    return run_single(self, string, real::detail::run_mode::search);
}

// ---------------------------------------------------------------------------
// Pattern: findall / finditer / split
// ---------------------------------------------------------------------------

PyObject* Pattern_findall(PyObject* self, PyObject* string) {
    PatternObject* pat = as_pattern(self);
    subject_view sv;
    if (get_subject(pat, string, &sv) < 0) {
        return nullptr;
    }
    PyObject* out = PyList_New(0);
    if (out == nullptr) {
        return nullptr;
    }
    const std::size_t ngroups = pat->rx->group_count();
    for (const auto& m : pat->rx->find_iter(sv.view())) {
        PyObject* item = nullptr;
        if (ngroups == 0) {
            item = slice_subject(pat, sv, static_cast<Py_ssize_t>(m.start()),
                                 static_cast<Py_ssize_t>(m.end()));
        } else if (ngroups == 1) {
            item = m.start(1) == real::npos
                       ? empty_like(pat)
                       : slice_subject(pat, sv, static_cast<Py_ssize_t>(m.start(1)),
                                       static_cast<Py_ssize_t>(m.end(1)));
        } else {
            item = PyTuple_New(static_cast<Py_ssize_t>(ngroups));
            for (std::size_t g = 1; item != nullptr && g <= ngroups; ++g) {
                PyObject* part =
                    m.start(g) == real::npos
                        ? empty_like(pat)
                        : slice_subject(pat, sv, static_cast<Py_ssize_t>(m.start(g)),
                                        static_cast<Py_ssize_t>(m.end(g)));
                if (part == nullptr) {
                    Py_DECREF(item);
                    item = nullptr;
                    break;
                }
                PyTuple_SetItem(item, static_cast<Py_ssize_t>(g) - 1, part);
            }
        }
        if (item == nullptr || PyList_Append(out, item) < 0) {
            Py_XDECREF(item);
            Py_DECREF(out);
            return nullptr;
        }
        Py_DECREF(item);
    }
    return out;
}

PyObject* Pattern_finditer(PyObject* self, PyObject* string) {
    PatternObject* pat = as_pattern(self);
    subject_view sv;
    if (get_subject(pat, string, &sv) < 0) {
        return nullptr;
    }
    PyObject* matches = PyList_New(0);
    if (matches == nullptr) {
        return nullptr;
    }
    for (const auto& m : pat->rx->find_iter(sv.view())) {
        PyObject* obj = make_match(pat, string, sv, m);
        if (obj == nullptr || PyList_Append(matches, obj) < 0) {
            Py_XDECREF(obj);
            Py_DECREF(matches);
            return nullptr;
        }
        Py_DECREF(obj);
    }
    PyObject* it = PyObject_GetIter(matches);
    Py_DECREF(matches);
    return it;
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
    if (get_subject(pat, string, &sv) < 0) {
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
    Py_ssize_t last = 0;
    Py_ssize_t done = 0;
    for (const auto& m : pat->rx->find_iter(sv.view())) {
        if (maxsplit != 0 && done == maxsplit) {
            break;
        }
        if (!append(slice_subject(pat, sv, last, static_cast<Py_ssize_t>(m.start())))) {
            Py_DECREF(out);
            return nullptr;
        }
        for (std::size_t g = 1; g <= pat->rx->group_count(); ++g) {
            PyObject* piece =
                m.start(g) == real::npos
                    ? Py_NewRef(Py_None)
                    : slice_subject(pat, sv, static_cast<Py_ssize_t>(m.start(g)),
                                    static_cast<Py_ssize_t>(m.end(g)));
            if (!append(piece)) {
                Py_DECREF(out);
                return nullptr;
            }
        }
        last = static_cast<Py_ssize_t>(m.end());
        ++done;
    }
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

// Parses Python's replacement template syntax (\1, \g<name>, escapes).
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
        const char c = repl[i];
        if (c != '\\') {
            literal.push_back(c);
            ++i;
            continue;
        }
        ++i;
        if (i >= repl.size()) {
            set_error("bad escape (end of pattern)");
            return -1;
        }
        const char d = repl[i];
        if (d >= '0' && d <= '9') {
            Py_ssize_t group = 0;  // \0 .. \99
            std::size_t digits = 0;
            while (i < repl.size() && digits < 2 && repl[i] >= '0' && repl[i] <= '9') {
                group = (group * 10) + (repl[i] - '0');
                ++i;
                ++digits;
            }
            if (static_cast<std::size_t>(group) > pat->rx->group_count()) {
                set_error("invalid group reference");
                return -1;
            }
            flush_group(group);
            continue;
        }
        if (d == 'g') {
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
                const std::size_t g = pat->rx->group_index(name);
                if (g == real::npos) {
                    set_error("unknown group name");
                    return -1;
                }
                group = static_cast<Py_ssize_t>(g);
            }
            if (static_cast<std::size_t>(group) > pat->rx->group_count()) {
                set_error("invalid group reference");
                return -1;
            }
            flush_group(group);
            continue;
        }
        ++i;
        switch (d) {
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
                if ((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z')) {
                    set_error("bad escape in replacement");
                    return -1;
                }
                literal.push_back('\\');
                literal.push_back(d);
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
    if (get_subject(pat, string, &sv) < 0) {
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
    Py_ssize_t last = 0;
    Py_ssize_t done = 0;
    for (const auto& m : pat->rx->find_iter(sv.view())) {
        if (count != 0 && done == count) {
            break;
        }
        result.append(sv.data + last, static_cast<std::size_t>(m.start()) - last);
        if (callable) {
            PyObject* match_obj = make_match(pat, string, sv, m);
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
        } else {
            for (const repl_segment& seg : segments) {
                if (seg.group < 0) {
                    result.append(seg.literal);
                } else if (m.start(static_cast<std::size_t>(seg.group)) != real::npos) {
                    const auto g = static_cast<std::size_t>(seg.group);
                    result.append(sv.data + m.start(g), m.end(g) - m.start(g));
                }
            }
        }
        last = static_cast<Py_ssize_t>(m.end());
        ++done;
    }
    result.append(sv.data + last, static_cast<std::size_t>(sv.len - last));

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
    {"match", Pattern_match, METH_O, "Anchored match at the start."},
    {"fullmatch", Pattern_fullmatch, METH_O, "Match the entire string."},
    {"search", Pattern_search, METH_O, "Leftmost match anywhere."},
    {"findall", Pattern_findall, METH_O, "All matches, re.findall semantics."},
    {"finditer", Pattern_finditer, METH_O, "Iterator over all Match objects."},
    {"split", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_split)),
     METH_VARARGS | METH_KEYWORDS, "Split by pattern occurrences."},
    {"sub", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_sub)),
     METH_VARARGS | METH_KEYWORDS, "Replace matches."},
    {"subn", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(Pattern_subn)),
     METH_VARARGS | METH_KEYWORDS, "Replace matches, returning (result, count)."},
    {nullptr, nullptr, 0, nullptr},
};

PyGetSetDef pattern_getset[] = {
    {"pattern", Pattern_get_pattern, nullptr, "The pattern text.", nullptr},
    {"flags", Pattern_get_flags, nullptr, "The compilation flags.", nullptr},
    {"groups", Pattern_get_groups, nullptr, "Number of capturing groups.", nullptr},
    {"groupindex", Pattern_get_groupindex, nullptr, "Mapping name -> group number.",
     nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyType_Slot pattern_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void*>(Pattern_dealloc)},
    {Py_tp_methods, static_cast<void*>(pattern_methods)},
    {Py_tp_getset, static_cast<void*>(pattern_getset)},
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
    real::flags f = real::flags::none;
    if (is_bytes != 0) {
        f = f | real::flags::bytes;  // raw-byte semantics, like re on bytes
    }
    if ((py_flags & PYFLAG_IGNORECASE) != 0) {
        f = f | real::flags::icase;
    }
    if ((py_flags & PYFLAG_MULTILINE) != 0) {
        f = f | real::flags::multiline;
    }
    if ((py_flags & PYFLAG_DOTALL) != 0) {
        f = f | real::flags::dotall;
    }
    if ((py_flags & PYFLAG_VERBOSE) != 0) {
        f = f | real::flags::verbose;
    }

    real::regex* rx = nullptr;
    try {
        rx = new real::regex(std::string_view(data, static_cast<std::size_t>(len)), f);
    } catch (const real::regex_error& e) {
        set_error(e.what());
        return nullptr;
    } catch (const std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }

    auto* obj = PyObject_New(PatternObject, reinterpret_cast<PyTypeObject*>(pattern_type));
    if (obj == nullptr) {
        delete rx;
        return nullptr;
    }
    obj->pattern_obj = Py_NewRef(pattern);
    obj->rx = rx;
    obj->scratch = new real::detail::pike_state();
    obj->scratch_slots = new std::vector<std::size_t>();
    obj->py_flags = py_flags;
    obj->is_bytes = is_bytes;
    return reinterpret_cast<PyObject*>(obj);
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

PyMethodDef module_methods[] = {
    {"compile", reinterpret_cast<PyCFunction>(reinterpret_cast<void*>(real_compile)),
     METH_VARARGS | METH_KEYWORDS, "Compile a pattern into a real.Pattern."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT, "_real", "REAL regex engine (C++ core).", -1,
    module_methods,        nullptr, nullptr,                         nullptr, nullptr,
};

}  // namespace

PyMODINIT_FUNC PyInit__real() {  // PyMODINIT_FUNC already says extern "C"
    PyObject* module = PyModule_Create(&module_def);
    if (module == nullptr) {
        return nullptr;
    }
    error_type = PyErr_NewException("real.error", nullptr, nullptr);
    pattern_type = PyType_FromSpec(&pattern_spec);
    match_type = PyType_FromSpec(&match_spec);
    if (error_type == nullptr || pattern_type == nullptr || match_type == nullptr ||
        PyModule_AddObject(module, "error", Py_NewRef(error_type)) < 0 ||
        PyModule_AddObject(module, "Pattern", Py_NewRef(pattern_type)) < 0 ||
        PyModule_AddObject(module, "Match", Py_NewRef(match_type)) < 0) {
        Py_DECREF(module);
        return nullptr;
    }
    return module;
}
