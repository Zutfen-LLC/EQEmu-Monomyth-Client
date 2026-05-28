#!/usr/bin/env python3
import json
import os
import re
import sys
import textwrap
import urllib.error
import urllib.parse
import urllib.request

COMMENT_MARKER = "<!-- ai-adversarial-review -->"
GITHUB_API = "https://api.github.com"
MAX_DIFF_BYTES = 120_000
HTTP_TIMEOUT_SECS = 60


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def env(name):
    value = os.environ.get(name, "").strip()
    if not value:
        fail(f"Missing required environment variable: {name}")
    return value


def http_json(url, *, method="GET", headers=None, body=None, timeout=HTTP_TIMEOUT_SECS):
    request = urllib.request.Request(url, method=method)
    for key, value in (headers or {}).items():
        request.add_header(key, value)

    payload = None
    if body is not None:
        payload = json.dumps(body).encode("utf-8")
        request.add_header("Content-Type", "application/json")
        request.add_header("Content-Length", str(len(payload)))

    try:
        with urllib.request.urlopen(request, data=payload, timeout=timeout) as response:
            raw = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        details = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP {exc.code} for {url}: {details}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Request failed for {url}: {exc}") from exc

    if not raw:
        return {}
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Non-JSON response from {url}: {raw[:400]}") from exc


def github_headers(token):
    return {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "User-Agent": "ai-adversarial-review",
    }


def openai_headers(api_key, referer=None):
    headers = {
        "Authorization": f"Bearer {api_key}",
        "Accept": "application/json",
        "User-Agent": "ai-adversarial-review",
    }
    if referer:
        headers["HTTP-Referer"] = referer
        headers["X-Title"] = "EQEmu-Monomyth-Client AI Review"
    return headers


def get_pr(repo, pr_number, token):
    return http_json(
        f"{GITHUB_API}/repos/{repo}/pulls/{pr_number}",
        headers=github_headers(token),
    )


def list_pr_comments(repo, pr_number, token):
    return http_json(
        f"{GITHUB_API}/repos/{repo}/issues/{pr_number}/comments?per_page=100",
        headers=github_headers(token),
    )


def upsert_pr_comment(repo, pr_number, token, body):
    comments = list_pr_comments(repo, pr_number, token)
    existing = None
    for comment in reversed(comments if isinstance(comments, list) else []):
        if COMMENT_MARKER in (comment.get("body") or ""):
            existing = comment
            break

    if existing:
        return http_json(
            f"{GITHUB_API}/repos/{repo}/issues/comments/{existing['id']}",
            method="PATCH",
            headers=github_headers(token),
            body={"body": body},
        )

    return http_json(
        f"{GITHUB_API}/repos/{repo}/issues/{pr_number}/comments",
        method="POST",
        headers=github_headers(token),
        body={"body": body},
    )


def extract_changed_files(diff_text):
    files = []
    seen = set()
    for line in diff_text.splitlines():
        if not line.startswith("diff --git "):
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        path = parts[3][2:]
        if path not in seen:
            seen.add(path)
            files.append(path)
    return files


def extract_changed_line_ranges(diff_text):
    changed = {}
    current_path = None
    for line in diff_text.splitlines():
        if line.startswith("diff --git "):
            parts = line.split()
            current_path = parts[3][2:] if len(parts) >= 4 else None
            if current_path:
                changed.setdefault(current_path, [])
            continue

        if current_path is None or not line.startswith("@@ "):
            continue

        match = re.search(r"\+(\d+)(?:,(\d+))?", line)
        if not match:
            continue

        start = int(match.group(1))
        length = int(match.group(2) or "1")
        if length <= 0:
            continue
        changed[current_path].append((start, start + length - 1))

    return changed


def load_diff(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        diff_text = handle.read()
    if len(diff_text.encode("utf-8")) <= MAX_DIFF_BYTES:
        return diff_text, False

    encoded = diff_text.encode("utf-8")[:MAX_DIFF_BYTES]
    truncated = encoded.decode("utf-8", errors="ignore")
    return truncated, True


def extract_json_block(text):
    fenced = re.search(r"```(?:json)?\s*(.*?)```", text, re.DOTALL | re.IGNORECASE)
    if fenced:
        text = fenced.group(1)

    start = text.find("{")
    end = text.rfind("}")
    if start == -1 or end == -1 or end < start:
        raise RuntimeError("Model response did not contain a JSON object")
    return json.loads(text[start : end + 1])


def call_openai_review(base_url, api_key, model, prompt):
    response = http_json(
        urllib.parse.urljoin(base_url.rstrip("/") + "/", "chat/completions"),
        method="POST",
        headers=openai_headers(api_key, os.environ.get("HTTP_REFERER", "").strip()),
        body={
            "model": model,
            "temperature": 0.1,
            "response_format": {"type": "json_object"},
            "messages": [
                {
                    "role": "system",
                    "content": (
                        "You are a strict code reviewer. Review only the supplied diff. "
                        "Focus on concrete bugs, regressions, unsafe assumptions, missing guards, "
                        "or broken behavior introduced by the diff itself. "
                        "Do not block on repository-wide architectural traits such as low-level hooking, "
                        "RVA offsets, pointer arithmetic, global state, or version-coupled seams unless "
                        "the diff introduces a specific defect in how those patterns are used. "
                        "Do not escalate based on generic maintainability concerns alone. "
                        "Return compact JSON with keys verdict, summary, and findings. "
                        "verdict must be PASS, WARN, or BLOCK. findings must be an array. "
                        "Each finding must contain severity, title, path, line, summary, and recommendation. "
                        "Use severities BLOCKER, HIGH, MEDIUM, or LOW."
                    ),
                },
                {"role": "user", "content": prompt},
            ],
        },
    )

    try:
        return response["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError) as exc:
        raise RuntimeError(f"Unexpected chat completion payload: {response}") from exc


def normalize_review(payload, changed_files, changed_line_ranges):
    verdict = str(payload.get("verdict", "")).strip().upper()
    if verdict not in {"PASS", "WARN", "BLOCK"}:
        verdict = "WARN"

    summary = str(payload.get("summary", "")).strip() or "No summary returned."
    findings = payload.get("findings", [])
    normalized = []
    allowed = set(changed_files)

    if isinstance(findings, list):
        for finding in findings:
            if not isinstance(finding, dict):
                continue
            severity = str(finding.get("severity", "")).strip().upper()
            if severity not in {"BLOCKER", "HIGH", "MEDIUM", "LOW"}:
                continue

            path = str(finding.get("path", "")).strip()
            if allowed and path and path not in allowed:
                continue

            line = finding.get("line", "")
            if isinstance(line, str) and line.isdigit():
                line = int(line)
            if not isinstance(line, int):
                line = 0

            if path and line > 0:
                ranges = changed_line_ranges.get(path, [])
                if ranges and not any(start <= line <= end for start, end in ranges):
                    continue

            title = str(finding.get("title", "")).strip()
            detail = str(finding.get("summary", "")).strip()
            recommendation = str(finding.get("recommendation", "")).strip()
            if not title or not detail:
                continue

            normalized.append(
                {
                    "severity": severity,
                    "title": title,
                    "path": path or "unknown",
                    "line": line,
                    "summary": detail,
                    "recommendation": recommendation or "No recommendation provided.",
                }
            )

    if not normalized and verdict == "BLOCK":
        verdict = "PASS"
        summary = "No actionable findings remained after validating the model output against the changed hunks in the diff."

    return verdict, summary, normalized


def render_comment(model, verdict, summary, findings, truncated, changed_files):
    lines = [
        COMMENT_MARKER,
        "",
        f"### AI Adversarial Review (`{model}`)",
        "",
        f"Verdict: **{verdict}**",
        "",
        summary,
        "",
        f"Files reviewed: {len(changed_files)}",
    ]
    if truncated:
        lines.extend(["", "_Diff input was truncated before submission due to size limits._"])

    if not findings:
        lines.extend(["", "No actionable findings reported."])
        return "\n".join(lines)

    lines.extend(["", "Findings:"])
    for finding in findings:
        location = finding["path"]
        if finding["line"] > 0:
            location = f"{location}:{finding['line']}"
        lines.extend(
            [
                f"- `{finding['severity']}` {finding['title']} [{location}]",
                f"  {finding['summary']}",
                f"  Recommendation: {finding['recommendation']}",
            ]
        )
    return "\n".join(lines)


def main():
    if len(sys.argv) != 2:
        fail("Usage: ai_adversarial_review.py <diff-file>")

    diff_text, truncated = load_diff(sys.argv[1])
    changed_files = extract_changed_files(diff_text)
    changed_line_ranges = extract_changed_line_ranges(diff_text)
    if not diff_text.strip() or not changed_files:
        print("No meaningful diff to review.")
        sys.exit(0)

    base_url = env("OPENAI_BASE_URL")
    api_key = env("OPENAI_API_KEY")
    model = env("OPENAI_MODEL")
    repo = env("REPO")
    pr_number = env("PR_NUMBER")
    gh_token = env("GITHUB_TOKEN")

    pr = get_pr(repo, pr_number, gh_token)
    title = (pr.get("title") or "").strip()
    body = (pr.get("body") or "").strip()
    prompt = textwrap.dedent(
        f"""\
        Pull request title:
        {title or "(empty)"}

        Pull request body:
        {body[:2000] or "(empty)"}

        Changed files:
        {chr(10).join(changed_files)}

        Unified diff:
        {diff_text}
        """
    )

    raw_response = call_openai_review(base_url, api_key, model, prompt)
    payload = extract_json_block(raw_response)
    verdict, summary, findings = normalize_review(payload, changed_files, changed_line_ranges)
    if truncated and verdict == "BLOCK":
        verdict = "WARN"
        summary = (
            "The model returned BLOCK, but the diff submitted for review was truncated before submission. "
            "Blocking verdicts require complete review input, so this result was downgraded to WARN."
        )
    comment = render_comment(model, verdict, summary, findings, truncated, changed_files)
    upsert_pr_comment(repo, pr_number, gh_token, comment)

    if verdict == "BLOCK":
        print("AI review returned BLOCK.")
        sys.exit(1)

    print("AI review posted.")


if __name__ == "__main__":
    main()
