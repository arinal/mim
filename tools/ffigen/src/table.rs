// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! The table's types, and the checks on them the grammar can't express

use std::collections::HashSet;
use std::fmt;

// More than this many WASM values per call is a sign the import wants a struct
const MAX_WASM_ARGS: usize = 8;
// Names the thunks and handlers use themselves
const RESERVED: [&str; 7] = ["m", "mem", "sp", "ctx", "runtime", "result", "err"];
// Names that would break the generated C, C++ (sdk/cpp/ffi.h) or Rust: keywords, and the C types
// the generated code uses. Function names only clash in Rust, since the C ones get a prefix.
#[rustfmt::skip]
const KEYWORDS: &[&str] = &[
    // C
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
    "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict",
    "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while",
    "int32_t", "uint32_t", "int64_t", "uint64_t", "uint8_t", "size_t",
    // C++, on top of C
    "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor", "bool", "catch", "char8_t",
    "char16_t", "char32_t", "class", "co_await", "co_return", "co_yield", "compl", "concept",
    "const_cast", "consteval", "constexpr", "constinit", "decltype", "delete", "dynamic_cast",
    "explicit", "export", "false", "friend", "mutable", "namespace", "new", "noexcept", "not",
    "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
    "reinterpret_cast", "requires", "static_assert", "static_cast", "template", "this",
    "thread_local", "throw", "true", "try", "typeid", "typename", "using", "virtual", "wchar_t",
    "xor", "xor_eq",
    // Rust, including the reserved ones
    "_", "abstract", "as", "async", "await", "become", "box", "crate", "dyn", "final", "fn", "gen",
    "impl", "in", "let", "loop", "macro", "match", "mod", "move", "mut", "override", "priv", "pub",
    "ref", "self", "super", "trait", "type", "typeof", "unsafe", "unsized", "use", "where", "yield",
];

/// One WASM value
#[derive(Clone, Copy)]
pub enum Scalar {
    I32,
    U32,
    I64,
    U64,
    F32,
    F64,
}

#[derive(Clone, Copy)]
pub enum Ty {
    Scalar(Scalar),
    /// A guest address, one i32. The handler checks what it points at.
    Ptr,
    /// A guest address and a length, two i32s. Checked before the handler runs.
    Bytes,
}

pub struct Arg {
    pub name: String,
    pub ty: Ty,
}

pub struct Func {
    pub name: String,
    pub args: Vec<Arg>,
    /// What it returns, if anything: always a number, never memory
    pub ret: Option<Scalar>,
    /// `= <host fn>`: the thunk calls it directly, instead of a handler; it can't trap
    pub direct: Option<String>,
}

pub struct Const {
    pub name: String,
    pub value: u64,
}

pub enum Item {
    Const(Const),
    Func(Func),
    /// A header ffi.c includes, declaring the functions `=` names
    Include(String),
}

#[derive(Default)]
pub struct Table {
    pub consts: Vec<Const>,
    pub funcs: Vec<Func>,
    pub includes: Vec<String>,
}

impl Scalar {
    /// Its C type, its Rust type and its letter in a wasm3 signature
    fn names(self) -> (&'static str, &'static str, char) {
        match self {
            Scalar::I32 => ("int32_t", "i32", 'i'),
            Scalar::U32 => ("uint32_t", "u32", 'i'),
            Scalar::I64 => ("int64_t", "i64", 'I'),
            Scalar::U64 => ("uint64_t", "u64", 'I'),
            Scalar::F32 => ("float", "f32", 'f'),
            Scalar::F64 => ("double", "f64", 'F'),
        }
    }

    pub fn c(self) -> &'static str {
        self.names().0
    }

    pub fn rust(self) -> &'static str {
        self.names().1
    }

    pub fn wasm3(self) -> char {
        self.names().2
    }
}

impl fmt::Display for Ty {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Ty::Scalar(s) => f.write_str(s.rust()),
            Ty::Ptr => f.write_str("ptr"),
            Ty::Bytes => f.write_str("bytes"),
        }
    }
}

impl Arg {
    /// The WASM values this argument arrives as, named as the generated code names them
    pub fn values(&self) -> Vec<(String, Scalar)> {
        match self.ty {
            Ty::Scalar(s) => vec![(self.name.clone(), s)],
            Ty::Ptr => vec![(self.name.clone(), Scalar::U32)],
            Ty::Bytes => vec![
                (self.name.clone(), Scalar::U32),
                (format!("{}_len", self.name), Scalar::U32),
            ],
        }
    }
}

impl Func {
    pub fn values(&self) -> impl Iterator<Item = (String, Scalar)> + '_ {
        self.args.iter().flat_map(Arg::values)
    }

    pub fn signature(&self) -> String {
        format!(
            "{}({})",
            self.ret.map_or('v', Scalar::wasm3),
            self.values().map(|(_, s)| s.wasm3()).collect::<String>()
        )
    }

    /// Its return type in C, `void` if none
    pub fn c_ret(&self) -> &'static str {
        self.ret.map_or("void", Scalar::c)
    }

    // What's wrong with it that the grammar can't see
    pub fn problems(&self) -> Vec<String> {
        let keyword = KEYWORDS
            .contains(&self.name.as_str())
            .then(|| format!("`{}` is a C, C++ or Rust keyword", self.name));
        let mut seen = HashSet::new();
        let names = self
            .values()
            .map(|(n, _)| n)
            .filter(|n| {
                RESERVED.contains(&n.as_str())
                    || KEYWORDS.contains(&n.as_str())
                    || !seen.insert(n.clone())
            })
            .map(|n| format!("bad or duplicate argument name `{n}`"));
        let slots = self.values().count();
        let too_many = (slots > MAX_WASM_ARGS)
            .then(|| format!("{slots} WASM arguments, more than {MAX_WASM_ARGS}"));
        let direct = self.direct.as_ref().and_then(|d| {
            if KEYWORDS.contains(&d.as_str()) {
                Some(format!("`{d}` is a C, C++ or Rust keyword"))
            } else {
                // nothing would check where it points
                self.args
                    .iter()
                    .any(|a| matches!(a.ty, Ty::Ptr))
                    .then(|| "a `ptr` argument needs a handler to check it".to_owned())
            }
        });
        keyword
            .into_iter()
            .chain(names)
            .chain(too_many)
            .chain(direct)
            .collect()
    }
}

impl Item {
    pub fn name(&self) -> &str {
        match self {
            Item::Const(c) => &c.name,
            Item::Func(f) => &f.name,
            Item::Include(path) => path,
        }
    }

    pub fn problems(&self) -> Vec<String> {
        match self {
            Item::Func(f) => f.problems(),
            // a C enum constant is an int
            Item::Const(c) => (c.value > i32::MAX as u64)
                .then(|| format!("{} is too big for C, at most {}", c.value, i32::MAX))
                .into_iter()
                .collect(),
            Item::Include(_) => vec![],
        }
    }
}
