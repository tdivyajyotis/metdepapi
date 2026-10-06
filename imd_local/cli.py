import argparse
import json
import sys
import re
from pathlib import Path
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlsplit

from .core import CATALOG, PUBLIC_ROOT, CollectionError, Store, Transport, collect
from .operations import freshness, load_jobs, run_jobs, compare_shapes
from .artifacts import ARTIFACTS, collect_artifact


def parameters(items):
    values = {}
    for item in items:
        key, separator, value = item.partition("=")
        if not separator or not key or key in values:
            raise CollectionError("invalid_parameters", "Use unique --param key=value entries")
        values[key] = value
    return values


def handler(store, provider, max_age=86400, max_source_age=172800):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            parts = urlsplit(self.path)
            query = parse_qs(parts.query, keep_blank_values=True)
            if any(len(values) != 1 for values in query.values()):
                return self.respond(400, {"status": "invalid_parameters", "message": "Repeated parameters are unsupported"})
            params = {key: values[0] for key, values in query.items()}
            if parts.path == "/health":
                return self.respond(200, {"status": "running", "provider": provider, "mode": "stored_snapshots"})
            if parts.path == "/coverage":
                return self.respond(200, CATALOG)
            if parts.path == "/marine":
                from .incois.view import marine_view
                try:
                    return self.respond(200, marine_view(store, params, max_age))
                except CollectionError as exc:
                    return self.respond(400, {"status": exc.status, "message": str(exc)})
            if parts.path.startswith("/incois/"):
                from .incois import PRODUCTS, forecast_quality
                name = parts.path[len("/incois/"):]
                if name == "coverage":
                    return self.respond(200, {"publisher": "INCOIS", "products": PRODUCTS})
                wrapped = name.startswith("data/")
                if wrapped:
                    name = name[5:]
                if name not in PRODUCTS:
                    return self.respond(404, {"status": "unknown_product"})
                snapshot = store.latest("incois:" + name, "public", params)
                if snapshot is None:
                    return self.respond(503, {"status": "unavailable", "message": "No matching INCOIS snapshot"})
                quality = forecast_quality(snapshot, max_age)
                snapshot["metadata"]["freshness"] = quality
                if (quality["stale"] or quality.get("unavailable")) and not wrapped:
                    return self.respond(503, {"status": "unavailable" if quality.get("unavailable") else "stale", "freshness": quality})
                return self.respond(200, snapshot if wrapped else snapshot["data"])
            if parts.path.startswith("/files/"):
                filename = parts.path[len("/files/"):]
                if not re.fullmatch(r"[a-f0-9]{64}\.(pdf|png|gif|jpg)", filename):
                    return self.respond(404, {"status": "not_found"})
                path = store.path.parent / "artifacts" / filename
                if not path.is_file():
                    return self.respond(404, {"status": "not_found"})
                mime = {".pdf": "application/pdf", ".png": "image/png", ".gif": "image/gif", ".jpg": "image/jpeg"}[path.suffix]
                self.send_response(200)
                self.send_header("Content-Type", mime)
                self.send_header("X-Content-Type-Options", "nosniff")
                self.end_headers()
                self.wfile.write(path.read_bytes())
                return
            if parts.path.startswith("/artifacts/"):
                name = parts.path[len("/artifacts/"):]
                if name not in ARTIFACTS:
                    return self.respond(404, {"status": "unknown_product"})
                snapshot = store.latest("artifact:" + name, "public", params)
                if snapshot is None:
                    return self.respond(503, {"status": "unavailable"})
                snapshot["metadata"]["freshness"] = freshness(snapshot, max_age, max_source_age)
                return self.respond(200, snapshot)
            for prefix, wrapped in (("/api/v1/", False), ("/data/", True)):
                if parts.path.startswith(prefix):
                    product = parts.path[len(prefix):]
                    if product not in CATALOG:
                        return self.respond(404, {"status": "unknown_product"})
                    if provider == "public" and not CATALOG[product]["public_provider"]:
                        return self.respond(501, {"status": "unsupported", "product": product})
                    snapshot = store.latest(product, provider, params)
                    if snapshot is None:
                        return self.respond(503, {"status": "unavailable", "message": "No matching collected snapshot"})
                    if provider == "public":
                        from .core import Result
                        from .contracts import apply_documented_types
                        result = Result(snapshot["product"],snapshot["provider"],snapshot["status"],snapshot["data"],snapshot["metadata"])
                        try:
                            apply_documented_types(result)
                        except CollectionError as exc:
                            return self.respond(503,{"status":exc.status,"message":str(exc)})
                        snapshot = result.envelope()
                    quality = freshness(snapshot, max_age, max_source_age)
                    snapshot["metadata"]["freshness"] = quality
                    if quality["stale"] and not wrapped:
                        return self.respond(503, {"status": "stale", "product": product, "freshness": quality})
                    # No automatic network calls or silent stale/fallback substitutions.
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json; charset=utf-8")
                    self.send_header("X-IMD-Provider", provider)
                    self.send_header("X-IMD-Status", snapshot["status"])
                    self.send_header("X-IMD-Retrieved-At", snapshot["metadata"]["retrieved_at"])
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(json.dumps(snapshot if wrapped else snapshot["data"]).encode())
                    return
            self.respond(404, {"status": "not_found"})

        def respond(self, status, value):
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.end_headers()
            self.wfile.write(json.dumps(value).encode())

        def log_message(self, *_):
            pass  # Request query parameters are not written into logs.

    return Handler


def main(argv=None):
    parser = argparse.ArgumentParser(description="Local IMD collection; documented field names, provisional public compatibility")
    parser.add_argument("--database", default="data/imd.sqlite")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("coverage")
    from .incois import PRODUCTS
    incois = commands.add_parser("incois", help="Collect source-native INCOIS products")
    incois.add_argument("product", choices=sorted(PRODUCTS))
    incois.add_argument("--param", action="append", default=[])
    search = commands.add_parser("search")
    search.add_argument("query")
    collection = commands.add_parser("collect")
    collection.add_argument("product", choices=sorted(CATALOG))
    collection.add_argument("--provider", choices=["public", "official"], default="public")
    collection.add_argument("--param", action="append", default=[])
    server = commands.add_parser("serve")
    server.add_argument("--provider", choices=["public", "official"], default="public")
    server.add_argument("--port", type=int, default=8000)
    server.add_argument("--host", default="127.0.0.1", help="Bind address; use 0.0.0.0 inside a container")
    server.add_argument("--max-age", type=int, default=86400, help="Maximum retrieval age in seconds")
    server.add_argument("--max-source-age", type=int, default=172800, help="Maximum known source age in seconds")
    artifact = commands.add_parser("artifact")
    artifact.add_argument("name", choices=ARTIFACTS)
    artifact.add_argument("--param", action="append", default=[])
    artifact.add_argument("--index-only", action="store_true", help="Discover links without downloading contents")
    batch = commands.add_parser("batch")
    batch.add_argument("config")
    batch.add_argument("--watch", action="store_true", help="Run portable periodic collection until interrupted")
    parity = commands.add_parser("compare")
    parity.add_argument("public_json")
    parity.add_argument("official_json")
    validation = commands.add_parser("validate")
    validation.add_argument("product", choices=sorted(CATALOG))
    validation.add_argument("json_file")
    args = parser.parse_args(argv)
    transport = Transport()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    try:
        if args.command == "coverage":
            output = CATALOG
        elif args.command == "incois":
            from .incois import collect_incois
            result, raw = collect_incois(args.product, parameters(args.param), transport, directory=Path(args.database).parent / "artifacts")
            Store(args.database).save(result, raw)
            output = result.envelope()
        elif args.command == "search":
            from urllib.parse import urlencode
            output = transport.json(PUBLIC_ROOT + "api/search.php?" + urlencode({"query": args.query}))
        elif args.command == "collect":
            result, raw = collect(args.product, parameters(args.param), args.provider, transport)
            Store(args.database).save(result, raw)
            output = result.envelope()
        elif args.command == "artifact":
            result, raw = collect_artifact(args.name, parameters(args.param), transport,
                                           download=not args.index_only, directory=Path(args.database).parent / "artifacts")
            Store(args.database).save(result, raw)
            output = result.envelope()
        elif args.command == "batch":
            output = run_jobs(load_jobs(args.config), Store(args.database), transport, args.watch,
                              (lambda report: print(json.dumps(report), flush=True)) if args.watch else None)
            if any(report.get("failed") for report in output):
                print(json.dumps(output, indent=2))
                return 1
        elif args.command == "compare":
            left = json.loads(Path(args.public_json).read_text(encoding="utf-8-sig"))
            right = json.loads(Path(args.official_json).read_text(encoding="utf-8-sig"))
            differences = compare_shapes(left, right)
            output = {"shape_match": not differences, "differences": differences,
                      "semantics_verified": False, "notes": "Compare product payloads; time, units and identifier parity require review."}
            print(json.dumps(output, indent=2))
            return int(bool(differences))
        elif args.command == "validate":
            from .contracts import validate_example
            value = json.loads(Path(args.json_file).read_text(encoding="utf-8-sig"))
            spec = CATALOG[args.product]
            example = spec.get("documented_example")
            if example is not None:
                if isinstance(example,dict) and "data" not in example and isinstance(value,list):
                    errors = []
                    for index,row in enumerate(value): validate_example(row,example,f"$[{index}]",errors)
                else:
                    errors = validate_example(value, example)
            else:
                rows = value if isinstance(value, list) else [value]
                errors = [{"record": index, "expected_keys": spec["documented_fields"]}
                          for index, row in enumerate(rows) if not isinstance(row, dict) or set(row) != set(spec["documented_fields"])]
                if not spec["documented_fields"]:
                    raise CollectionError("undocumented_schema", "This endpoint has no schema in the reference")
            print(json.dumps({"valid": not errors, "basis": "IMD_reference_only", "errors": errors}, indent=2))
            return int(bool(errors))
        else:
            print(f"Serving stored {args.provider} snapshots at http://{args.host}:{args.port}", flush=True)
            with HTTPServer((args.host, args.port), handler(Store(args.database), args.provider, args.max_age, args.max_source_age)) as httpd:
                httpd.serve_forever()
            return 0
        print(json.dumps(output, indent=2, ensure_ascii=False))
        return 0
    except CollectionError as exc:
        print(json.dumps({"status": exc.status, "message": str(exc)}), file=sys.stderr)
        return 1
    except (OSError, ValueError) as exc:
        print(json.dumps({"status": "configuration_error", "message": "Check file paths, JSON content and server port"}), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
