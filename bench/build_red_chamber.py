"""Rebuild the Red Chamber v1 supplement from Gutenberg ebook 24264."""
import argparse
import hashlib
import re
from pathlib import Path

from build import build

SOURCE_SHA256 = "824b0baead58b4bb1c9f6cd365d8323e4799065f4864170db964b1dfa981f557"


def samples(path):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError("expected the pinned 2021-12-22 revision of 24264-0.txt")
    text = raw.decode("utf-8-sig").replace("\r\n", "\n")
    start = text.index("第一回　甄士隱")
    end = text.index("*** END OF THE PROJECT GUTENBERG EBOOK", start)
    body = text[start:end].rstrip("\n") + "\n"

    # Chapter 45 repeats its heading. Use its first occurrence and retain both in the text.
    headings = {}
    for match in re.finditer(r"^第[一二三四五六七八九十百零]+回(?=\s)", body, re.M):
        headings.setdefault(match[0], match.start())
    starts = list(headings.values())
    assert len(starts) == 120 and starts[0] == 0
    chapters = [body[a:b] for a, b in zip(starts, starts[1:] + [len(body)])]

    for width in (1, 4, 16, 64, 256, 1024):
        excerpts = []
        for number, chapter in enumerate(chapters):
            # Vary the location across chapters and keep the same anchor across widths.
            anchor = (11 + number * 37 % 80) * (len(chapter) - 1024) // 100
            anchor = re.search(r"[\u4e00-\u9fff]", chapter[anchor:]).start() + anchor
            excerpt = chapter[anchor:anchor + width]
            assert len(excerpt) == width
            excerpts.append(excerpt.encode())
        yield f"red-chamber-{width}cp", excerpts

    paragraphs = []
    for chapter in chapters:
        blocks = re.findall(r"^　　[^\n]+(?:\n(?!　　|第|[-\n])[^\n]+)*", chapter, re.M)
        if blocks:
            paragraphs.append(blocks[len(blocks) // 2].encode())
    yield "red-chamber-paragraphs", paragraphs
    yield "red-chamber-chapters", [chapters[i * 119 // 11].encode() for i in range(12)]
    yield "red-chamber-book", [body.encode()]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    build(args.destination, samples(args.source))
