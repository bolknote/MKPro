#!/usr/bin/env python3
"""Export public Google Groups conversations and flag possible MK-61 programs.

This is a fallback for sources that are mirrored or discussed outside Telegram.
It needs no login for public Google Groups archives.

Typical use:
  python3 scripts/export-google-groups.py --group programming-calcs --limit 20

Output goes to .tmp/google-groups/<group>/ by default:
  - conversations.jsonl: raw-ish normalized conversation records
  - candidates.md: messages likely to contain MK-61 programs/listings
"""

from __future__ import annotations

import argparse
import datetime as dt
import html
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Optional


DEFAULT_GROUP = "programming-calcs"
DEFAULT_OUT = Path(".tmp/google-groups")
BASE = "https://groups.google.com"

KEYWORD_WEIGHTS = (
    (re.compile(r"\b(?:мк|mk)\s*-?\s*61\b", re.IGNORECASE), 5),
    (re.compile(r"\b(?:мк|mk)\s*-?\s*52\b", re.IGNORECASE), 3),
    (re.compile(r"\b(?:б3|b3)\s*-?\s*34\b", re.IGNORECASE), 2),
    (re.compile(r"\b(?:pmk|пмк)\b", re.IGNORECASE), 3),
    (re.compile(r"\bлистинг\w*\b", re.IGNORECASE), 4),
    (re.compile(r"\bпрограмм\w*\b", re.IGNORECASE), 3),
    (re.compile(r"\bисходник\w*\b", re.IGNORECASE), 3),
    (re.compile(r"\b(?:код|коды|команд[аы])\b", re.IGNORECASE), 2),
)

PROGRAM_LINE_RE = re.compile(
    r"(?m)^\s*(?:\d{2}|[AaАа][0-9]|[BbВв][0-9])\s*[:.\t ]+\S+"
)
OPCODE_RE = re.compile(
    r"(?:В/О|С/П|БП|ПП|xП|хП|Пx|Пх|Cx|Сx|F\s*x|K\s*[А-ЯA-Z]|\bНОП\b)",
    re.IGNORECASE,
)
SCRIPT_RE = re.compile(
    r"AF_initDataCallback\(\{key: 'ds:7'.*?data:",
    re.DOTALL,
)
TAG_RE = re.compile(r"<[^>]+>")


@dataclass(frozen=True)
class ConversationSummary:
    id: str
    title: str
    snippet: str
    date: Optional[str]
    author: str
    message_count: Optional[int]


def main() -> int:
    parser = argparse.ArgumentParser(description="Export public Google Groups conversations.")
    parser.add_argument("--group", default=DEFAULT_GROUP, help=f"Group name, default: {DEFAULT_GROUP}")
    parser.add_argument("--out", type=Path, default=None, help="Output directory")
    parser.add_argument("--limit", type=int, default=20, help="Maximum conversations to fetch")
    parser.add_argument("--query", action="append", default=[], help="Search query; may be repeated")
    parser.add_argument("--min-score", type=int, default=5, help="Candidate threshold")
    parser.add_argument("--sleep", type=float, default=0.7, help="Delay between HTTP requests")
    parser.add_argument("--download-attachments", action="store_true", help="Download linked attachments")
    args = parser.parse_args()

    out = args.out or (DEFAULT_OUT / args.group)
    out.mkdir(parents=True, exist_ok=True)
    html_dir = out / "html"
    html_dir.mkdir(parents=True, exist_ok=True)

    conversations = collect_conversations(args.group, args.query, args.limit)
    if not conversations:
        print("No public conversations found.", file=sys.stderr)
        return 2

    records = []
    candidates = []
    attachments_dir = out / "attachments"
    if args.download_attachments:
        attachments_dir.mkdir(parents=True, exist_ok=True)

    for index, summary in enumerate(conversations[: args.limit], start=1):
        url = conversation_url(args.group, summary.id)
        print(f"{index}/{min(args.limit, len(conversations))}: {summary.title}")
        page = fetch_text(url)
        (html_dir / f"{safe_name(summary.id)}.html").write_text(page, encoding="utf-8")
        record = parse_conversation_page(page, args.group, summary)
        records.append(record)

        if args.download_attachments:
            download_attachments(record, attachments_dir)

        for message in record["messages"]:
            haystack = "\n".join([record["title"], message["text"], " ".join(a["name"] for a in message["attachments"])])
            score, reasons = candidate_score(haystack)
            if score >= args.min_score:
                candidates.append(
                    {
                        "score": score,
                        "reasons": reasons,
                        "conversation": record["title"],
                        "conversation_id": record["id"],
                        "message_id": message["id"],
                        "date": message["date"],
                        "author": message["author"],
                        "link": record["url"],
                        "text": message["text"],
                        "attachments": message["attachments"],
                    }
                )
        time.sleep(args.sleep)

    write_jsonl(out / "conversations.jsonl", records)
    write_candidates(out / "candidates.md", candidates, args.min_score)
    print(f"Wrote {len(records)} conversation(s) to {out / 'conversations.jsonl'}")
    print(f"Candidate index: {out / 'candidates.md'}")
    return 0


def collect_conversations(group: str, queries: list[str], limit: int) -> list[ConversationSummary]:
    urls = [group_url(group)]
    urls.extend(search_url(group, query) for query in queries if query)

    found: dict[str, ConversationSummary] = {}
    for url in urls:
        page = fetch_text(url)
        for summary in parse_conversation_list(page):
            found.setdefault(summary.id, summary)
            if len(found) >= limit:
                break
        if len(found) >= limit:
            break
    return list(found.values())


def fetch_text(url: str) -> str:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": "Mozilla/5.0",
            "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.read().decode("utf-8", errors="replace")
    except urllib.error.URLError as exc:
        raise SystemExit(f"Failed to fetch {url}: {exc}") from exc


def parse_conversation_list(page: str) -> list[ConversationSummary]:
    data = extract_ds7(page)
    if not data or len(data) < 3 or not isinstance(data[2], list):
        return []

    result = []
    for row in data[2]:
        if not row or not isinstance(row, list):
            continue
        summary = row[0] if isinstance(row[0], list) else None
        parsed = parse_summary(summary)
        if parsed:
            result.append(parsed)
    return result


def parse_conversation_page(page: str, group: str, fallback: ConversationSummary) -> dict[str, Any]:
    data = extract_ds7(page)
    if not data or len(data) < 3:
        return {
            "id": fallback.id,
            "title": fallback.title,
            "snippet": fallback.snippet,
            "date": fallback.date,
            "author": fallback.author,
            "url": conversation_url(group, fallback.id),
            "messages": [],
        }

    summary = parse_summary(data[1]) or fallback
    messages = []
    for block in data[2] if isinstance(data[2], list) else []:
        parsed = parse_message_block(block)
        if parsed:
            messages.append(parsed)

    return {
        "id": summary.id,
        "title": summary.title,
        "snippet": summary.snippet,
        "date": summary.date,
        "author": summary.author,
        "message_count": summary.message_count,
        "url": conversation_url(group, summary.id),
        "messages": messages,
    }


def parse_summary(value: Any) -> Optional[ConversationSummary]:
    if not isinstance(value, list) or len(value) < 4:
        return None
    conv_id = value[1] if len(value) > 1 else None
    if not isinstance(conv_id, str):
        return None
    author = ""
    try:
        author = value[9][0][0][0] or ""
    except (IndexError, TypeError):
        pass
    message_count = value[6] if len(value) > 6 and isinstance(value[6], int) else None
    return ConversationSummary(
        id=conv_id,
        title=str(value[2] or ""),
        snippet=html.unescape(str(value[3] or "")),
        date=format_google_date(value[4] if len(value) > 4 else None),
        author=str(author),
        message_count=message_count,
    )


def parse_message_block(block: Any) -> Optional[dict[str, Any]]:
    if not isinstance(block, list) or not block:
        return None

    payload = block[0]
    if (
        isinstance(payload, list)
        and payload
        and isinstance(payload[0], list)
        and not (len(payload) > 1 and isinstance(payload[1], list))
    ):
        payload = payload[0]
    if not isinstance(payload, list) or len(payload) < 2:
        return None

    header = payload[0]
    body = payload[1] if len(payload) > 1 else None
    attachments = payload[2] if len(payload) > 2 else []
    if not isinstance(header, list) or len(header) < 8:
        return None

    author = ""
    try:
        author = header[2][0][0]
    except (IndexError, TypeError):
        pass

    body_html = extract_body_html(body)
    return {
        "id": header[1],
        "author": author,
        "subject": header[5] or "",
        "snippet": html.unescape(str(header[6] or "")),
        "date": format_google_date(header[7]),
        "text": html_to_text(body_html),
        "html": body_html,
        "attachments": parse_attachments(attachments),
    }


def extract_body_html(body: Any) -> str:
    if not isinstance(body, list):
        return ""
    strings = []
    for value in walk(body):
        if isinstance(value, str) and ("<" in value or len(value) > 40):
            strings.append(value)
    return max(strings, key=len, default="")


def parse_attachments(value: Any) -> list[dict[str, Any]]:
    result = []
    if not isinstance(value, list):
        return result
    for attachment in value:
        if not isinstance(attachment, list) or len(attachment) < 5:
            continue
        result.append(
            {
                "url": attachment[0],
                "kind": attachment[2] if len(attachment) > 2 else None,
                "mime": attachment[3] if len(attachment) > 3 else None,
                "name": attachment[4] if len(attachment) > 4 else "",
                "size": attachment[5] if len(attachment) > 5 else None,
            }
        )
    return result


def extract_ds7(page: str) -> Any:
    match = SCRIPT_RE.search(page)
    if not match:
        return None
    start = match.end()
    while start < len(page) and page[start].isspace():
        start += 1
    try:
        value, _ = json.JSONDecoder().raw_decode(page[start:])
        return value
    except json.JSONDecodeError:
        return None


def walk(value: Any) -> Iterable[Any]:
    yield value
    if isinstance(value, list):
        for item in value:
            yield from walk(item)
    elif isinstance(value, dict):
        for item in value.values():
            yield from walk(item)


def html_to_text(value: str) -> str:
    text = re.sub(r"(?i)<br\s*/?>", "\n", value)
    text = re.sub(r"(?i)</(?:div|p|li|ul|ol|h[1-6])>", "\n", text)
    text = TAG_RE.sub("", text)
    text = html.unescape(text)
    text = text.replace("\xa0", " ")
    lines = [re.sub(r"[ \t]+", " ", line).strip() for line in text.splitlines()]
    return "\n".join(line for line in lines if line).strip()


def candidate_score(text: str) -> tuple[int, list[str]]:
    score = 0
    reasons = []
    for pattern, weight in KEYWORD_WEIGHTS:
        matches = len(pattern.findall(text))
        if matches:
            score += min(matches, 3) * weight
            reasons.append(pattern.pattern)

    program_lines = len(PROGRAM_LINE_RE.findall(text))
    if program_lines:
        score += min(program_lines, 12)
        reasons.append(f"{program_lines} address-like line(s)")

    opcodes = len(OPCODE_RE.findall(text))
    if opcodes:
        score += min(opcodes, 8)
        reasons.append(f"{opcodes} MK opcode token(s)")
    return score, reasons


def write_jsonl(path: Path, records: list[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")


def write_candidates(path: Path, candidates: list[dict[str, Any]], min_score: int) -> None:
    candidates.sort(key=lambda item: (-item["score"], item["conversation"], item["message_id"] or ""))
    lines = [
        "# Google Groups MK-61 Candidates",
        "",
        f"Minimum score: {min_score}",
        f"Generated: {dt.datetime.now(dt.timezone.utc).isoformat()}",
        "",
    ]
    if not candidates:
        lines.append("No candidates matched the current threshold.")
    for item in candidates:
        attachment_names = ", ".join(a["name"] for a in item["attachments"]) or "-"
        lines.extend(
            [
                f"## Score {item['score']}: {item['conversation']}",
                "",
                f"- Date: {item['date']}",
                f"- Author: {item['author'] or '-'}",
                f"- Link: {item['link']}",
                f"- Attachments: {attachment_names}",
                f"- Reasons: {', '.join(item['reasons'])}",
                "",
                "```text",
                snippet(item["text"]),
                "```",
                "",
            ]
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def download_attachments(record: dict[str, Any], out: Path) -> None:
    prefix = safe_name(record["id"])
    for message in record["messages"]:
        for attachment in message["attachments"]:
            name = safe_name(attachment["name"] or "attachment")
            target = out / f"{prefix}-{message['id']}-{name}"
            if target.exists():
                continue
            request = urllib.request.Request(attachment["url"], headers={"User-Agent": "Mozilla/5.0"})
            try:
                with urllib.request.urlopen(request, timeout=30) as response:
                    target.write_bytes(response.read())
            except urllib.error.URLError as exc:
                print(f"Attachment failed: {attachment['url']}: {exc}", file=sys.stderr)


def snippet(text: str, max_chars: int = 1400) -> str:
    clean = text.strip()
    if len(clean) <= max_chars:
        return clean
    return clean[: max_chars - 1].rstrip() + "…"


def format_google_date(value: Any) -> Optional[str]:
    if isinstance(value, list) and value and isinstance(value[0], int):
        return dt.datetime.fromtimestamp(value[0], tz=dt.timezone.utc).isoformat()
    return None


def safe_name(value: str) -> str:
    cleaned = re.sub(r"[^0-9A-Za-z._-]+", "-", value).strip("-")
    return cleaned[:120] or "item"


def group_url(group: str) -> str:
    return f"{BASE}/g/{urllib.parse.quote(group)}"


def search_url(group: str, query: str) -> str:
    return f"{BASE}/g/{urllib.parse.quote(group)}/search?q={urllib.parse.quote(query)}"


def conversation_url(group: str, conversation_id: str) -> str:
    return f"{BASE}/g/{urllib.parse.quote(group)}/c/{urllib.parse.quote(conversation_id)}"


if __name__ == "__main__":
    raise SystemExit(main())
