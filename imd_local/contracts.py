"""Only the IMD reference supplies names, sample types and nested structures."""
from .core import CATALOG, CollectionError


def apply_documented_types(result):
    spec = CATALOG[result.product]
    example = spec.get("documented_example")
    record = example[0] if isinstance(example, list) and example else example
    rows = result.data if isinstance(result.data, list) else [result.data]
    if not spec["documented_fields"]:
        raise CollectionError("undocumented_schema", "The reference has no record fields for this endpoint")
    for row in rows:
        if not isinstance(row, dict) or set(row) != set(spec["documented_fields"]):
            raise CollectionError("contract_error", "Public mapping differs from documented field names")
        if isinstance(record, dict):
            for key, value in row.items():
                if value is None or key not in record:
                    continue
                # A string in the reference remains a string, including IDs/numeric-looking values.
                if isinstance(record[key], str):
                    row[key] = str(value)
    result.metadata["contract_basis"] = "IMD_API_reference_only"
    if isinstance(example, dict) and "data" not in example and isinstance(result.data, list):
        if "id" in result.metadata.get("parameters", {}) and len(result.data) == 1:
            result.data = result.data[0]
            result.metadata["response_shape"] = "documented_single_record_object"
        else:
            result.metadata["response_shape"] = "array_of_documented_records; aggregate_shape_not_illustrated_in_reference"
    result.metadata["schema_limits"] = "Samples define illustrated types; tables do not define every wire type or aggregate wrapper. Missing source fields are null."


def validate_example(value, example, path="$", errors=None):
    """Check documented sample topology and types, permitting explicit missing nulls."""
    errors = errors if errors is not None else []
    if value is None:
        return errors
    if isinstance(example, dict):
        if not isinstance(value, dict):
            errors.append({"path": path, "expected": "object"})
        else:
            for key in sorted(set(value) ^ set(example)):
                errors.append({"path": path + "." + key, "expected": "documented_key_set"})
            for key in set(value) & set(example):
                validate_example(value[key], example[key], path + "." + key, errors)
    elif isinstance(example, list):
        if not isinstance(value, list):
            errors.append({"path": path, "expected": "array"})
        elif example:
            for index, row in enumerate(value):
                # Sunmoon illustrates two distinct positional record structures.
                template = example[index] if len(example) > 1 and index < len(example) else example[0]
                validate_example(row, template, f"{path}[{index}]", errors)
    elif example is not None and type(value) is not type(example):
        errors.append({"path": path, "expected": type(example).__name__, "actual": type(value).__name__})
    return errors
