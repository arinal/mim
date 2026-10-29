// SPDX-License-Identifier: MIT OR GPL-2.0-only

//! Parsing: one item per line, `#` starts a comment

use crate::TABLE;
use crate::table::{Arg, Const, Func, Item, Scalar, Table, Ty};
use std::collections::HashSet;
use winnow::ascii::{dec_uint, space0, space1};
use winnow::combinator::{alt, cut_err, delimited, fail, opt, preceded, separated, seq};
use winnow::error::{StrContext, StrContextValue};
use winnow::prelude::*;
use winnow::token::{one_of, take_while};

fn expected(what: &'static str) -> StrContext {
    StrContext::Expected(StrContextValue::Description(what))
}

fn ident(i: &mut &str) -> ModalResult<String> {
    (
        one_of(('a'..='z', '_')),
        take_while(0.., ('a'..='z', '0'..='9', '_')),
    )
        .take()
        .map(str::to_owned)
        .context(expected("a lower_case name"))
        .parse_next(i)
}

fn scalar(i: &mut &str) -> ModalResult<Scalar> {
    alt((
        "i32".value(Scalar::I32),
        "u32".value(Scalar::U32),
        "i64".value(Scalar::I64),
        "u64".value(Scalar::U64),
        "f32".value(Scalar::F32),
        "f64".value(Scalar::F64),
    ))
    .parse_next(i)
}

fn ty(i: &mut &str) -> ModalResult<Ty> {
    alt((
        scalar.map(Ty::Scalar),
        "ptr".value(Ty::Ptr),
        "bytes".value(Ty::Bytes),
    ))
    .context(expected("i32, u32, i64, u64, f32, f64, ptr or bytes"))
    .parse_next(i)
}

fn arg(i: &mut &str) -> ModalResult<Arg> {
    // past the `:` it's an argument, so a bad type is reported where it is
    seq!(Arg { name: ident, _: (space0, ':', space0), ty: cut_err(ty) }).parse_next(i)
}

// `const <name> <value>`
fn constant(i: &mut &str) -> ModalResult<Const> {
    seq!(Const {
        name: ident,
        _: space1,
        value: dec_uint.context(expected("a non-negative integer")),
    })
    .parse_next(i)
}

// `fn <name> (<arg>, ...)`, then `-> <type>` if it returns something, then `= <host fn>` if a host
// function does it all
fn func(i: &mut &str) -> ModalResult<Func> {
    seq!(Func {
        name: ident,
        _: space0,
        args: delimited(
            ('(', space0),
            separated(0.., arg, (space0, ',', space0)),
            (space0, ')').context(expected("`,` or `)`")),
        ),
        // a guest can't be handed host memory, so a result is always a number
        ret: opt(preceded(
            (space0, "->", space0),
            cut_err(scalar.context(expected("i32, u32, i64, u64, f32 or f64"))),
        )),
        direct: opt(preceded((space0, '=', space0), cut_err(ident))),
    })
    .parse_next(i)
}

fn item(i: &mut &str) -> ModalResult<Item> {
    alt((
        preceded(("const", space1), cut_err(constant)).map(Item::Const),
        preceded(("fn", space1), cut_err(func)).map(Item::Func),
        preceded(
            ("include", space1),
            cut_err(
                take_while(1.., |c: char| !c.is_whitespace()).context(expected("a header path")),
            ),
        )
        .map(|path: &str| Item::Include(path.to_owned())),
        fail.context(expected("`const`, `fn` or `include`")),
    ))
    .parse_next(i)
}

/// The table, or every problem found in it, each starting with "sdk/mim.ffi:<line>:"
pub fn parse(text: &str) -> Result<Table, Vec<String>> {
    let parsed: Vec<Result<(usize, Item), String>> = text
        .lines()
        .enumerate()
        .map(|(n, line)| (n + 1, line.split('#').next().unwrap_or("").trim()))
        .filter(|(_, line)| !line.is_empty())
        .map(|(n, line)| {
            item.parse(line)
                .map(|it| (n, it))
                .map_err(|e| format!("{TABLE}:{n}:\n{e}"))
        })
        .collect();

    let mut names = HashSet::new();
    let errors: Vec<String> = parsed
        .iter()
        .flat_map(|r| match r {
            Err(e) => vec![e.clone()],
            Ok((n, it)) => {
                let duplicate = (!names.insert(it.name().to_owned()))
                    .then(|| format!("duplicate name `{}`", it.name()));
                duplicate
                    .into_iter()
                    .chain(it.problems())
                    .map(|p| format!("{TABLE}:{n}: {p}"))
                    .collect()
            }
        })
        .collect();
    if !errors.is_empty() {
        return Err(errors);
    }

    let table = parsed
        .into_iter()
        .flatten()
        .fold(Table::default(), |mut t, (_, it)| {
            match it {
                Item::Const(c) => t.consts.push(c),
                Item::Func(f) => t.funcs.push(f),
                Item::Include(path) => t.includes.push(path),
            }
            t
        });
    // C has no empty array, and guest_imports[] would be one
    Some(table).filter(|t| !t.funcs.is_empty()).ok_or_else(|| {
        vec![format!(
            "{TABLE}: no `fn`: there has to be at least one import"
        )]
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signatures() {
        let t = parse("const n 3\nfn log (level: u32, text: bytes)  # comment\nfn f (a: f32, b: i64, p: ptr)\nfn g (t: bytes) -> f32\n").unwrap();
        let sigs: Vec<String> = t.funcs.iter().map(Func::signature).collect();
        assert_eq!(sigs, ["v(iii)", "v(fIi)", "f(ii)"]);
        assert_eq!(t.consts[0].value, 3);
    }

    #[test]
    fn rejects() {
        let Err(errors) = parse(
            "fn a (x: str)\nfn b (x: u32) -> ptr\nfn c (x: u32, x: u32)\nfn d (t: bytes, t_len: u32)\n\
             fn e (mem: u32)\nconst c 1\nconst k -1\nfn g (a: bytes, b: bytes, c: bytes, d: bytes, e: u32)\nwhat\n\
             fn h (int: u32)\nfn type (a: u32)\nfn _ (a: u32)\nconst big 2147483648\nfn i (this: u32)\n\
             fn j (p: ptr) = f\nfn k () = int\ninclude\n",
        ) else {
            panic!("accepted a bad table");
        };
        let lines: Vec<&str> = errors
            .iter()
            .map(|e| e.split(':').nth(1).unwrap())
            .collect();
        let expected: Vec<String> = (1..=17).map(|n| n.to_string()).collect();
        assert_eq!(lines, expected, "{errors:#?}");
    }

    #[test]
    fn limits() {
        // the largest constant C takes
        assert!(parse("const n 2147483647\nfn f ()").is_ok());
        // no imports: guest_imports[] would be an empty array
        assert!(parse("const n 1").is_err());
        // `result` is the generated code's own name
        assert!(parse("fn f (result: u32)").is_err());
    }
}
