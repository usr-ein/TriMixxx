#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["sexpdata>=1.0"]
# ///
"""Parse a KiCad netlist (.net) and answer connectivity questions.

KiCad exports netlists as S-expressions -- `(export (components (comp ...)) (nets
(net (node ...))))`. Regex chokes on the nested parens, so parse them properly
with sexpdata and walk the tree.

    uv run parse_net.py FILE.net components        # ref, value, footprint
    uv run parse_net.py FILE.net nets [--min N]    # nets with >= N nodes (default 1)
    uv run parse_net.py FILE.net net NAME          # every pin on one net
    uv run parse_net.py FILE.net pins REF          # each pin of REF -> the net it is on
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from sexpdata import Symbol, loads


def _sym(x: object) -> object:
    """Unwrap a sexpdata Symbol to its string; pass other atoms through."""
    return x.value() if isinstance(x, Symbol) else x


def _tag(node: object) -> str | None:
    """The head symbol of an S-expr list, e.g. 'comp' for (comp ...)."""
    if isinstance(node, list) and node:
        return str(_sym(node[0]))
    return None


def _kids(node: list, name: str) -> list:
    """All direct child lists tagged `name`."""
    return [c for c in node if _tag(c) == name]


def _kid(node: list, name: str) -> list | None:
    """First direct child list tagged `name`, or None."""
    for c in node:
        if _tag(c) == name:
            return c
    return None


def _val(node: list, name: str, default: object = None) -> object:
    """The atom in a `(name atom)` child, e.g. _val(comp, 'ref') -> 'U1'."""
    c = _kid(node, name)
    if c is None or len(c) < 2:  # noqa: PLR2004
        return default
    return _sym(c[1])


def load(path: Path) -> list:
    """Parse a .net file into its nested-list S-expression."""
    return loads(path.read_text())


def components(root: list) -> list[dict]:
    """[{ref, value, footprint}] for every (comp ...)."""
    comps = _kid(root, "components") or []
    return [
        {"ref": _val(c, "ref"), "value": _val(c, "value"), "footprint": _val(c, "footprint")}
        for c in _kids(comps, "comp")
    ]


def nets(root: list) -> list[dict]:
    """[{name, code, nodes:[(ref, pin, pinfunction)]}] for every (net ...)."""
    ns = _kid(root, "nets") or []
    out = []
    for n in _kids(ns, "net"):
        nodes = [
            (_val(nd, "ref"), _val(nd, "pin"), _val(nd, "pinfunction") or "")
            for nd in _kids(n, "node")
        ]
        out.append({"name": _val(n, "name"), "code": _val(n, "code"), "nodes": nodes})
    return out


def cmd_components(root: list, _args: argparse.Namespace) -> None:
    for c in sorted(components(root), key=lambda c: str(c["ref"])):
        print(f"{c['ref'] or '?':8} {c['value'] or '':24} {c['footprint'] or ''}")


def cmd_nets(root: list, args: argparse.Namespace) -> None:
    for n in nets(root):
        if len(n["nodes"]) < args.min:
            continue
        pins = ", ".join(f"{r}.{p}" for r, p, _ in n["nodes"])
        print(f"{n['name'] or '?':22} ({len(n['nodes'])})  {pins}")


def cmd_net(root: list, args: argparse.Namespace) -> None:
    for n in nets(root):
        if n["name"] == args.name:
            print(f"net \"{n['name']}\"  ({len(n['nodes'])} nodes)")
            for r, p, fn in n["nodes"]:
                print(f"   {r or '?':8} pin {p or '?':6} {fn}")
            return
    sys.exit(f'net "{args.name}" not found')


def cmd_pins(root: list, args: argparse.Namespace) -> None:
    rows = [
        (p, fn, n["name"]) for n in nets(root) for r, p, fn in n["nodes"] if r == args.ref
    ]
    if not rows:
        sys.exit(f"no pins found for {args.ref}")
    print(f"{args.ref}: each pin -> the net it connects to")
    for p, fn, net in sorted(rows, key=lambda x: (len(str(x[0])), str(x[0]))):
        print(f"   pin {p or '?':6} {fn:16} -> {net}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Parse a KiCad .net netlist (S-expression).")
    ap.add_argument("file", type=Path, help="the .net export")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("components", help="list ref/value/footprint")
    p_nets = sub.add_parser("nets", help="list nets and their pins")
    p_nets.add_argument("--min", type=int, default=1, help="only nets with >= N nodes")
    p_net = sub.add_parser("net", help="show one net's nodes")
    p_net.add_argument("name")
    p_pins = sub.add_parser("pins", help="show which net each pin of a ref is on")
    p_pins.add_argument("ref")
    args = ap.parse_args()

    root = load(args.file)
    handlers = {
        "components": cmd_components,
        "nets": cmd_nets,
        "net": cmd_net,
        "pins": cmd_pins,
    }
    handlers[args.cmd](root, args)


if __name__ == "__main__":
    main()
