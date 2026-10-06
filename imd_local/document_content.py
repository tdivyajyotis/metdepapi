"""Content-addressed public documents and conservative PDF text extraction."""
import hashlib
import io
import re
from pathlib import Path
from urllib.parse import quote, urlsplit, urlunsplit

from .core import CollectionError


def document_content(item, transport, directory="data/artifacts", allowed_domains=("imd.gov.in",)):
    parsed = urlsplit(item["url"])
    if parsed.scheme not in ("http", "https") or not parsed.hostname or not any(parsed.hostname == d or parsed.hostname.endswith("." + d) for d in allowed_domains):
        raise CollectionError("invalid_source", "Document must be on an allowed publisher host")
    if parsed.scheme == "http":
        parsed = parsed._replace(scheme="https")
    # Quote spaces/unicode in published URLs; already quoted URL components remain intact.
    url = urlunsplit(parsed._replace(path=quote(parsed.path, safe="/%:@"), query=quote(parsed.query, safe="=&%:+,/")))
    content = transport.request(url, headers={"Accept": "application/pdf,image/*,*/*"}, allowed_domains=allowed_domains)
    if not content:
        raise CollectionError("unavailable", "Document content is empty")
    mime = None
    if content.startswith(b"%PDF-"):
        mime, suffix = "application/pdf", ".pdf"
    elif content.startswith(b"\x89PNG\r\n\x1a\n"):
        mime, suffix = "image/png", ".png"
    elif content.startswith((b"GIF87a", b"GIF89a")):
        mime, suffix = "image/gif", ".gif"
    elif content.startswith(b"\xff\xd8\xff"):
        mime, suffix = "image/jpeg", ".jpg"
    else:
        raise CollectionError("invalid_response", "Document URL returned an unexpected format")
    digest = hashlib.sha256(content).hexdigest()
    root = Path(directory).resolve()
    root.mkdir(parents=True, exist_ok=True)
    destination = root / (digest + suffix)
    # File names derive solely from a content hash, never remote file names.
    if not destination.exists():
        temporary = root / (digest + ".tmp")
        temporary.write_bytes(content)
        temporary.replace(destination)
    result = {**item, "retrieval_url": url, "content_verified": True, "sha256": digest, "bytes": len(content),
              "media_type": mime, "file": str(destination), "text_status": "not_applicable"}
    if mime == "application/pdf":
        try:
            from pypdf import PdfReader
        except ImportError:
            result["text_status"] = "requires_documents_extra"
        else:
            try:
                reader = PdfReader(io.BytesIO(content))
                if len(reader.pages) > 100:
                    raise CollectionError("invalid_response", "Document exceeds 100 page extraction limit")
                pages = [page.extract_text() or "" for page in reader.pages]
                result.update({"pages": pages, "page_count": len(pages),
                               "text_status": "extracted" if any(page.strip() for page in pages) else "image_only"})
            except CollectionError:
                raise
            except Exception as exc:
                # Upstream malformed/encrypted PDFs are isolated from independent products.
                result["text_status"] = "extraction_failed"
    return result


def enrich_documents(items, transport, directory="data/artifacts"):
    enriched, failures = [], []
    # PDF page renderings duplicate the PDF content; retain those URLs without redownloading.
    has_pdf = any(item["kind"] == "pdf" for item in items)
    for item in items:
        if has_pdf and "/pdf_to_img_" in item["url"]:
            enriched.append({**item, "download_status": "pdf_page_rendering"})
            continue
        try:
            enriched.append(document_content(item, transport, directory))
        except CollectionError as exc:
            failures.append({"source_url": item["url"], "status": exc.status})
            enriched.append({**item, "download_status": exc.status})
    return enriched, failures
