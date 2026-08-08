from __future__ import annotations

import pathlib

from .protocol import format_records, parse_records


def render_log(data: bytes, output_format: str) -> bytes | str:
    if output_format == "raw":
        return data
    return format_records(parse_records(data), output_format)


def save_log(data: bytes, output_format: str, output_path: pathlib.Path) -> None:
    rendered = render_log(data, output_format)
    if isinstance(rendered, bytes):
        output_path.write_bytes(rendered)
    else:
        output_path.write_text(rendered, encoding="utf-8")
