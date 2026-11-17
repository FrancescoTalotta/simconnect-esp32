#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Check simple repository-relative Markdown file links (not URLs/anchors)."""

import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures = []
    checked = 0
    documents = sorted(root.rglob("*.md"))
    for document in documents:
        if any(part in {".git", "build", "managed_components"} for part in
               document.relative_to(root).parts):
            continue
        text = document.read_text(encoding="utf-8")
        for match in re.finditer(r"!?\[[^\]]*\]\(([^)\s]+)\)", text):
            link = urlsplit(match.group(1))
            if link.scheme or link.netloc or not link.path:
                continue
            checked += 1
            target = (document.parent / unquote(link.path)).resolve()
            line = text.count("\n", 0, match.start()) + 1
            if root not in target.parents and target != root:
                failures.append(f"{document.relative_to(root)}:{line}: outside repository")
            elif not target.is_file():
                failures.append(
                    f"{document.relative_to(root)}:{line}: missing {link.path}"
                )
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"OK: {checked} relative file links checked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
