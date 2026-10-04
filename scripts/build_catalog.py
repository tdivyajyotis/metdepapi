"""Extract contracts from the saved IMD reference; never guess undocumented URLs."""
import json
import re
from html.parser import HTMLParser
from pathlib import Path


class ReferenceParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.sections = []
        self.section = None
        self.cell = None
        self.row = None
        self.table = None
        self.pre = None

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "section" and attrs.get("id", "").startswith("api-"):
            self.section = {"text": [], "tables": [], "samples": []}
        if self.section is None:
            return
        if tag == "table":
            self.table = []
        if tag == "tr":
            self.row = []
        if tag in ("td", "th"):
            self.cell = []
        if tag == "pre":
            self.pre = []

    def handle_data(self, text):
        if self.section is not None:
            self.section["text"].append(text)
        if self.cell is not None:
            self.cell.append(text)
        if self.pre is not None:
            self.pre.append(text)

    def handle_endtag(self, tag):
        if tag in ("td", "th") and self.cell is not None:
            self.row.append("".join(self.cell).strip())
            self.cell = None
        if tag == "tr" and self.table is not None:
            self.table.append(self.row)
        if tag == "table" and self.section is not None:
            self.section["tables"].append(self.table)
            self.table = None
        if tag == "pre" and self.pre is not None:
            self.section["samples"].append("".join(self.pre))
            self.pre = None
        if tag == "section" and self.section is not None:
            self.sections.append(self.section)
            self.section = None


def build():
    root = Path(__file__).resolve().parents[1]
    parser = ReferenceParser()
    parser.feed((root / "references/imd-api-reference.html").read_text(encoding="utf-8-sig"))
    products = {}
    for section in parser.sections:
        text = " ".join(section["text"])
        endpoints = re.findall(r"https://api\.imd\.gov\.in/api/v1/([a-z_]+)", text)
        if not endpoints:
            continue
        name = endpoints[0]
        fields = []
        shape = "unspecified"
        example = None
        if section["tables"] and section["tables"][0][0][0] in ("Field", "Field / Value / Description"):
            fields = [row[0] for row in section["tables"][0][1:] if row]
        for sample in section["samples"]:
            try:
                value = json.loads(re.sub(r",\s*([}\]])", r"\1", sample))
            except json.JSONDecodeError:
                continue
            shape = "array" if isinstance(value, list) else "object"
            example = value
            if not fields:
                record = value[0] if isinstance(value, list) and value else value
                if isinstance(record, dict):
                    fields = list(record)
            break
        products[name] = {
            "official_path": name, "documented_fields": fields,
            "documented_shape": shape, "contract_status": "reference_only",
            "public_provider": "city" if name in ("cityforecast", "cityforecastloc") else None,
            "documented_example": example,
        }
        if name == "aws_data":
            products[name]["state_ids"] = dict(re.findall(r'"(\d+)"\s*:\s*"([A-Z_]+)"', text))
        for endpoint in set(endpoints[1:]) - {name}:
            products.setdefault(endpoint, {
                "official_path": endpoint, "documented_fields": [],
                "documented_shape": "unspecified", "contract_status": "endpoint_only",
                "public_provider": None,
            })
    # Index entries have no detailed section or documented URL in this snapshot.
    for label in ("All India Weather Forecast Bulletin", "Mausamgram", "Fishermen Warning",
                  "Highway Nowcast Warning", "Highway Warning - 5 Days", "Radar Image",
                  "Lightning Data", "Agromet Advisory"):
        products["planned:" + label] = {
            "official_path": None, "documented_fields": [],
            "documented_shape": "unspecified", "contract_status": "index_only",
            "public_provider": None,
        }
    for name in ("districtrainfall", "staterainfall"):
        products[name]["public_provider"] = "rainfall"
    for name in ("districtwarning", "districtnowcast", "stationnowcast"):
        products[name]["public_provider"] = "warnings"
    for name in ("current_wx", "aws_data", "basinqpf"):
        products[name]["public_provider"] = "feed"
        products[name]["contract_status"] = "provisional_public_mapping"
    for name in ("subdivisionwarning", "subdivision_rainfall_forecast", "state_district_rainfall_forecast",
                 "portwarning", "seabulletin", "coastalbulletin"):
        products[name]["public_provider"] = "extended"
        products[name]["contract_status"] = "IMD_reference_field_contract"
    products["sunmoon"]["public_provider"] = "astronomy_station"
    products["sunmoon"]["contract_status"] = "IMD_reference_field_contract"
    (root / "imd_local/catalog.json").write_text(json.dumps(products, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    build()
