#!/usr/bin/env python3
"""Classify one correlated suspend/wake evidence set without inferring missing observations.

@canonical wake_trial_classifier -- all wake-trial manifests use this classifier.

The classifier reads an external-observation record plus raw, before/after Linux
counter snapshots. It recomputes every delta, validates the complete structural
and semantic manifest contract, writes a schema-versioned manifest, and treats
missing, decreasing, or ambiguous evidence as ``unobserved``.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Literal, NoReturn


CLASSIFICATIONS = (
    "entered_and_attributed",
    "aborted",
    "wrong_source",
    "timeout",
    "unobserved",
)
OBSERVATION_KEYS = {
    "schema_version",
    "correlation_id",
    "implementation",
    "requested_state",
    "selected_mem_sleep",
    "starting_level",
    "wake_edge",
    "expected_interrupt_label",
    "trial_started_at_utc",
    "external_stimulus",
    "state_entry",
    "wake_reason",
    "negative_control",
    "linux_input_event",
    "resume_observation",
}
EXPECTATION_KEYS = {
    "schema_version",
    "correlation_id",
    "implementation",
    "requested_state",
    "selected_mem_sleep",
    "starting_level",
    "wake_edge",
    "expected_wake_source",
    "expected_interrupt_label",
    "trial_started_at_utc",
}
CORRELATION_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
RFC3339_DATE_TIME_PATTERN = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$"
)
WAKE_TRIAL_SCHEMA_PATH = Path(__file__).resolve().parents[1] / "schemas/wake_trial.schema.json"
SUPPORTED_SCHEMA_KEYWORDS = {
    "$schema",
    "$id",
    "$defs",
    "$ref",
    "title",
    "description",
    "type",
    "additionalProperties",
    "required",
    "properties",
    "const",
    "enum",
    "pattern",
    "minLength",
    "format",
    "minimum",
    "minItems",
    "uniqueItems",
    "items",
    "allOf",
    "if",
    "then",
    "else",
}


class WakeTrialEvidenceError(Exception):
    """Report an invalid classifier input with a stable code and context."""

    def __init__(self, code: str, message: str, context: dict[str, Any]) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.context = context


class WakeTrialArgumentParser(argparse.ArgumentParser):
    """Raise a structured domain error instead of exiting on invalid arguments."""

    def error(self, message: str) -> NoReturn:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.ARGUMENT_INVALID",
            message,
            {"usage": self.format_usage().strip()},
        )


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _require_object(container: dict[str, Any], key: str) -> dict[str, Any]:
    value = container.get(key)
    if not isinstance(value, dict):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{key} must be an object",
            {"field": key},
        )
    return value


def _require_string(container: dict[str, Any], key: str, field_path: str) -> str:
    value = container.get(key)
    if not isinstance(value, str) or not value:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{field_path}.{key} must be a non-empty string",
            {"field": f"{field_path}.{key}"},
        )
    return value


def _require_enum(
    container: dict[str, Any], key: str, allowed: set[str], field_path: str
) -> str:
    value = _require_string(container, key, field_path)
    if value not in allowed:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{field_path}.{key} has an unsupported value",
            {"field": f"{field_path}.{key}", "value": value, "allowed": sorted(allowed)},
        )
    return value


def _optional_string(container: dict[str, Any], key: str, field_path: str) -> str | None:
    value = container.get(key)
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{field_path}.{key} must be null or a non-empty string",
            {"field": f"{field_path}.{key}"},
        )
    return value


def _require_boolean_or_null(
    container: dict[str, Any], key: str, field_path: str
) -> bool | None:
    value = container.get(key)
    if value is not None and not isinstance(value, bool):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{field_path}.{key} must be boolean or null",
            {"field": f"{field_path}.{key}"},
        )
    return value


def _validate_timestamp(value: str | None, field_path: str) -> None:
    if value is None:
        return
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (AttributeError, ValueError) as error:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{field_path} must be an ISO-8601 timestamp",
            {"field": field_path, "value": value},
        ) from error
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{field_path} must be UTC",
            {"field": field_path, "value": value},
        )


def _raise_schema_error(message: str, context: dict[str, Any]) -> NoReturn:
    raise WakeTrialEvidenceError(
        "WAKE_TRIAL.SCHEMA_UNSUPPORTED",
        message,
        context,
    )


def _resolve_local_schema_reference(
    root_schema: dict[str, Any], reference: str
) -> dict[str, Any]:
    if not reference.startswith("#/"):
        _raise_schema_error(
            "Wake-trial validation supports local JSON Pointer references only",
            {"reference": reference},
        )
    resolved: Any = root_schema
    for encoded_component in reference[2:].split("/"):
        component = encoded_component.replace("~1", "/").replace("~0", "~")
        if isinstance(resolved, dict) and component in resolved:
            resolved = resolved[component]
            continue
        if isinstance(resolved, list) and component.isdigit() and int(component) < len(resolved):
            resolved = resolved[int(component)]
            continue
        _raise_schema_error(
            "Wake-trial schema contains an unresolved local reference",
            {"reference": reference, "component": component},
        )
    if not isinstance(resolved, dict):
        _raise_schema_error(
            "Wake-trial schema reference did not resolve to an object schema",
            {"reference": reference},
        )
    return resolved


def _assert_supported_schema(
    schema: Any,
    root_schema: dict[str, Any],
    schema_path: str,
) -> None:
    if not isinstance(schema, dict):
        _raise_schema_error(
            "Boolean and non-object JSON Schemas are not supported",
            {"schema_path": schema_path, "value_type": type(schema).__name__},
        )
    unsupported_keywords = sorted(set(schema) - SUPPORTED_SCHEMA_KEYWORDS)
    if unsupported_keywords:
        _raise_schema_error(
            "Wake-trial schema uses unsupported validation keywords",
            {"schema_path": schema_path, "unsupported_keywords": unsupported_keywords},
        )

    for metadata_keyword in ("$schema", "$id", "title", "description"):
        if metadata_keyword in schema and not isinstance(schema[metadata_keyword], str):
            _raise_schema_error(
                "Wake-trial schema metadata must be a string",
                {"schema_path": schema_path, "keyword": metadata_keyword},
            )
    if "$ref" in schema:
        if not isinstance(schema["$ref"], str):
            _raise_schema_error(
                "Wake-trial schema references must be strings",
                {"schema_path": schema_path},
            )
        _resolve_local_schema_reference(root_schema, schema["$ref"])

    allowed_type_names = {"object", "array", "string", "integer", "boolean", "null"}
    if "type" in schema:
        type_value = schema["type"]
        type_names = [type_value] if isinstance(type_value, str) else type_value
        if (
            not isinstance(type_names, list)
            or not type_names
            or any(type(type_name) is not str for type_name in type_names)
            or len(set(type_names)) != len(type_names)
            or not set(type_names).issubset(allowed_type_names)
        ):
            _raise_schema_error(
                "Wake-trial schema contains an unsupported type declaration",
                {"schema_path": schema_path, "type": type_value},
            )
    if "additionalProperties" in schema and type(schema["additionalProperties"]) is not bool:
        _raise_schema_error(
            "Only boolean additionalProperties declarations are supported",
            {"schema_path": schema_path},
        )
    if "required" in schema:
        required = schema["required"]
        if (
            not isinstance(required, list)
            or any(type(field_name) is not str for field_name in required)
            or len(set(required)) != len(required)
        ):
            _raise_schema_error(
                "Wake-trial schema required declarations must be unique strings",
                {"schema_path": schema_path},
            )
    if "enum" in schema and (not isinstance(schema["enum"], list) or not schema["enum"]):
        _raise_schema_error(
            "Wake-trial schema enum declarations must be non-empty arrays",
            {"schema_path": schema_path},
        )
    if "pattern" in schema:
        if not isinstance(schema["pattern"], str):
            _raise_schema_error(
                "Wake-trial schema patterns must be strings",
                {"schema_path": schema_path},
            )
        try:
            re.compile(schema["pattern"])
        except re.error as error:
            _raise_schema_error(
                "Wake-trial schema contains an invalid regular expression",
                {"schema_path": schema_path, "cause": str(error)},
            )
    for integer_keyword in ("minLength", "minItems"):
        if integer_keyword in schema and (
            type(schema[integer_keyword]) is not int or schema[integer_keyword] < 0
        ):
            _raise_schema_error(
                "Wake-trial schema size constraints must be nonnegative integers",
                {"schema_path": schema_path, "keyword": integer_keyword},
            )
    if "minimum" in schema and (
        type(schema["minimum"]) not in {int, float}
    ):
        _raise_schema_error(
            "Wake-trial schema minimum constraints must be numeric",
            {"schema_path": schema_path},
        )
    if "uniqueItems" in schema and type(schema["uniqueItems"]) is not bool:
        _raise_schema_error(
            "Wake-trial schema uniqueItems must be boolean",
            {"schema_path": schema_path},
        )
    if "format" in schema and schema["format"] != "date-time":
        _raise_schema_error(
            "Wake-trial schema uses an unsupported format",
            {"schema_path": schema_path, "format": schema["format"]},
        )

    for mapping_keyword in ("$defs", "properties"):
        if mapping_keyword not in schema:
            continue
        mapping = schema[mapping_keyword]
        if not isinstance(mapping, dict) or any(type(key) is not str for key in mapping):
            _raise_schema_error(
                "Wake-trial schema mappings must be string-keyed objects",
                {"schema_path": schema_path, "keyword": mapping_keyword},
            )
        for child_name, child_schema in mapping.items():
            _assert_supported_schema(
                child_schema,
                root_schema,
                f"{schema_path}/{mapping_keyword}/{child_name}",
            )
    if "items" in schema:
        _assert_supported_schema(schema["items"], root_schema, f"{schema_path}/items")
    if "allOf" in schema:
        if not isinstance(schema["allOf"], list):
            _raise_schema_error(
                "Wake-trial schema allOf must be an array",
                {"schema_path": schema_path},
            )
        for index, child_schema in enumerate(schema["allOf"]):
            _assert_supported_schema(
                child_schema, root_schema, f"{schema_path}/allOf/{index}"
            )
    for conditional_keyword in ("if", "then", "else"):
        if conditional_keyword in schema:
            _assert_supported_schema(
                schema[conditional_keyword],
                root_schema,
                f"{schema_path}/{conditional_keyword}",
            )


def _load_wake_trial_schema() -> dict[str, Any]:
    try:
        schema = json.loads(WAKE_TRIAL_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.SCHEMA_UNREADABLE",
            "Wake-trial manifest schema could not be loaded",
            {"path": str(WAKE_TRIAL_SCHEMA_PATH), "cause": str(error)},
        ) from error
    if not isinstance(schema, dict):
        _raise_schema_error(
            "Wake-trial manifest schema must be a JSON object",
            {"path": str(WAKE_TRIAL_SCHEMA_PATH)},
        )
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        _raise_schema_error(
            "Wake-trial validation supports Draft 2020-12 only",
            {"path": str(WAKE_TRIAL_SCHEMA_PATH), "$schema": schema.get("$schema")},
        )
    _assert_supported_schema(schema, schema, "#")
    return schema


def _json_schema_type_matches(value: Any, type_name: str) -> bool:
    return {
        "object": isinstance(value, dict),
        "array": isinstance(value, list),
        "string": isinstance(value, str),
        "integer": type(value) is int,
        "boolean": type(value) is bool,
        "null": value is None,
    }[type_name]


def _json_values_equal(left: Any, right: Any) -> bool:
    if type(left) is bool or type(right) is bool:
        return type(left) is type(right) and left == right
    if type(left) in {int, float} and type(right) in {int, float}:
        return left == right
    if type(left) is not type(right):
        return False
    if isinstance(left, list):
        return len(left) == len(right) and all(
            _json_values_equal(left_item, right_item)
            for left_item, right_item in zip(left, right)
        )
    if isinstance(left, dict):
        return set(left) == set(right) and all(
            _json_values_equal(left[key], right[key]) for key in left
        )
    return left == right


def _structural_schema_errors(
    value: Any,
    schema: dict[str, Any],
    root_schema: dict[str, Any],
    value_path: str = "$",
) -> list[str]:
    errors: list[str] = []
    if "$ref" in schema:
        errors.extend(
            _structural_schema_errors(
                value,
                _resolve_local_schema_reference(root_schema, schema["$ref"]),
                root_schema,
                value_path,
            )
        )

    allowed_types = schema.get("type")
    if allowed_types is not None:
        type_names = [allowed_types] if isinstance(allowed_types, str) else allowed_types
        if not any(_json_schema_type_matches(value, type_name) for type_name in type_names):
            return [f"{value_path}:TYPE_MISMATCH"]
    if "const" in schema and not _json_values_equal(value, schema["const"]):
        errors.append(f"{value_path}:CONST_MISMATCH")
    if "enum" in schema and not any(
        _json_values_equal(value, enum_value) for enum_value in schema["enum"]
    ):
        errors.append(f"{value_path}:ENUM_MISMATCH")

    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0):
            errors.append(f"{value_path}:STRING_TOO_SHORT")
        if "pattern" in schema and re.search(schema["pattern"], value) is None:
            errors.append(f"{value_path}:PATTERN_MISMATCH")
        if schema.get("format") == "date-time":
            if RFC3339_DATE_TIME_PATTERN.fullmatch(value) is None:
                errors.append(f"{value_path}:DATE_TIME_INVALID")
                return errors
            try:
                parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
            except ValueError:
                errors.append(f"{value_path}:DATE_TIME_INVALID")
            else:
                if parsed.tzinfo is None:
                    errors.append(f"{value_path}:DATE_TIME_TIMEZONE_MISSING")
    if type(value) in {int, float} and "minimum" in schema and value < schema["minimum"]:
        errors.append(f"{value_path}:BELOW_MINIMUM")

    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            errors.append(f"{value_path}:TOO_FEW_ITEMS")
        if schema.get("uniqueItems"):
            for index, item in enumerate(value):
                if any(
                    _json_values_equal(item, earlier_item)
                    for earlier_item in value[:index]
                ):
                    errors.append(f"{value_path}:DUPLICATE_ITEMS")
                    break
        if "items" in schema:
            for index, item in enumerate(value):
                errors.extend(
                    _structural_schema_errors(
                        item, schema["items"], root_schema, f"{value_path}[{index}]"
                    )
                )

    if isinstance(value, dict):
        required = schema.get("required", [])
        for field_name in required:
            if field_name not in value:
                errors.append(f"{value_path}.{field_name}:REQUIRED")
        properties = schema.get("properties", {})
        for field_name, field_schema in properties.items():
            if field_name in value:
                errors.extend(
                    _structural_schema_errors(
                        value[field_name],
                        field_schema,
                        root_schema,
                        f"{value_path}.{field_name}",
                    )
                )
        if schema.get("additionalProperties") is False:
            for field_name in sorted(set(value) - set(properties)):
                errors.append(f"{value_path}.{field_name}:ADDITIONAL_PROPERTY")

    if "if" in schema:
        condition_errors = _structural_schema_errors(
            value, schema["if"], root_schema, value_path
        )
        selected_keyword = "then" if not condition_errors else "else"
        if selected_keyword in schema:
            errors.extend(
                _structural_schema_errors(
                    value, schema[selected_keyword], root_schema, value_path
                )
            )
    for all_of_schema in schema.get("allOf", []):
        errors.extend(
            _structural_schema_errors(value, all_of_schema, root_schema, value_path)
        )
    return sorted(set(errors))


def _reject_unknown_keys(value: dict[str, Any], allowed: set[str], field_path: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            f"{field_path} contains unknown fields",
            {"field": field_path, "unknown_fields": unknown},
        )


def _load_and_validate_observation(observation_path: Path) -> dict[str, Any]:
    try:
        with observation_path.open(encoding="utf-8") as observation_file:
            observation = json.load(observation_file)
    except (OSError, json.JSONDecodeError) as error:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_UNREADABLE",
            "External-observation record could not be read",
            {"path": str(observation_path), "cause": str(error)},
        ) from error
    if not isinstance(observation, dict):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "External-observation record must be a JSON object",
            {"path": str(observation_path)},
        )
    _reject_unknown_keys(observation, OBSERVATION_KEYS, "observation")
    if type(observation.get("schema_version")) is not int or observation.get("schema_version") != 1:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.SCHEMA_VERSION_UNSUPPORTED",
            "Only wake observation schema_version 1 is supported",
            {"schema_version": observation.get("schema_version")},
        )

    correlation_id = _require_string(observation, "correlation_id", "observation")
    if not CORRELATION_ID_PATTERN.fullmatch(correlation_id):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "observation.correlation_id has an invalid format",
            {"field": "observation.correlation_id"},
        )
    _require_string(observation, "implementation", "observation")
    requested_state = _require_enum(
        observation, "requested_state", {"freeze", "mem"}, "observation"
    )
    selected_mem_sleep = _optional_string(observation, "selected_mem_sleep", "observation")
    if requested_state == "mem" and selected_mem_sleep is None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "A mem trial requires the observed selected_mem_sleep value",
            {"field": "observation.selected_mem_sleep"},
        )
    if requested_state == "freeze" and selected_mem_sleep is not None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "A freeze trial must record selected_mem_sleep as null",
            {"field": "observation.selected_mem_sleep"},
        )
    _require_enum(
        observation, "starting_level", {"asserted", "deasserted"}, "observation"
    )
    _require_enum(
        observation, "wake_edge", {"rising", "falling", "both", "level"}, "observation"
    )
    _require_string(observation, "expected_interrupt_label", "observation")
    trial_started_at_utc = _require_string(observation, "trial_started_at_utc", "observation")
    _validate_timestamp(trial_started_at_utc, "observation.trial_started_at_utc")

    external_stimulus = _require_object(observation, "external_stimulus")
    _reject_unknown_keys(
        external_stimulus,
        {"status", "stimulus_type", "observed_at_utc", "observer", "timing_marker_id"},
        "external_stimulus",
    )
    external_status = _require_enum(
        external_stimulus,
        "status",
        {"observed", "not_applied", "unobserved"},
        "external_stimulus",
    )
    for key in ("stimulus_type", "observed_at_utc", "observer", "timing_marker_id"):
        _optional_string(external_stimulus, key, "external_stimulus")
    _validate_timestamp(external_stimulus.get("observed_at_utc"), "external_stimulus.observed_at_utc")
    if external_status == "observed" and any(
        external_stimulus.get(key) is None
        for key in ("stimulus_type", "observed_at_utc", "observer", "timing_marker_id")
    ):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "Observed external stimulus requires type, time, observer, and timing marker",
            {"field": "external_stimulus"},
        )

    state_entry = _require_object(observation, "state_entry")
    _reject_unknown_keys(
        state_entry,
        {"confirmed", "observed_state", "evidence_source", "observed_at_utc"},
        "state_entry",
    )
    state_confirmed = _require_boolean_or_null(state_entry, "confirmed", "state_entry")
    for key in ("observed_state", "evidence_source", "observed_at_utc"):
        _optional_string(state_entry, key, "state_entry")
    _validate_timestamp(state_entry.get("observed_at_utc"), "state_entry.observed_at_utc")
    if state_confirmed is not None and any(
        state_entry.get(key) is None for key in ("evidence_source", "observed_at_utc")
    ):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "Observed state-entry result requires evidence source and time",
            {"field": "state_entry"},
        )
    if state_confirmed is True and state_entry.get("observed_state") is None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "Confirmed state entry requires the observed state",
            {"field": "state_entry.observed_state"},
        )

    wake_reason = _require_object(observation, "wake_reason")
    _reject_unknown_keys(
        wake_reason,
        {"status", "expected_source", "observed_source", "evidence_source"},
        "wake_reason",
    )
    wake_reason_status = _require_enum(
        wake_reason, "status", {"observed", "unobserved"}, "wake_reason"
    )
    _require_string(wake_reason, "expected_source", "wake_reason")
    for key in ("observed_source", "evidence_source"):
        _optional_string(wake_reason, key, "wake_reason")
    if wake_reason_status == "observed" and any(
        wake_reason.get(key) is None for key in ("observed_source", "evidence_source")
    ):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "Observed wake reason requires an observed source and evidence source",
            {"field": "wake_reason"},
        )

    negative_control = _require_object(observation, "negative_control")
    _reject_unknown_keys(
        negative_control,
        {"status", "companion_correlation_id", "evidence_source"},
        "negative_control",
    )
    negative_control_status = _require_enum(
        negative_control, "status", {"passed", "failed", "unobserved"}, "negative_control"
    )
    for key in ("companion_correlation_id", "evidence_source"):
        _optional_string(negative_control, key, "negative_control")
    companion_correlation_id = negative_control.get("companion_correlation_id")
    if (
        companion_correlation_id is not None
        and not CORRELATION_ID_PATTERN.fullmatch(companion_correlation_id)
    ):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "negative_control.companion_correlation_id has an invalid format",
            {"field": "negative_control.companion_correlation_id"},
        )
    if negative_control_status != "unobserved" and any(
        negative_control.get(key) is None
        for key in ("companion_correlation_id", "evidence_source")
    ):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "A negative-control result requires its companion correlation ID and evidence source",
            {"field": "negative_control"},
        )

    linux_input_event = _require_object(observation, "linux_input_event")
    _reject_unknown_keys(
        linux_input_event,
        {"status", "event_type", "event_code", "event_value", "observed_at_utc"},
        "linux_input_event",
    )
    linux_event_status = _require_enum(
        linux_input_event, "status", {"observed", "unobserved"}, "linux_input_event"
    )
    if linux_input_event.get("event_type") != "EV_SW" or linux_input_event.get("event_code") != 11:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.LINUX_INPUT_ABI_MISMATCH",
            "Linux input evidence must use EV_SW code 11 (SW_FRONT_PROXIMITY)",
            {
                "event_type": linux_input_event.get("event_type"),
                "event_code": linux_input_event.get("event_code"),
            },
        )
    event_value = linux_input_event.get("event_value")
    if event_value is not None and (type(event_value) is not int or event_value not in (0, 1)):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "linux_input_event.event_value must be 0, 1, or null",
            {"field": "linux_input_event.event_value"},
        )
    _optional_string(linux_input_event, "observed_at_utc", "linux_input_event")
    _validate_timestamp(linux_input_event.get("observed_at_utc"), "linux_input_event.observed_at_utc")
    if linux_event_status == "observed" and any(
        linux_input_event.get(key) is None for key in ("event_value", "observed_at_utc")
    ):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "Observed Linux input event requires value and time",
            {"field": "linux_input_event"},
        )

    resume_observation = _require_object(observation, "resume_observation")
    _reject_unknown_keys(
        resume_observation,
        {"status", "observed_at_utc", "sensor_level"},
        "resume_observation",
    )
    resume_status = _require_enum(
        resume_observation,
        "status",
        {"observed", "timeout", "unobserved"},
        "resume_observation",
    )
    _optional_string(resume_observation, "observed_at_utc", "resume_observation")
    _validate_timestamp(resume_observation.get("observed_at_utc"), "resume_observation.observed_at_utc")
    sensor_level = resume_observation.get("sensor_level")
    if sensor_level is not None and sensor_level not in {"asserted", "deasserted"}:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "resume_observation.sensor_level has an unsupported value",
            {"field": "resume_observation.sensor_level", "value": sensor_level},
        )
    if resume_status in {"observed", "timeout"} and resume_observation.get("observed_at_utc") is None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "Observed resume or timeout requires its observation time",
            {"field": "resume_observation.observed_at_utc"},
        )
    if resume_status == "observed" and sensor_level is None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OBSERVATION_INVALID",
            "Observed resume requires the re-read sensor level",
            {"field": "resume_observation.sensor_level"},
        )
    return observation


def _load_and_validate_expectations(expectations_path: Path) -> dict[str, Any]:
    try:
        with expectations_path.open(encoding="utf-8") as expectations_file:
            expectations = json.load(expectations_file)
    except (OSError, json.JSONDecodeError) as error:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATIONS_UNREADABLE",
            "Runner-owned trial expectations could not be read",
            {"path": str(expectations_path), "cause": str(error)},
        ) from error
    if not isinstance(expectations, dict):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATIONS_INVALID",
            "Runner-owned trial expectations must be a JSON object",
            {"path": str(expectations_path)},
        )
    _reject_unknown_keys(expectations, EXPECTATION_KEYS, "expectations")
    if type(expectations.get("schema_version")) is not int or expectations.get("schema_version") != 1:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATIONS_INVALID",
            "Only wake expectations schema_version 1 is supported",
            {"schema_version": expectations.get("schema_version")},
        )
    correlation_id = _require_string(expectations, "correlation_id", "expectations")
    if not CORRELATION_ID_PATTERN.fullmatch(correlation_id):
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATIONS_INVALID",
            "expectations.correlation_id has an invalid format",
            {"field": "expectations.correlation_id"},
        )
    _require_string(expectations, "implementation", "expectations")
    requested_state = _require_enum(
        expectations, "requested_state", {"freeze", "mem"}, "expectations"
    )
    selected_mem_sleep = _optional_string(
        expectations, "selected_mem_sleep", "expectations"
    )
    if requested_state == "mem" and selected_mem_sleep is None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATIONS_INVALID",
            "A mem expectation requires selected_mem_sleep",
            {"field": "expectations.selected_mem_sleep"},
        )
    if requested_state == "freeze" and selected_mem_sleep is not None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATIONS_INVALID",
            "A freeze expectation requires selected_mem_sleep null",
            {"field": "expectations.selected_mem_sleep"},
        )
    _require_enum(
        expectations, "starting_level", {"asserted", "deasserted"}, "expectations"
    )
    _require_enum(
        expectations, "wake_edge", {"rising", "falling", "both", "level"}, "expectations"
    )
    _require_string(expectations, "expected_wake_source", "expectations")
    _require_string(expectations, "expected_interrupt_label", "expectations")
    trial_started_at_utc = _require_string(
        expectations, "trial_started_at_utc", "expectations"
    )
    _validate_timestamp(trial_started_at_utc, "expectations.trial_started_at_utc")
    return expectations


def _mismatched_expectation_fields(
    actual_values: dict[str, Any], expected_values: dict[str, Any]
) -> list[str]:
    return sorted(
        key
        for key, expected_value in expected_values.items()
        if key != "schema_version" and actual_values.get(key) != expected_value
    )


def _validate_observation_matches_expectations(
    observation: dict[str, Any], expectations: dict[str, Any]
) -> None:
    observation_values = {
        "correlation_id": observation["correlation_id"],
        "implementation": observation["implementation"],
        "requested_state": observation["requested_state"],
        "selected_mem_sleep": observation["selected_mem_sleep"],
        "starting_level": observation["starting_level"],
        "wake_edge": observation["wake_edge"],
        "expected_wake_source": observation["wake_reason"]["expected_source"],
        "expected_interrupt_label": observation["expected_interrupt_label"],
        "trial_started_at_utc": observation["trial_started_at_utc"],
    }
    mismatched_fields = _mismatched_expectation_fields(
        observation_values, expectations
    )
    if mismatched_fields:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATION_MISMATCH",
            "External observation changed runner-owned trial expectations",
            {"mismatched_fields": mismatched_fields},
        )


def _read_counter(path: Path) -> tuple[int | None, str | None]:
    try:
        raw_value = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None, "EVIDENCE_MISSING"
    if not raw_value.isdigit():
        return None, "EVIDENCE_AMBIGUOUS"
    value = int(raw_value)
    return value, None


def _counter_delta(
    before_path: Path, after_path: Path, missing_reason_prefix: str
) -> tuple[dict[str, Any], list[str]]:
    before, before_error = _read_counter(before_path)
    after, after_error = _read_counter(after_path)
    reasons: list[str] = []
    if before_error:
        reasons.append(f"{missing_reason_prefix}_BEFORE_{before_error}")
    if after_error:
        reasons.append(f"{missing_reason_prefix}_AFTER_{after_error}")
    delta = None if before is None or after is None else after - before
    if delta is not None and delta < 0:
        reasons.append(f"{missing_reason_prefix}_COUNTER_DECREASED")
        delta = None
    return {
        "before": before,
        "after": after,
        "delta": delta,
        "status": "observed" if not reasons else "unobserved",
    }, reasons


def _parse_interrupt_counter(path: Path, expected_label: str) -> tuple[int | None, str | None]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None, "EVIDENCE_MISSING"

    matches: list[int] = []
    for line in lines:
        _, separator, right = line.partition(":")
        if not separator:
            continue
        tokens = right.split()
        counters: list[int] = []
        descriptor_start = 0
        for index, token in enumerate(tokens):
            if token.isdigit():
                counters.append(int(token))
                descriptor_start = index + 1
                continue
            break
        descriptor_tokens = tokens[descriptor_start:]
        if counters and expected_label in descriptor_tokens:
            matches.append(sum(counters))
    if len(matches) != 1:
        return None, "EVIDENCE_AMBIGUOUS" if matches else "EVIDENCE_MISSING"
    return matches[0], None


def _interrupt_delta(
    before_path: Path, after_path: Path, expected_label: str
) -> tuple[dict[str, Any], list[str]]:
    before, before_error = _parse_interrupt_counter(before_path, expected_label)
    after, after_error = _parse_interrupt_counter(after_path, expected_label)
    reasons: list[str] = []
    if before_error:
        reasons.append(f"INTERRUPT_BEFORE_{before_error}")
    if after_error:
        reasons.append(f"INTERRUPT_AFTER_{after_error}")
    delta = None if before is None or after is None else after - before
    if delta is not None and delta < 0:
        reasons.append("INTERRUPT_COUNTER_DECREASED")
        delta = None
    return {
        "status": "observed" if not reasons else "unobserved",
        "source": "/proc/interrupts",
        "expected_interrupt_label": expected_label,
        "before": before,
        "after": after,
        "delta": delta,
    }, reasons


def _read_suspend_write_result(path: Path) -> str:
    try:
        result = path.read_text(encoding="utf-8").strip()
    except OSError:
        return "unobserved"
    if result not in {"returned_after_resume", "rejected"}:
        return "unobserved"
    return result


def _read_selected_mem_sleep(path: Path) -> tuple[str | None, str | None]:
    try:
        raw_value = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None, "EVIDENCE_MISSING"
    selected_values = re.findall(r"\[([^\[\]\s]+)\]", raw_value)
    if len(selected_values) != 1:
        return None, "EVIDENCE_AMBIGUOUS"
    return selected_values[0], None


def _mem_sleep_selection(
    raw_directory: Path, requested_state: str
) -> tuple[dict[str, Any], list[str]]:
    if requested_state == "freeze":
        return {
            "status": "not_applicable",
            "source": "/sys/power/mem_sleep",
            "before": None,
            "after": None,
            "stable": None,
        }, []

    before, before_error = _read_selected_mem_sleep(raw_directory / "before/mem_sleep")
    after, after_error = _read_selected_mem_sleep(raw_directory / "after/mem_sleep")
    reasons: list[str] = []
    if before_error:
        reasons.append(f"MEM_SLEEP_BEFORE_{before_error}")
    if after_error:
        reasons.append(f"MEM_SLEEP_AFTER_{after_error}")
    stable = None if before is None or after is None else before == after
    if stable is False:
        reasons.append("MEM_SLEEP_SELECTION_CHANGED")
    return {
        "status": "observed" if not reasons else "unobserved",
        "source": "/sys/power/mem_sleep",
        "before": before,
        "after": after,
        "stable": stable,
    }, reasons


def _read_raw_timestamp(path: Path) -> tuple[str | None, str | None]:
    try:
        raw_value = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None, "EVIDENCE_MISSING"
    try:
        _validate_timestamp(raw_value, str(path))
    except WakeTrialEvidenceError:
        return None, "EVIDENCE_AMBIGUOUS"
    return raw_value, None


def _runner_timestamps(
    raw_directory: Path, expected_runner_timestamps: dict[str, str | None]
) -> tuple[dict[str, Any], list[str]]:
    timestamp_paths = {
        "before_capture_at_utc": raw_directory / "before/captured_at_utc",
        "process_returned_at_utc": raw_directory / "process_returned_at_utc",
        "after_capture_at_utc": raw_directory / "after/captured_at_utc",
    }
    timestamps: dict[str, str | None] = {}
    reasons: list[str] = []
    for field_name, timestamp_path in timestamp_paths.items():
        timestamp, timestamp_error = _read_raw_timestamp(timestamp_path)
        timestamps[field_name] = timestamp
        if timestamp_error:
            reasons.append(f"RUNNER_{field_name.upper()}_{timestamp_error}")
    mismatched_fields = sorted(
        field_name
        for field_name, expected_value in expected_runner_timestamps.items()
        if timestamps.get(field_name) != expected_value
    )
    if mismatched_fields:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.RUNNER_TIMESTAMP_ARTIFACT_MISMATCH",
            "On-disk runner timestamps changed before classification",
            {"mismatched_fields": mismatched_fields},
        )
    return {
        "status": "observed" if not reasons else "unobserved",
        **timestamps,
    }, reasons


def _parse_validated_timestamp(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def _timestamps_are_nondecreasing(values: list[str]) -> bool:
    parsed_values = [_parse_validated_timestamp(value) for value in values]
    return all(
        earlier <= later for earlier, later in zip(parsed_values, parsed_values[1:])
    )


def _expected_observed_state(
    requested_state: str, selected_mem_sleep: str | None
) -> str:
    return (
        requested_state
        if requested_state == "freeze" or selected_mem_sleep is None
        else f"mem:{selected_mem_sleep}"
    )


def _event_sensor_level(event_value: int | None) -> str | None:
    if event_value == 1:
        return "asserted"
    if event_value == 0:
        return "deasserted"
    return None


def _event_matches_declared_edge(
    starting_level: str, wake_edge: str, event_value: int | None
) -> bool:
    event_sensor_level = _event_sensor_level(event_value)
    if wake_edge == "rising":
        return starting_level == "deasserted" and event_value == 1
    if wake_edge == "falling":
        return starting_level == "asserted" and event_value == 0
    if wake_edge == "both":
        return event_sensor_level is not None and event_sensor_level != starting_level
    return event_sensor_level is not None


def _negative_control_temporal_relationship(
    control_started_at_utc: str,
    control_completed_at_utc: str,
    trial_started_at_utc: str,
    resume_observed_at_utc: str | None,
    after_capture_at_utc: str | None,
    classified_at_utc: str,
) -> str | None:
    control_started = _parse_validated_timestamp(control_started_at_utc)
    control_completed = _parse_validated_timestamp(control_completed_at_utc)
    trial_started = _parse_validated_timestamp(trial_started_at_utc)
    classified_at = _parse_validated_timestamp(classified_at_utc)
    if control_started <= control_completed <= trial_started:
        return "before_subject"
    if resume_observed_at_utc is None or after_capture_at_utc is None:
        return None
    resume_observed = _parse_validated_timestamp(resume_observed_at_utc)
    after_capture = _parse_validated_timestamp(after_capture_at_utc)
    subject_completed = max(resume_observed, after_capture)
    if subject_completed <= control_started <= control_completed <= classified_at:
        return "after_subject"
    return None


def _temporal_correlation(
    observation: dict[str, Any],
    runner_timestamps: dict[str, Any],
    classified_at_utc: str,
) -> dict[str, Any]:
    required_order = [
        "trial_started_at_utc",
        "runner_before_capture_at_utc",
        "state_entry.observed_at_utc",
        "external_stimulus.observed_at_utc",
        "linux_input_event.observed_at_utc",
        "runner_process_returned_at_utc",
        "resume_observation.observed_at_utc",
        "classified_at_utc",
    ]
    runner_order = [
        "runner_process_returned_at_utc",
        "runner_after_capture_at_utc",
        "classified_at_utc",
    ]
    ordered_values = [
        observation["trial_started_at_utc"],
        runner_timestamps["before_capture_at_utc"],
        observation["state_entry"]["observed_at_utc"],
        observation["external_stimulus"]["observed_at_utc"],
        observation["linux_input_event"]["observed_at_utc"],
        runner_timestamps["process_returned_at_utc"],
        observation["resume_observation"]["observed_at_utc"],
        classified_at_utc,
    ]
    if runner_timestamps["status"] != "observed" or any(
        value is None for value in ordered_values
    ):
        return {
            "status": "unobserved",
            "required_order": required_order,
            "runner_order": runner_order,
        }

    ordered = _timestamps_are_nondecreasing(ordered_values)
    process_returned = _parse_validated_timestamp(
        runner_timestamps["process_returned_at_utc"]
    )
    after_capture = _parse_validated_timestamp(runner_timestamps["after_capture_at_utc"])
    classified_at = _parse_validated_timestamp(classified_at_utc)
    if not ordered or not (process_returned <= after_capture <= classified_at):
        return {
            "status": "contradictory",
            "required_order": required_order,
            "runner_order": runner_order,
        }
    return {
        "status": "observed",
        "required_order": required_order,
        "runner_order": runner_order,
    }


def _derive_negative_control(
    artifact_path: Path,
    observation: dict[str, Any],
    runner_timestamps: dict[str, Any],
    classified_at_utc: str,
) -> tuple[dict[str, Any], list[str]]:
    empty_result = {
        "status": "unobserved",
        "subject_correlation_id": None,
        "companion_correlation_id": None,
        "stimulus_applied": None,
        "unexpected_wake_observed": None,
        "evidence_source": None,
        "control_started_at_utc": None,
        "control_completed_at_utc": None,
        "correlation_bound": False,
        "temporally_isolated": False,
        "temporal_relationship": None,
        "independently_verified": False,
    }
    try:
        artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return empty_result, ["NEGATIVE_CONTROL_ARTIFACT_MISSING"]
    except OSError:
        return empty_result, ["NEGATIVE_CONTROL_ARTIFACT_UNREADABLE"]
    except json.JSONDecodeError:
        return empty_result, ["NEGATIVE_CONTROL_ARTIFACT_AMBIGUOUS"]
    allowed_keys = {
        "schema_version",
        "subject_correlation_id",
        "control_correlation_id",
        "stimulus_applied",
        "unexpected_wake_observed",
        "evidence_source",
        "control_started_at_utc",
        "control_completed_at_utc",
    }
    if not isinstance(artifact, dict) or set(artifact) != allowed_keys:
        return empty_result, ["NEGATIVE_CONTROL_ARTIFACT_INVALID"]

    subject_correlation_id = artifact.get("subject_correlation_id")
    control_correlation_id = artifact.get("control_correlation_id")
    stimulus_applied = artifact.get("stimulus_applied")
    unexpected_wake_observed = artifact.get("unexpected_wake_observed")
    evidence_source = artifact.get("evidence_source")
    control_started_at_utc = artifact.get("control_started_at_utc")
    control_completed_at_utc = artifact.get("control_completed_at_utc")
    result = {
        **empty_result,
        "subject_correlation_id": subject_correlation_id,
        "companion_correlation_id": control_correlation_id,
        "stimulus_applied": stimulus_applied,
        "unexpected_wake_observed": unexpected_wake_observed,
        "evidence_source": evidence_source,
        "control_started_at_utc": control_started_at_utc,
        "control_completed_at_utc": control_completed_at_utc,
    }
    if artifact.get("schema_version") != 1:
        return result, ["NEGATIVE_CONTROL_SCHEMA_VERSION_INVALID"]
    if not all(
        isinstance(value, str) and CORRELATION_ID_PATTERN.fullmatch(value)
        for value in (subject_correlation_id, control_correlation_id)
    ):
        return result, ["NEGATIVE_CONTROL_CORRELATION_ID_INVALID"]
    if type(stimulus_applied) is not bool or type(unexpected_wake_observed) is not bool:
        return result, ["NEGATIVE_CONTROL_RESULT_UNOBSERVED"]
    if not isinstance(evidence_source, str) or not evidence_source:
        return result, ["NEGATIVE_CONTROL_EVIDENCE_SOURCE_UNOBSERVED"]
    if not all(
        isinstance(value, str) and value
        for value in (control_started_at_utc, control_completed_at_utc)
    ):
        return result, ["NEGATIVE_CONTROL_TIMESTAMP_UNOBSERVED"]
    try:
        _validate_timestamp(control_started_at_utc, "negative_control.control_started_at_utc")
        _validate_timestamp(control_completed_at_utc, "negative_control.control_completed_at_utc")
    except WakeTrialEvidenceError:
        return result, ["NEGATIVE_CONTROL_TIMESTAMP_INVALID"]

    observation_reference = observation["negative_control"]
    correlation_bound = (
        subject_correlation_id == observation["correlation_id"]
        and control_correlation_id == observation_reference["companion_correlation_id"]
        and subject_correlation_id != control_correlation_id
        and evidence_source == observation_reference["evidence_source"]
    )
    result["correlation_bound"] = correlation_bound
    if not correlation_bound:
        return result, ["NEGATIVE_CONTROL_CORRELATION_UNBOUND"]

    temporal_relationship = _negative_control_temporal_relationship(
        control_started_at_utc,
        control_completed_at_utc,
        observation["trial_started_at_utc"],
        observation["resume_observation"]["observed_at_utc"],
        runner_timestamps["after_capture_at_utc"],
        classified_at_utc,
    )
    if temporal_relationship is None:
        return result, ["NEGATIVE_CONTROL_TEMPORAL_BOUNDS_INVALID"]

    result["temporally_isolated"] = True
    result["temporal_relationship"] = temporal_relationship
    result["independently_verified"] = True
    if stimulus_applied is False and unexpected_wake_observed is False:
        result["status"] = "passed"
        return result, []
    result["status"] = "failed"
    return result, ["NEGATIVE_CONTROL_FAILED"]


def _derive_observation_consistency(
    observation: dict[str, Any], mem_sleep_selection: dict[str, Any]
) -> dict[str, dict[str, Any]]:
    """Add classifier-owned consistency results used by success constraints."""

    expected_observed_state = _expected_observed_state(
        observation["requested_state"], mem_sleep_selection["before"]
    )

    state_entry = observation["state_entry"]
    wake_reason = observation["wake_reason"]
    linux_input_event = observation["linux_input_event"]
    resume_observation = observation["resume_observation"]
    event_value = linux_input_event["event_value"]
    event_sensor_level = _event_sensor_level(event_value)
    event_matches_wake_edge = _event_matches_declared_edge(
        observation["starting_level"], observation["wake_edge"], event_value
    )

    return {
        "state_entry": {
            **state_entry,
            "matches_requested_state": (
                state_entry["confirmed"] is True
                and state_entry["observed_state"] == expected_observed_state
            ),
        },
        "wake_reason": {
            **wake_reason,
            "attributed": (
                wake_reason["status"] == "observed"
                and wake_reason["observed_source"] == wake_reason["expected_source"]
            ),
        },
        "linux_input_event": {
            **linux_input_event,
            "matches_declared_edge": (
                linux_input_event["status"] == "observed" and event_matches_wake_edge
            ),
        },
        "resume_observation": {
            **resume_observation,
            "matches_linux_event": (
                resume_observation["status"] == "observed"
                and event_sensor_level is not None
                and resume_observation["sensor_level"] == event_sensor_level
            ),
        },
    }


def _success_semantic_errors(manifest: dict[str, Any]) -> list[str]:
    if manifest["classification"] != "entered_and_attributed":
        return []

    errors: list[str] = []
    mem_sleep_selection = manifest["mem_sleep_selection"]
    requested_state = manifest["requested_state"]
    selected_mem_sleep = manifest["selected_mem_sleep"]
    expected_state = _expected_observed_state(requested_state, selected_mem_sleep)
    state_entry = manifest["state_entry"]
    state_matches = (
        state_entry["confirmed"] is True
        and state_entry["observed_state"] == expected_state
    )
    if not state_matches or state_entry["matches_requested_state"] != state_matches:
        errors.append("SUCCESS_STATE_BINDING_MISMATCH")

    if requested_state == "freeze":
        mem_sleep_matches = (
            selected_mem_sleep is None
            and mem_sleep_selection["status"] == "not_applicable"
            and mem_sleep_selection["before"] is None
            and mem_sleep_selection["after"] is None
            and mem_sleep_selection["stable"] is None
        )
    else:
        mem_sleep_matches = (
            mem_sleep_selection["status"] == "observed"
            and selected_mem_sleep == mem_sleep_selection["before"]
            and selected_mem_sleep == mem_sleep_selection["after"]
            and mem_sleep_selection["stable"] is True
        )
    if not mem_sleep_matches:
        errors.append("SUCCESS_MEM_SLEEP_BINDING_MISMATCH")

    wake_reason = manifest["wake_reason"]
    wake_attributed = (
        wake_reason["status"] == "observed"
        and wake_reason["observed_source"] == wake_reason["expected_source"]
    )
    if not wake_attributed or wake_reason["attributed"] != wake_attributed:
        errors.append("SUCCESS_WAKE_REASON_BINDING_MISMATCH")

    negative_control = manifest["negative_control"]
    correlation_bound = (
        negative_control["subject_correlation_id"] == manifest["correlation_id"]
        and negative_control["companion_correlation_id"]
        != negative_control["subject_correlation_id"]
    )
    temporal_relationship = _negative_control_temporal_relationship(
        negative_control["control_started_at_utc"],
        negative_control["control_completed_at_utc"],
        manifest["trial_started_at_utc"],
        manifest["resume_observation"]["observed_at_utc"],
        manifest["runner_timestamps"]["after_capture_at_utc"],
        manifest["classified_at_utc"],
    )
    temporally_isolated = temporal_relationship is not None
    negative_status = (
        "passed"
        if negative_control["stimulus_applied"] is False
        and negative_control["unexpected_wake_observed"] is False
        else "failed"
    )
    independently_verified = correlation_bound and temporally_isolated
    if (
        negative_control["correlation_bound"] != correlation_bound
        or not correlation_bound
    ):
        errors.append("SUCCESS_NEGATIVE_CONTROL_CORRELATION_MISMATCH")
    if (
        negative_control["temporally_isolated"] != temporally_isolated
        or negative_control["temporal_relationship"] != temporal_relationship
        or not temporally_isolated
    ):
        errors.append("SUCCESS_NEGATIVE_CONTROL_TEMPORAL_MISMATCH")
    if (
        negative_control["independently_verified"] != independently_verified
        or not independently_verified
    ):
        errors.append("SUCCESS_NEGATIVE_CONTROL_INDEPENDENCE_MISMATCH")
    if negative_control["status"] != negative_status or negative_status != "passed":
        errors.append("SUCCESS_NEGATIVE_CONTROL_RESULT_MISMATCH")

    linux_input_event = manifest["linux_input_event"]
    edge_matches = (
        linux_input_event["status"] == "observed"
        and _event_matches_declared_edge(
            manifest["starting_level"],
            manifest["wake_edge"],
            linux_input_event["event_value"],
        )
    )
    if (
        linux_input_event["matches_declared_edge"] != edge_matches
        or not edge_matches
    ):
        errors.append("SUCCESS_LINUX_EVENT_EDGE_MISMATCH")

    resume_observation = manifest["resume_observation"]
    resume_matches = (
        resume_observation["status"] == "observed"
        and resume_observation["sensor_level"]
        == _event_sensor_level(linux_input_event["event_value"])
    )
    if resume_observation["matches_linux_event"] != resume_matches or not resume_matches:
        errors.append("SUCCESS_RESUME_EVENT_MISMATCH")

    suspend_stat_delta = manifest["suspend_stat_delta"]
    success_counter = suspend_stat_delta["success"]
    fail_counter = suspend_stat_delta["fail"]
    if (
        success_counter["after"] - success_counter["before"]
        != success_counter["delta"]
        or success_counter["delta"] != 1
    ):
        errors.append("SUCCESS_SUSPEND_SUCCESS_COUNTER_MISMATCH")
    if (
        fail_counter["after"] - fail_counter["before"] != fail_counter["delta"]
        or fail_counter["delta"] != 0
    ):
        errors.append("SUCCESS_SUSPEND_FAIL_COUNTER_MISMATCH")
    interrupt_delta = manifest["interrupt_delta"]
    if (
        interrupt_delta["after"] - interrupt_delta["before"]
        != interrupt_delta["delta"]
        or interrupt_delta["delta"] <= 0
    ):
        errors.append("SUCCESS_INTERRUPT_COUNTER_MISMATCH")
    if manifest["expected_interrupt_label"] != interrupt_delta["expected_interrupt_label"]:
        errors.append("SUCCESS_INTERRUPT_LABEL_MISMATCH")

    main_temporal_values = [
        manifest["trial_started_at_utc"],
        manifest["runner_timestamps"]["before_capture_at_utc"],
        state_entry["observed_at_utc"],
        manifest["external_stimulus"]["observed_at_utc"],
        linux_input_event["observed_at_utc"],
        manifest["runner_timestamps"]["process_returned_at_utc"],
        resume_observation["observed_at_utc"],
        manifest["classified_at_utc"],
    ]
    runner_temporal_values = [
        manifest["runner_timestamps"]["process_returned_at_utc"],
        manifest["runner_timestamps"]["after_capture_at_utc"],
        manifest["classified_at_utc"],
    ]
    if (
        not _timestamps_are_nondecreasing(main_temporal_values)
        or not _timestamps_are_nondecreasing(runner_temporal_values)
        or manifest["temporal_correlation"]["status"] != "observed"
    ):
        errors.append("SUCCESS_TEMPORAL_ORDER_MISMATCH")
    return sorted(set(errors))


def validate_wake_trial_manifest(
    manifest: Any,
    *,
    validation_scope: Literal["structural", "acceptance"] = "acceptance",
) -> list[str]:
    """Validate one manifest structurally and, for acceptance, recompute semantics.

    The structural scope implements every keyword currently used by the v1
    schema and raises if that schema grows an unsupported keyword. Acceptance
    additionally recomputes cross-field success bindings that Draft 2020-12
    cannot express.
    """

    if validation_scope not in {"structural", "acceptance"}:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.VALIDATION_SCOPE_INVALID",
            "Wake-trial manifest validation scope is unsupported",
            {"validation_scope": validation_scope},
        )
    schema = _load_wake_trial_schema()
    structural_errors = _structural_schema_errors(manifest, schema, schema)
    if structural_errors or validation_scope == "structural":
        return structural_errors
    return _success_semantic_errors(manifest)


def _classify(
    observation: dict[str, Any],
    suspend_write_result: str,
    suspend_stat_delta: dict[str, Any],
    interrupt_delta: dict[str, Any],
    mem_sleep_selection: dict[str, Any],
    runner_timestamps: dict[str, Any],
    temporal_correlation: dict[str, Any],
    negative_control: dict[str, Any],
    evidence_reasons: list[str],
) -> tuple[str, list[str]]:
    reasons: list[str] = []
    state_entry = observation["state_entry"]
    resume_observation = observation["resume_observation"]
    wake_reason = observation["wake_reason"]
    external_stimulus = observation["external_stimulus"]
    linux_input_event = observation["linux_input_event"]
    fail_delta = suspend_stat_delta["fail"]["delta"]

    if suspend_write_result == "rejected":
        reasons.append("SUSPEND_WRITE_REJECTED")
    if state_entry["confirmed"] is False:
        reasons.append("STATE_ENTRY_NOT_ENTERED")
    if fail_delta is not None and fail_delta > 0:
        reasons.append("SUSPEND_FAIL_COUNTER_INCREASED")
    if reasons:
        return "aborted", reasons

    if resume_observation["status"] == "timeout":
        return "timeout", ["RESUME_TIMEOUT"]

    state_and_resume_observed = (
        state_entry["confirmed"] is True and resume_observation["status"] == "observed"
    )
    if state_and_resume_observed:
        if negative_control["status"] == "failed":
            reasons.append("NEGATIVE_CONTROL_FAILED")
        if (
            wake_reason["status"] == "observed"
            and wake_reason["observed_source"] != wake_reason["expected_source"]
        ):
            reasons.append("WAKE_REASON_MISMATCH")
        if external_stimulus["status"] == "not_applied":
            reasons.append("WAKE_WITHOUT_EXTERNAL_STIMULUS")
    if reasons:
        return "wrong_source", reasons

    success_checks = {
        "SUSPEND_WRITE_RESULT_UNOBSERVED": suspend_write_result == "returned_after_resume",
        "STATE_ENTRY_UNCONFIRMED": state_entry["confirmed"] is True,
        "STATE_ENTRY_OBSERVED_STATE_MISMATCH": state_entry["matches_requested_state"],
        "MEM_SLEEP_SELECTION_UNOBSERVED": (
            mem_sleep_selection["status"]
            == ("not_applicable" if observation["requested_state"] == "freeze" else "observed")
        ),
        "MEM_SLEEP_OBSERVATION_MISMATCH": (
            observation["selected_mem_sleep"]
            == (
                None
                if observation["requested_state"] == "freeze"
                else mem_sleep_selection["before"]
            )
        ),
        "RUNNER_TIMESTAMPS_UNOBSERVED": runner_timestamps["status"] == "observed",
        "TEMPORAL_CORRELATION_INVALID": temporal_correlation["status"] == "observed",
        "SUSPEND_STAT_DELTA_UNOBSERVED": suspend_stat_delta["status"] == "observed",
        "SUSPEND_SUCCESS_DELTA_NOT_EXACTLY_ONE": suspend_stat_delta["success"]["delta"] == 1,
        "SUSPEND_FAIL_DELTA_NOT_ZERO": suspend_stat_delta["fail"]["delta"] == 0,
        "INTERRUPT_DELTA_UNOBSERVED": interrupt_delta["status"] == "observed",
        "EXPECTED_INTERRUPT_NOT_OBSERVED": (
            interrupt_delta["delta"] is not None and interrupt_delta["delta"] > 0
        ),
        "EXTERNAL_STIMULUS_UNOBSERVED": external_stimulus["status"] == "observed",
        "WAKE_REASON_UNOBSERVED": wake_reason["status"] == "observed",
        "WAKE_REASON_NOT_ATTRIBUTED": wake_reason["attributed"],
        "NEGATIVE_CONTROL_UNOBSERVED": negative_control["status"] == "passed",
        "NEGATIVE_CONTROL_NOT_INDEPENDENT": (
            negative_control["independently_verified"] is True
        ),
        "LINUX_INPUT_EVENT_UNOBSERVED": linux_input_event["status"] == "observed",
        "LINUX_EVENT_WAKE_EDGE_MISMATCH": linux_input_event["matches_declared_edge"],
        "RESUME_UNOBSERVED": resume_observation["status"] == "observed",
        "RESUME_SENSOR_LEVEL_MISMATCH": resume_observation["matches_linux_event"],
    }
    reasons.extend(reason for reason, passed in success_checks.items() if not passed)
    reasons.extend(evidence_reasons)
    if reasons:
        return "unobserved", sorted(set(reasons))
    return "entered_and_attributed", ["ALL_ATTRIBUTION_EVIDENCE_CONFIRMED"]


def classify_wake_trial(
    evidence_directory: Path,
    expected_trial: dict[str, Any],
    expected_runner_timestamps: dict[str, str | None],
) -> dict[str, Any]:
    """Return one v1 wake-trial manifest computed from a correlated evidence directory.

    Args:
        evidence_directory: Directory containing ``observation.json`` and the
            ``raw/before`` and ``raw/after`` evidence trees.
        expected_trial: Complete in-memory matrix cell supplied by the runner;
            neither on-disk JSON file is trusted to redefine it.
        expected_runner_timestamps: In-memory before/return/after timestamps
            supplied by the runner to detect replacement of raw anchors.

    Returns:
        A JSON-serializable manifest with recomputed deltas and one of the five
        wake-trial classifications.

    Raises:
        WakeTrialEvidenceError: The observation record is absent, malformed, or
            violates the v1 evidence contract. Missing raw counters instead
            produce an explicit ``unobserved`` classification.
    """

    _load_and_validate_expectations_from_value(expected_trial)
    expected_timestamp_keys = {
        "before_capture_at_utc",
        "process_returned_at_utc",
        "after_capture_at_utc",
    }
    if set(expected_runner_timestamps) != expected_timestamp_keys:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.RUNNER_TIMESTAMPS_INVALID",
            "Expected runner timestamps have an invalid field set",
            {"fields": sorted(expected_runner_timestamps)},
        )
    for field_name, timestamp_value in expected_runner_timestamps.items():
        if timestamp_value is not None:
            _validate_timestamp(timestamp_value, f"expected_runner_timestamps.{field_name}")
    if expected_runner_timestamps["before_capture_at_utc"] is None:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.RUNNER_TIMESTAMPS_INVALID",
            "Runner before-capture timestamp is required",
            {"field": "before_capture_at_utc"},
        )
    returned_is_recorded = expected_runner_timestamps["process_returned_at_utc"] is not None
    after_is_recorded = expected_runner_timestamps["after_capture_at_utc"] is not None
    if returned_is_recorded != after_is_recorded:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.RUNNER_TIMESTAMPS_INVALID",
            "Runner return and after-capture timestamps must both be present or absent",
            {},
        )
    expectations = _load_and_validate_expectations(evidence_directory / "expectations.json")
    artifact_mismatches = _mismatched_expectation_fields(expectations, expected_trial)
    if artifact_mismatches:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.EXPECTATIONS_ARTIFACT_MISMATCH",
            "On-disk expectations changed after the runner prepared the trial",
            {"mismatched_fields": artifact_mismatches},
        )
    observation = _load_and_validate_observation(evidence_directory / "observation.json")
    _validate_observation_matches_expectations(observation, expectations)
    classified_at_utc = _utc_now()
    raw_directory = evidence_directory / "raw"
    success_delta, success_reasons = _counter_delta(
        raw_directory / "before/suspend_stats/success",
        raw_directory / "after/suspend_stats/success",
        "SUSPEND_SUCCESS",
    )
    fail_delta, fail_reasons = _counter_delta(
        raw_directory / "before/suspend_stats/fail",
        raw_directory / "after/suspend_stats/fail",
        "SUSPEND_FAIL",
    )
    suspend_stat_delta = {
        "status": (
            "observed"
            if success_delta["status"] == "observed" and fail_delta["status"] == "observed"
            else "unobserved"
        ),
        "source": "/sys/power/suspend_stats",
        "success": success_delta,
        "fail": fail_delta,
    }
    expected_interrupt_label = _require_string(
        observation, "expected_interrupt_label", "observation"
    )
    interrupt_delta, interrupt_reasons = _interrupt_delta(
        raw_directory / "before/proc_interrupts",
        raw_directory / "after/proc_interrupts",
        expected_interrupt_label,
    )
    mem_sleep_selection, mem_sleep_reasons = _mem_sleep_selection(
        raw_directory, observation["requested_state"]
    )
    runner_timestamps, runner_timestamp_reasons = _runner_timestamps(
        raw_directory, expected_runner_timestamps
    )
    derived_observations = _derive_observation_consistency(
        observation, mem_sleep_selection
    )
    manifest_observation = {**observation, **derived_observations}
    temporal_correlation = _temporal_correlation(
        manifest_observation, runner_timestamps, classified_at_utc
    )
    negative_control, negative_control_reasons = _derive_negative_control(
        evidence_directory / "negative_control.json",
        manifest_observation,
        runner_timestamps,
        classified_at_utc,
    )
    suspend_write_result = _read_suspend_write_result(raw_directory / "suspend_write_result")
    evidence_reasons = (
        success_reasons
        + fail_reasons
        + interrupt_reasons
        + mem_sleep_reasons
        + runner_timestamp_reasons
        + negative_control_reasons
    )
    classification, classification_reasons = _classify(
        manifest_observation,
        suspend_write_result,
        suspend_stat_delta,
        interrupt_delta,
        mem_sleep_selection,
        runner_timestamps,
        temporal_correlation,
        negative_control,
        evidence_reasons,
    )
    manifest = {
        **manifest_observation,
        "negative_control": negative_control,
        "classified_at_utc": classified_at_utc,
        "suspend_write_result": suspend_write_result,
        "suspend_stat_delta": suspend_stat_delta,
        "interrupt_delta": interrupt_delta,
        "mem_sleep_selection": mem_sleep_selection,
        "runner_timestamps": runner_timestamps,
        "temporal_correlation": temporal_correlation,
        "classification": classification,
        "classification_reasons": classification_reasons,
    }
    manifest_validation_errors = validate_wake_trial_manifest(manifest)
    if manifest_validation_errors:
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.MANIFEST_INTERNAL_INVALID",
            "Freshly computed wake-trial manifest violates its acceptance contract",
            {
                "correlation_id": manifest["correlation_id"],
                "validation_errors": manifest_validation_errors,
            },
        )
    return manifest


def summarize_wake_trial_manifests(
    trials_directory: Path,
    expected_count: int,
    classification_error_count: int,
) -> tuple[dict[str, Any], bool]:
    """Aggregate every retained trial manifest and report whether all succeeded."""

    classification_counts = {classification: 0 for classification in CLASSIFICATIONS}
    invalid_manifests: list[dict[str, Any]] = []
    correlation_ids: set[str] = set()
    manifest_paths = sorted(trials_directory.glob("trial-*/wake_trial_manifest.json"))
    for manifest_path in manifest_paths:
        relative_manifest_path = str(manifest_path.relative_to(trials_directory))
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            invalid_manifests.append(
                {
                    "manifest_path": relative_manifest_path,
                    "validation_errors": [f"MANIFEST_UNREADABLE:{type(error).__name__}"],
                }
            )
            continue
        manifest_validation_errors = validate_wake_trial_manifest(manifest)
        if manifest_validation_errors:
            invalid_manifests.append(
                {
                    "manifest_path": relative_manifest_path,
                    "validation_errors": manifest_validation_errors,
                }
            )
            continue
        classification = manifest.get("classification")
        correlation_id = manifest.get("correlation_id")
        if correlation_id in correlation_ids:
            invalid_manifests.append(
                {
                    "manifest_path": relative_manifest_path,
                    "validation_errors": ["DUPLICATE_CORRELATION_ID"],
                }
            )
            continue
        correlation_ids.add(correlation_id)
        classification_counts[classification] += 1

    manifest_error_count = len(invalid_manifests)
    valid_manifest_count = sum(classification_counts.values())
    all_succeeded = (
        expected_count > 0
        and valid_manifest_count == expected_count
        and classification_error_count == 0
        and manifest_error_count == 0
        and classification_counts["entered_and_attributed"] == expected_count
    )
    record = {
        "timestamp": _utc_now(),
        "level": "info" if all_succeeded else "error",
        "message": "wake_trial_summary",
        "service": "hardware_tests.suspend_wake",
        "trace_id": f"wake-summary-{os.getpid()}",
        "expected_count": expected_count,
        "manifest_count": len(manifest_paths),
        "valid_manifest_count": valid_manifest_count,
        "classification_error_count": classification_error_count,
        "manifest_error_count": manifest_error_count,
        "invalid_manifests": invalid_manifests,
        **{f"{classification}_count": count for classification, count in classification_counts.items()},
        "all_entered_and_attributed": all_succeeded,
    }
    return record, all_succeeded


def create_observation_template(arguments: argparse.Namespace) -> dict[str, Any]:
    """Return an explicit, unobserved v1 record for an external evidence recorder.

    Every unknown observation is represented as null or ``unobserved``. The
    caller must replace those values with measured evidence; this function never
    supplies hardware defaults.
    """

    return {
        "schema_version": 1,
        "correlation_id": arguments.correlation_id,
        "implementation": arguments.implementation,
        "requested_state": arguments.requested_state,
        "selected_mem_sleep": arguments.selected_mem_sleep,
        "starting_level": arguments.starting_level,
        "wake_edge": arguments.wake_edge,
        "expected_interrupt_label": arguments.expected_interrupt_label,
        "trial_started_at_utc": arguments.trial_started_at_utc,
        "external_stimulus": {
            "status": "unobserved",
            "stimulus_type": None,
            "observed_at_utc": None,
            "observer": None,
            "timing_marker_id": None,
        },
        "state_entry": {
            "confirmed": None,
            "observed_state": None,
            "evidence_source": None,
            "observed_at_utc": None,
        },
        "wake_reason": {
            "status": "unobserved",
            "expected_source": arguments.expected_wake_source,
            "observed_source": None,
            "evidence_source": None,
        },
        "negative_control": {
            "status": "unobserved",
            "companion_correlation_id": None,
            "evidence_source": None,
        },
        "linux_input_event": {
            "status": "unobserved",
            "event_type": "EV_SW",
            "event_code": 11,
            "event_value": None,
            "observed_at_utc": None,
        },
        "resume_observation": {
            "status": "unobserved",
            "observed_at_utc": None,
            "sensor_level": None,
        },
    }


def create_trial_expectations(arguments: argparse.Namespace) -> dict[str, Any]:
    """Return the immutable matrix cell owned by the suspend runner."""

    return {
        "schema_version": 1,
        "correlation_id": arguments.correlation_id,
        "implementation": arguments.implementation,
        "requested_state": arguments.requested_state,
        "selected_mem_sleep": arguments.selected_mem_sleep,
        "starting_level": arguments.starting_level,
        "wake_edge": arguments.wake_edge,
        "expected_wake_source": arguments.expected_wake_source,
        "expected_interrupt_label": arguments.expected_interrupt_label,
        "trial_started_at_utc": arguments.trial_started_at_utc,
    }


def expected_trial_from_classify_arguments(arguments: argparse.Namespace) -> dict[str, Any]:
    """Return the complete runner-owned matrix cell carried in classifier arguments."""

    return {
        "schema_version": 1,
        "correlation_id": arguments.expected_correlation_id,
        "implementation": arguments.expected_implementation,
        "requested_state": arguments.expected_requested_state,
        "selected_mem_sleep": arguments.expected_selected_mem_sleep,
        "starting_level": arguments.expected_starting_level,
        "wake_edge": arguments.expected_wake_edge,
        "expected_wake_source": arguments.expected_wake_source,
        "expected_interrupt_label": arguments.expected_interrupt_label,
        "trial_started_at_utc": arguments.expected_trial_started_at_utc,
    }


def expected_runner_timestamps_from_classify_arguments(
    arguments: argparse.Namespace,
) -> dict[str, str | None]:
    """Return runner timestamps carried in classifier arguments, not read from disk."""

    return {
        "before_capture_at_utc": arguments.expected_before_capture_at_utc,
        "process_returned_at_utc": arguments.expected_process_returned_at_utc,
        "after_capture_at_utc": arguments.expected_after_capture_at_utc,
    }


def _write_json_atomic(path: Path, value: dict[str, Any], allow_overwrite: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not allow_overwrite and path.exists():
        raise WakeTrialEvidenceError(
            "WAKE_TRIAL.OUTPUT_EXISTS",
            "Refusing to overwrite an existing observation record",
            {"path": str(path)},
        )
    temporary_file_descriptor, temporary_path_string = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent
    )
    temporary_path = Path(temporary_path_string)
    try:
        with os.fdopen(temporary_file_descriptor, "w", encoding="utf-8") as output_file:
            json.dump(value, output_file, indent=2, sort_keys=True)
            output_file.write("\n")
            output_file.flush()
            os.fsync(output_file.fileno())
        os.replace(temporary_path, path)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def _build_argument_parser() -> WakeTrialArgumentParser:
    parser = WakeTrialArgumentParser(
        description="Create or classify correlated suspend/wake evidence."
    )
    commands = parser.add_subparsers(dest="command", required=True)

    classify_parser = commands.add_parser("classify")
    classify_parser.add_argument("--evidence-directory", type=Path, required=True)
    classify_parser.add_argument("--output", type=Path)
    classify_parser.add_argument("--expected-correlation-id", required=True)
    classify_parser.add_argument("--expected-implementation", required=True)
    classify_parser.add_argument(
        "--expected-requested-state", choices=("freeze", "mem"), required=True
    )
    classify_parser.add_argument("--expected-selected-mem-sleep")
    classify_parser.add_argument(
        "--expected-starting-level",
        choices=("asserted", "deasserted"),
        required=True,
    )
    classify_parser.add_argument(
        "--expected-wake-edge",
        choices=("rising", "falling", "both", "level"),
        required=True,
    )
    classify_parser.add_argument("--expected-wake-source", required=True)
    classify_parser.add_argument("--expected-interrupt-label", required=True)
    classify_parser.add_argument("--expected-trial-started-at-utc", required=True)
    classify_parser.add_argument("--expected-before-capture-at-utc", required=True)
    classify_parser.add_argument("--expected-process-returned-at-utc")
    classify_parser.add_argument("--expected-after-capture-at-utc")
    classify_parser.add_argument("--require-success", action="store_true")

    template_parser = commands.add_parser("create-template")
    template_parser.add_argument("--output", type=Path, required=True)
    template_parser.add_argument("--expectations-output", type=Path, required=True)
    template_parser.add_argument("--correlation-id", required=True)
    template_parser.add_argument("--implementation", required=True)
    template_parser.add_argument("--requested-state", choices=("freeze", "mem"), required=True)
    template_parser.add_argument("--selected-mem-sleep")
    template_parser.add_argument(
        "--starting-level", choices=("asserted", "deasserted"), required=True
    )
    template_parser.add_argument(
        "--wake-edge", choices=("rising", "falling", "both", "level"), required=True
    )
    template_parser.add_argument("--expected-wake-source", required=True)
    template_parser.add_argument("--expected-interrupt-label", required=True)
    template_parser.add_argument("--trial-started-at-utc", required=True)

    summary_parser = commands.add_parser("summarize")
    summary_parser.add_argument("--trials-directory", type=Path, required=True)
    summary_parser.add_argument("--expected-count", type=int, required=True)
    summary_parser.add_argument("--classification-error-count", type=int, default=0)
    return parser


def _emit_error(error: WakeTrialEvidenceError, stack_trace: str) -> None:
    record = {
        "timestamp": _utc_now(),
        "level": "error",
        "message": "wake_trial_operation_failed",
        "service": "hardware_tests.wake_trial_classifier",
        "trace_id": f"wake-trial-{os.getpid()}",
        "error_code": error.code,
        "error_message": error.message,
        "context": error.context,
        "stack_trace": stack_trace,
    }
    print(json.dumps(record, sort_keys=True), file=sys.stderr)


def main() -> int:
    """Run the classifier command and return a stable process exit status."""

    try:
        arguments = _build_argument_parser().parse_args()
        if arguments.command == "create-template":
            if arguments.output == arguments.expectations_output:
                raise WakeTrialEvidenceError(
                    "WAKE_TRIAL.OUTPUT_PATH_COLLISION",
                    "Observation and expectations outputs must be different paths",
                    {"path": str(arguments.output)},
                )
            expectations = create_trial_expectations(arguments)
            template = create_observation_template(arguments)
            _load_and_validate_expectations_from_value(expectations)
            _load_and_validate_observation_from_value(template)
            _write_json_atomic(
                arguments.expectations_output, expectations, allow_overwrite=False
            )
            arguments.expectations_output.chmod(0o400)
            _write_json_atomic(arguments.output, template, allow_overwrite=False)
            return 0

        if arguments.command == "summarize":
            if arguments.expected_count <= 0 or arguments.classification_error_count < 0:
                raise WakeTrialEvidenceError(
                    "WAKE_TRIAL.SUMMARY_ARGUMENT_INVALID",
                    "Summary counts must be nonnegative and expected_count must be positive",
                    {
                        "expected_count": arguments.expected_count,
                        "classification_error_count": arguments.classification_error_count,
                    },
                )
            summary, all_succeeded = summarize_wake_trial_manifests(
                arguments.trials_directory,
                arguments.expected_count,
                arguments.classification_error_count,
            )
            print(json.dumps(summary, sort_keys=True))
            return 0 if all_succeeded else 1

        expected_trial = expected_trial_from_classify_arguments(arguments)
        expected_runner_timestamps = expected_runner_timestamps_from_classify_arguments(
            arguments
        )
        manifest = classify_wake_trial(
            arguments.evidence_directory,
            expected_trial,
            expected_runner_timestamps,
        )
        output_path = arguments.output or arguments.evidence_directory / "wake_trial_manifest.json"
        _write_json_atomic(output_path, manifest, allow_overwrite=True)
        print(manifest["classification"])
        if arguments.require_success and manifest["classification"] != "entered_and_attributed":
            raise WakeTrialEvidenceError(
                "WAKE_TRIAL.CLASSIFICATION_NOT_SUCCESS",
                "Wake trial did not enter and attribute the requested state",
                {
                    "correlation_id": manifest["correlation_id"],
                    "classification": manifest["classification"],
                    "classification_reasons": manifest["classification_reasons"],
                },
            )
        return 0
    except WakeTrialEvidenceError as error:
        _emit_error(error, "unavailable")
        return 1
    except Exception as error:  # noqa: BLE001 -- converts internal faults into a structured CLI error.
        wrapped_error = WakeTrialEvidenceError(
            "WAKE_TRIAL.INTERNAL_FAILURE",
            "Wake-trial classification failed unexpectedly",
            {"cause_type": type(error).__name__, "cause": str(error)},
        )
        _emit_error(wrapped_error, traceback.format_exc())
        return 1


def _load_and_validate_observation_from_value(observation: dict[str, Any]) -> None:
    temporary_directory = Path(tempfile.mkdtemp(prefix="wake-trial-template-validation-"))
    observation_path = temporary_directory / "observation.json"
    try:
        observation_path.write_text(json.dumps(observation), encoding="utf-8")
        _load_and_validate_observation(observation_path)
    finally:
        observation_path.unlink(missing_ok=True)
        temporary_directory.rmdir()


def _load_and_validate_expectations_from_value(expectations: dict[str, Any]) -> None:
    temporary_directory = Path(tempfile.mkdtemp(prefix="wake-trial-expectation-validation-"))
    expectations_path = temporary_directory / "expectations.json"
    try:
        expectations_path.write_text(json.dumps(expectations), encoding="utf-8")
        _load_and_validate_expectations(expectations_path)
    finally:
        expectations_path.unlink(missing_ok=True)
        temporary_directory.rmdir()


if __name__ == "__main__":
    raise SystemExit(main())
