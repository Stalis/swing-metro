#!/usr/bin/env python3

import argparse
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


def walk(node, namespaces=()):
    if node.get("kind") == "Namespace" and node.get("detail"):
        namespaces += (node["detail"].removesuffix("::"),)
    yield node, namespaces
    for child in node.get("children", ()):
        yield from walk(child, namespaces)


def descendants(node):
    yield node
    for child in node.get("children", ()):
        yield from descendants(child)


def node_text(source, node):
    node_range = node.get("range")
    if not node_range:
        return ""
    lines = source.splitlines(keepends=True)
    start = node_range["start"]
    end = node_range["end"]
    if start["line"] >= len(lines) or end["line"] >= len(lines):
        return ""
    if start["line"] == end["line"]:
        return lines[start["line"]][start["character"] : end["character"]]
    return "".join(
        [lines[start["line"]][start["character"] :]]
        + lines[start["line"] + 1 : end["line"]]
        + [lines[end["line"]][: end["character"]]]
    )


def template_declarations(ast):
    result = {}
    for node, namespaces in walk(ast):
        if node.get("kind") != "ClassTemplate" or not node.get("detail"):
            continue
        if any(not namespace for namespace in namespaces):
            raise RuntimeError(f"anonymous-namespace template is unsupported: {node['detail']}")
        parameters = {
            child["detail"]
            for child in node.get("children", ())
            if child.get("kind", "").endswith("TemplateParm") and child.get("detail")
        }
        qualified = "::".join((*namespaces, node["detail"]))
        result[qualified] = parameters
    return result


def template_name(node):
    for child in node.get("children", ()):
        if child.get("role") == "template name" and child.get("detail"):
            return child["detail"].split("::")[-1]
    return None


def definition_template_names(ast):
    result = set()
    for node, _ in walk(ast):
        children = node.get("children", ())
        if node.get("kind") not in {"CXXConstructor", "CXXMethod"}:
            continue
        if not any(child.get("kind", "").endswith("TemplateParm") for child in children):
            continue
        if not any(child.get("kind") == "Compound" for child in children):
            continue
        for candidate in descendants(node):
            if candidate.get("kind") == "TemplateSpecialization":
                name = template_name(candidate)
                if name:
                    result.add(name)
                    break
    return result


def concrete_specializations(ast, source, declarations):
    by_short_name = {}
    for qualified, parameters in declarations.items():
        by_short_name.setdefault(qualified.split("::")[-1], []).append((qualified, parameters))

    result = set()
    for node, _ in walk(ast):
        if node.get("kind") != "TemplateSpecialization":
            continue
        name = template_name(node)
        matches = by_short_name.get(name, ())
        if not matches:
            continue
        if len(matches) != 1:
            raise RuntimeError(f"ambiguous template name: {name}")
        qualified, parameters = matches[0]
        arguments = [
            child
            for child in node.get("children", ())
            if child.get("role") == "template argument"
        ]
        if not arguments:
            continue
        argument_text = [node_text(source, argument).strip() for argument in arguments]
        if not all(argument_text):
            continue
        if any(argument in parameters for argument in argument_text):
            continue
        if any(
            child.get("kind") == "DeclRef" and child.get("detail") in parameters
            for argument in arguments
            for child in descendants(argument)
        ):
            continue
        result.add(f"{qualified}<{', '.join(argument_text)}>")
    return result


class Clangd:
    def __init__(self, root):
        executable = shutil.which("clangd")
        if not executable:
            raise RuntimeError("clangd is not available in PATH")
        query_driver = str(Path.home() / ".platformio/packages/*/bin/*")
        self.process = subprocess.Popen(
            [
                executable,
                "--background-index=0",
                f"--compile-commands-dir={root}",
                f"--query-driver={query_driver}",
                "--log=error",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        assert self.process.stderr is not None
        self.stdin = self.process.stdin
        self.stdout = self.process.stdout
        self.stderr = self.process.stderr
        self.messages = queue.Queue()
        self.errors = []
        self.next_id = 1
        threading.Thread(target=self._read_messages, daemon=True).start()
        threading.Thread(target=self._read_errors, daemon=True).start()

    def _read_messages(self):
        stream = self.stdout
        try:
            while True:
                headers = {}
                while True:
                    line = stream.readline()
                    if not line:
                        self.messages.put(None)
                        return
                    if line == b"\r\n":
                        break
                    key, value = line.decode("ascii").split(":", 1)
                    headers[key.lower()] = value.strip()
                length = int(headers["content-length"])
                self.messages.put(json.loads(stream.read(length)))
        except Exception as error:
            self.messages.put(error)

    def _read_errors(self):
        for line in self.stderr:
            self.errors.append(line.decode(errors="replace").rstrip())

    def _send(self, message):
        payload = json.dumps(message, separators=(",", ":")).encode()
        self.stdin.write(f"Content-Length: {len(payload)}\r\n\r\n".encode() + payload)
        self.stdin.flush()

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self._send(message)

    def request(self, method, params, timeout=60):
        request_id = self.next_id
        self.next_id += 1
        self._send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        while True:
            try:
                message = self.messages.get(timeout=timeout)
            except queue.Empty as error:
                raise RuntimeError(f"clangd timed out handling {method}") from error
            if message is None:
                raise RuntimeError("clangd exited unexpectedly: " + "\n".join(self.errors[-10:]))
            if isinstance(message, Exception):
                raise RuntimeError(f"invalid clangd response: {message}") from message
            if message.get("method") and "id" in message:
                self._send({"jsonrpc": "2.0", "id": message["id"], "result": None})
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise RuntimeError(f"clangd {method} failed: {message['error']['message']}")
            return message.get("result")

    def initialize(self, root):
        result = self.request(
            "initialize",
            {
                "processId": os.getpid(),
                "rootUri": root.as_uri(),
                "capabilities": {"general": {"positionEncodings": ["utf-8"]}},
            },
        )
        if not result.get("capabilities", {}).get("astProvider"):
            raise RuntimeError("clangd does not support textDocument/ast")
        self.notify("initialized", {})

    def ast(self, path):
        source = path.read_text(encoding="utf-8")
        uri = path.as_uri()
        self.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "cpp",
                    "version": 1,
                    "text": source,
                }
            },
        )
        result = self.request(
            "textDocument/ast",
            {"textDocument": {"uri": uri}},
        )
        self.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        if result is None:
            raise RuntimeError(f"clangd returned no AST for {path}")
        return source, result

    def close(self):
        if self.process.poll() is not None:
            return
        try:
            self.request("shutdown", {}, timeout=10)
            self.notify("exit")
            self.process.wait(timeout=10)
        except Exception:
            self.process.terminate()


def write_if_changed(path, content):
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.write_text(content, encoding="utf-8")
    return True


def ensure_include(implementation, generated):
    include = f'#include "{generated.name}"'
    source = implementation.read_text(encoding="utf-8")
    if include in source:
        return False
    implementation.write_text(source.rstrip() + f"\n\n{include}\n", encoding="utf-8")
    return True


def generate(root):
    compile_commands = root / "compile_commands.json"
    if not compile_commands.is_file():
        raise RuntimeError("compile_commands.json is missing; run `pio run -t compiledb` first")
    source_root = root / "src"
    paths = sorted(
        path.resolve()
        for path in source_root.rglob("*")
        if path.suffix.lower() in SOURCE_SUFFIXES and not path.name.endswith(".instantiations.inc")
    )

    clangd = Clangd(root)
    try:
        clangd.initialize(root)
        parsed = {path: clangd.ast(path) for path in paths}
    finally:
        clangd.close()

    declarations = {}
    for _, ast in parsed.values():
        declarations.update(template_declarations(ast))

    implementations = {}
    by_short_name = {qualified.split("::")[-1]: qualified for qualified in declarations}
    for path, (_, ast) in parsed.items():
        if path.suffix.lower() not in {".cc", ".cpp", ".cxx"}:
            continue
        for short_name in definition_template_names(ast):
            qualified = by_short_name.get(short_name)
            if not qualified:
                continue
            previous = implementations.setdefault(qualified, path)
            if previous != path:
                raise RuntimeError(f"template {qualified} is defined in multiple files")

    specializations = set()
    for source, ast in parsed.values():
        specializations.update(concrete_specializations(ast, source, declarations))

    grouped = {}
    for implementation in implementations.values():
        grouped.setdefault(implementation, set())
    for specialization in specializations:
        template = specialization.split("<", 1)[0]
        implementation = implementations.get(template)
        if implementation:
            grouped.setdefault(implementation, set()).add(specialization)

    for implementation, values in grouped.items():
        generated = implementation.with_suffix(".instantiations.inc")
        content = "// Generated by scripts/generate_template_instantiations.py.\n\n"
        if values:
            content += "\n".join(f"template class {value};" for value in sorted(values)) + "\n"
        write_if_changed(generated, content)
        ensure_include(implementation, generated)


def self_test():
    declaration = {
        "kind": "Namespace",
        "detail": "controls",
        "children": [
            {
                "kind": "ClassTemplate",
                "detail": "Matrix",
                "children": [{"kind": "NonTypeTemplateParm", "detail": "N"}],
            }
        ],
    }
    assert template_declarations(declaration) == {"controls::Matrix": {"N"}}

    source = "Matrix<4> concrete; Matrix<N> dependent;"
    ast = {
        "children": [
            {
                "kind": "TemplateSpecialization",
                "children": [
                    {"role": "template name", "detail": "Matrix"},
                    {
                        "role": "template argument",
                        "range": {"start": {"line": 0, "character": 7}, "end": {"line": 0, "character": 8}},
                    },
                ],
            },
            {
                "kind": "TemplateSpecialization",
                "children": [
                    {"role": "template name", "detail": "Matrix"},
                    {
                        "role": "template argument",
                        "range": {"start": {"line": 0, "character": 27}, "end": {"line": 0, "character": 28}},
                        "children": [{"kind": "DeclRef", "detail": "N"}],
                    },
                ],
            },
        ]
    }
    assert concrete_specializations(ast, source, {"controls::Matrix": {"N"}}) == {"controls::Matrix<4>"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        else:
            generate(args.root.resolve())
    except RuntimeError as error:
        print(f"template instantiation generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
