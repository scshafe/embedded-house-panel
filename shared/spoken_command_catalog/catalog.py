"""@canonical spoken_command_catalog: fixture-only language and effect authority.

No acoustic qualification, real recognizer, or provider operation is selected.
All identities cover canonical integer-only JSON with Unicode NFC strings.
"""
from __future__ import annotations
import hashlib
import itertools
import json
import re
import unicodedata
from pathlib import Path
from typing import Any

EFFECTS = {"reversible_local", "bounded_learning_observation", "correctable_learning",
           "capture_stop", "capture_start", "external_submission", "power_or_illumination"}
RECOGNIZER_ID = "deterministic-fake-v1/no-acoustic-model"
METRIC_VERSION = "nfc-casefold-punctuation-space-arpabet-unit-levenshtein-v1"
CONTEXTS = {"study_front", "study_back", "study_history", "agenda_selected", "wish_recording"}
ARPABET = set("AA AE AH AO AW AY B CH D DH EH ER EY F G HH IH IY JH K L M N NG OW OY P R S SH T TH UH UW V W Y Z ZH".split())

class CatalogError(ValueError):
    """Structured redacted boundary error; supplied text is never included."""
    def __init__(self, code: str, operation: str):
        self.code, self.operation = code, operation
        super().__init__(code)

def fail(code: str, operation: str) -> None:
    raise CatalogError("VOICE_COMMAND." + code, operation)

def canonical(value: Any) -> bytes:
    """Serialize strict NFC/integer JSON deterministically; reject floats/NaN/unknown types."""
    def check(item: Any) -> None:
        if item is None or isinstance(item, bool):
            return
        if isinstance(item, int):
            if abs(item) > 9007199254740991:
                fail("JSON_INTEGER_INVALID", "canonical")
        elif isinstance(item, str):
            if unicodedata.normalize("NFC", item) != item:
                fail("JSON_UNICODE_INVALID", "canonical")
        elif isinstance(item, list):
            for child in item:
                check(child)
        elif isinstance(item, dict) and all(isinstance(key, str) for key in item):
            for key, child in item.items():
                check(key); check(child)
        else:
            fail("JSON_TYPE_INVALID", "canonical")
    check(value)
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()

def digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical(value)).hexdigest()

def load_json(path: Path) -> Any:
    """Load bounded JSON with duplicate-key rejection and structured errors."""
    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result = {}
        for key, value in items:
            if key in result:
                fail("JSON_DUPLICATE_KEY", "load_json")
            result[key] = value
        return result
    try:
        data = path.read_bytes()
        if len(data) > 1024 * 1024:
            fail("JSON_TOO_LARGE", "load_json")
        value = json.loads(data, object_pairs_hook=pairs)
        canonical(value)
        return value
    except (OSError, UnicodeError, json.JSONDecodeError):
        fail("JSON_INVALID", "load_json")

def exact(value: Any, keys: set[str], operation: str) -> None:
    if not isinstance(value, dict) or set(value) != keys:
        fail("SCHEMA_INVALID", operation)

def normalize_text(text: str) -> list[str]:
    """NFC/casefold; every Unicode punctuation character becomes a token separator."""
    text = unicodedata.normalize("NFC", text).casefold()
    return "".join(" " if unicodedata.category(c).startswith("P") else c for c in text).split()

def normalize_phones(phones: list[str], strip_stress: bool = True) -> list[str]:
    return [re.sub(r"[012]$", "", phone.upper()) if strip_stress else phone.upper() for phone in phones]

def metrics(a: list[str], b: list[str]) -> dict[str, Any]:
    """Exact rational vector avoids floating-point ambiguity in golden reports."""
    row = list(range(len(b) + 1))
    for i, left in enumerate(a, 1):
        nxt = [i]
        for j, right in enumerate(b, 1):
            nxt.append(min(nxt[-1] + 1, row[j] + 1, row[j - 1] + (left != right)))
        row = nxt
    distance = row[-1]
    longest, shortest = max(len(a), len(b)), min(len(a), len(b))
    prefix = sum(1 for _ in itertools.takewhile(lambda pair: pair[0] == pair[1], zip(a, b)))
    suffix = sum(1 for _ in itertools.takewhile(lambda pair: pair[0] == pair[1], zip(reversed(a), reversed(b))))
    small, large = (a, b) if len(a) <= len(b) else (b, a)
    contains = bool(small) and any(large[i:i + len(small)] == small for i in range(len(large) - len(small) + 1))
    empty = int(not a and not b)
    return {"distance": distance, "similarity": [longest - distance, longest] if longest else [1, 1],
            "prefix": [prefix, shortest] if shortest else [empty, 1],
            "suffix": [suffix, shortest] if shortest else [empty, 1], "containment": contains or bool(empty)}

def pair_report(left: dict, right: dict, invocation: dict) -> dict:
    """Worst-case Cartesian pronunciation metrics, including explicit invocation stripping."""
    vectors = []
    prefix = normalize_phones(invocation["phones"])
    for a, b in itertools.product(left["pronunciations"], right["pronunciations"]):
        ap, bp = normalize_phones(a["phones"]), normalize_phones(b["phones"])
        if ap[:len(prefix)] != prefix or bp[:len(prefix)] != prefix:
            fail("INVOCATION_PRONUNCIATION_INVALID", "pair_report")
        vectors.append({"variant_ids": [a["variant_id"], b["variant_id"]], "full": metrics(ap, bp),
                        "payload": metrics(ap[len(prefix):], bp[len(prefix):]),
                        "stressed_collision": normalize_phones(a["phones"], False) == normalize_phones(b["phones"], False)})
    similarity = lambda pair: pair[0] / pair[1]
    worst = max(vectors, key=lambda v: similarity(v["payload"]["similarity"]))
    triggered = any(v["payload"]["distance"] <= 1 or similarity(v["payload"]["similarity"]) >= .8
                    or similarity(v["payload"]["prefix"]) >= .6 or similarity(v["payload"]["suffix"]) >= .6
                    or v["payload"]["containment"] for v in vectors)
    return {"form_ids": [left["spoken_form_id"], right["spoken_form_id"]],
            "context_overlap": bool(set(left["contexts"]) & set(right["contexts"])),
            "text_collision": normalize_text(left["phrase"]) == normalize_text(right["phrase"]),
            "phone_collision": any(v["full"]["distance"] == 0 for v in vectors),
            "minimum_distance": min(v["payload"]["distance"] for v in vectors),
            "worst_similarity_variant_ids": worst["variant_ids"], "vectors": vectors,
            "requires_review": triggered}

def validate_catalog(catalog: dict) -> list[dict]:
    exact(catalog, {"catalog_schema_version", "catalog_revision", "language_tag", "status", "invocation", "intents", "pair_reviews", "acoustic_evidence"}, "catalog")
    unsigned = {key: value for key, value in catalog.items() if key != "catalog_revision"}
    if catalog["catalog_schema_version"] != 1 or catalog["catalog_revision"] != digest(unsigned) or catalog["language_tag"] != "en-US" or catalog["status"] != "fixture_only_acoustically_unvalidated":
        fail("CATALOG_IDENTITY_INVALID", "catalog")
    invocation = catalog["invocation"]
    exact(invocation, {"form_id", "text", "phones", "provenance"}, "invocation")
    forms, ids, form_ids, variant_ids = [], set(), set(), set()
    for intent in catalog["intents"]:
        exact(intent, {"intent_id", "owner", "effect_class", "status", "touch_only", "spoken_forms"}, "intent")
        if not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+", intent["intent_id"]) or intent["intent_id"] in ids or intent["effect_class"] not in EFFECTS:
            fail("INTENT_INVALID", "catalog")
        ids.add(intent["intent_id"])
        if intent["status"] not in {"fixture_enabled", "reserved_disabled"} or type(intent["touch_only"]) is not bool or intent["owner"] not in {"study", "agenda"}:
            fail("INTENT_INVALID", "catalog")
        if intent["touch_only"] and intent["spoken_forms"]:
            fail("TOUCH_ONLY_PROJECTED", "catalog")
        for form in intent["spoken_forms"]:
            exact(form, {"spoken_form_id", "phrase", "invocation_form_id", "contexts", "pronunciations"}, "spoken_form")
            if form["spoken_form_id"] in form_ids or form["invocation_form_id"] != invocation["form_id"] or not form["contexts"] or not set(form["contexts"]) <= CONTEXTS or not form["pronunciations"]:
                fail("SPOKEN_FORM_INVALID", "catalog")
            if normalize_text(form["phrase"])[:1] != normalize_text(invocation["text"]) or len(normalize_text(form["phrase"])) < 2:
                fail("INVOCATION_REQUIRED", "catalog")
            form_ids.add(form["spoken_form_id"])
            for pronunciation in form["pronunciations"]:
                exact(pronunciation, {"variant_id", "phones", "notation", "dialect", "source_version", "review_status"}, "pronunciation")
                if pronunciation["variant_id"] in variant_ids or pronunciation["notation"] != "ARPABET" or pronunciation["dialect"] != "en-US" or pronunciation["source_version"] != "project-semantic-fixture-v1" or pronunciation["review_status"] != "reviewed_semantic_fixture_not_acoustic" or not pronunciation["phones"] or any(not re.fullmatch(r"[A-Z]+[012]?", p) or re.sub(r"[012]$", "", p) not in ARPABET for p in pronunciation["phones"]):
                    fail("PRONUNCIATION_REVIEW_REQUIRED", "catalog")
                variant_ids.add(pronunciation["variant_id"])
            forms.append({**form, "intent_id": intent["intent_id"], "effect_class": intent["effect_class"], "status": intent["status"], "touch_only": intent["touch_only"]})
    reports = []
    reviews = {frozenset(review["form_ids"]): review for review in catalog["pair_reviews"]}
    for a, b in itertools.combinations(forms, 2):
        report = pair_report(a, b, invocation); reports.append(report)
        if a["intent_id"] != b["intent_id"] and report["context_overlap"] and (report["text_collision"] or report["phone_collision"]):
            fail("EXACT_COLLISION", "catalog")
        if report["requires_review"] and frozenset(report["form_ids"]) not in reviews:
            fail("PAIR_REVIEW_REQUIRED", "catalog")
    return reports

def phase_a(graph: dict, registered_intents: set[str]) -> dict:
    """Enumerate every endpoint/effect reachable from registered handler declarations.
    Unresolved compatible dispatch expands to every listed target; no unenumerable edge.
    """
    exact(graph, {"intents", "handlers", "endpoints"}, "phase_a")
    if not registered_intents <= set(graph["intents"]):
        fail("UNREGISTERED_EFFECT_ROOT", "phase_a")
    result = {}
    for intent, roots in sorted(graph["intents"].items()):
        found, seen, pending = set(), set(), list(roots)
        while pending:
            node = pending.pop()
            if node in seen: continue
            seen.add(node)
            if node in graph["endpoints"]:
                effects = graph["endpoints"][node]
                if not effects or not set(effects) <= EFFECTS: fail("ENDPOINT_EFFECT_INVALID", "phase_a")
                found.update((node, effect) for effect in effects)
            elif node in graph["handlers"]:
                edges = graph["handlers"][node]
                if not isinstance(edges, list) or not edges: fail("UNENUMERABLE_EDGE", "phase_a")
                for edge in edges:
                    if isinstance(edge, str): pending.append(edge)
                    elif isinstance(edge, dict) and set(edge) == {"compatible_targets"} and edge["compatible_targets"]:
                        pending.extend(edge["compatible_targets"])
                    else: fail("UNENUMERABLE_EDGE", "phase_a")
            else: fail("UNRESOLVED_EDGE", "phase_a")
        if not found: fail("ENDPOINT_MISSING", "phase_a")
        result[intent] = {"endpoint_effect_pairs": [list(pair) for pair in sorted(found)], "effects": sorted({effect for _, effect in found})}
    return result

def phase_b(active: dict, effects: dict, catalog: dict) -> None:
    intents = {i["intent_id"]: i for i in catalog["intents"]}
    for form in active["forms"]:
        intent = intents[form["intent_id"]]
        if intent["touch_only"] or form["intent_id"] not in effects: fail("TOUCH_ONLY_PROJECTED", "phase_b")
        for effect in effects[form["intent_id"]]["effects"]:
            allowed = effect in {"reversible_local", "bounded_learning_observation", "correctable_learning"}
            if not allowed or (effect == "correctable_learning" and "study_back" not in active["contexts"]):
                fail("EFFECT_FORBIDDEN", "phase_b")
            dominance = {"reversible_local": 0, "bounded_learning_observation": 1, "correctable_learning": 2}
            if dominance.get(effect, 99) > dominance.get(intent["effect_class"], -1):
                fail("EFFECT_CLASS_UNDERDECLARED", "phase_b")

def build_policy(template: dict, backend: dict) -> dict:
    exact(template, {"schema_version", "vad", "endpointing", "thresholds", "silence", "cooldown_ms", "debounce_ms", "postprocessing"}, "policy")
    shapes = {"vad": {"enabled", "minimum_speech_ms"}, "endpointing": {"maximum_utterance_ms", "complete_phrase_only"},
              "thresholds": {"minimum_confidence_milli", "minimum_margin_milli"}, "silence": {"timeout_ms"},
              "postprocessing": {"normalization_version", "allow_substring", "maximum_intents_per_utterance"}}
    for name, keys in shapes.items(): exact(template[name], keys, "policy." + name)
    if template["schema_version"] != 1 or template["vad"]["enabled"] is not False or template["endpointing"]["complete_phrase_only"] is not True or template["postprocessing"] != {"normalization_version": METRIC_VERSION, "allow_substring": False, "maximum_intents_per_utterance": 1}:
        fail("POLICY_INVALID", "build_policy")
    numeric = [template["cooldown_ms"], template["debounce_ms"], template["vad"]["minimum_speech_ms"], template["endpointing"]["maximum_utterance_ms"], template["silence"]["timeout_ms"], *template["thresholds"].values()]
    if any(type(value) is not int or value < 0 or value > 60000 for value in numeric) or any(value > 1000 for value in template["thresholds"].values()):
        fail("POLICY_BOUND_INVALID", "build_policy")
    value = {**template, "backend_artifact_digest": backend["backend_artifact_digest"], "recognizer_build_model_identity": RECOGNIZER_ID}
    return {**value, "recognition_policy_digest": digest(value)}

def verify_policy(policy: dict, backend: dict) -> None:
    exact(backend, {"projection_format", "recognizer_build_model_identity", "active_command_set_digest", "forms", "backend_artifact_digest"}, "backend")
    if backend["recognizer_build_model_identity"] != RECOGNIZER_ID or backend["projection_format"] != "deterministic_fake_complete_phrase_v1" or backend["backend_artifact_digest"] != digest({key: value for key, value in backend.items() if key != "backend_artifact_digest"}):
        fail("BACKEND_IDENTITY_MISMATCH", "verify_policy")
    bound = {"backend_artifact_digest", "recognizer_build_model_identity", "recognition_policy_digest"}
    template = {key: value for key, value in policy.items() if key not in bound}
    if policy != build_policy(template, backend): fail("POLICY_IDENTITY_MISMATCH", "verify_policy")

def build_bundle(root: Path, contexts: list[str]) -> dict:
    """Rebuild full content-addressed fake chain from the sole catalog and effect graph."""
    catalog = load_json(root / "docs/interfaces/spoken-command-catalog.json")
    validate_catalog(catalog)
    graph = load_json(root / "shared/spoken_command_catalog/intent_effect_graph.json")
    registry = (root / "panel_ui/src/panel_app_descriptors.cpp").read_text()
    registered = set(re.findall(r'\{"([a-z][a-z0-9_.]+)"\s*,\s*IntentEffectClass::', registry))
    effects = phase_a(graph, registered)
    if not set(contexts) <= CONTEXTS: fail("CONTEXT_INVALID", "build_bundle")
    forms = []
    for intent in catalog["intents"]:
        if intent["status"] != "fixture_enabled": continue
        for form in intent["spoken_forms"]:
            if set(form["contexts"]) & set(contexts):
                forms.append({"spoken_form_id": form["spoken_form_id"], "intent_id": intent["intent_id"], "phrase": form["phrase"], "pronunciations": form["pronunciations"]})
    active = {"schema_version": 1, "catalog_revision": catalog["catalog_revision"], "contexts": sorted(contexts), "forms": sorted(forms, key=lambda f: f["spoken_form_id"])}
    phase_b(active, effects, catalog)
    active["active_command_set_digest"] = digest(active)
    backend = {"projection_format": "deterministic_fake_complete_phrase_v1", "recognizer_build_model_identity": RECOGNIZER_ID,
               "active_command_set_digest": active["active_command_set_digest"], "forms": active["forms"]}
    backend["backend_artifact_digest"] = digest(backend)
    policy = build_policy(load_json(root / "config/voice/recognition-policies/fixture-policy-template.json"), backend)
    verify_policy(policy, backend)
    binding = {"catalog_revision": catalog["catalog_revision"], "active_command_set_digest": active["active_command_set_digest"],
               "backend_artifact_digest": backend["backend_artifact_digest"], "recognizer_build_model_identity": RECOGNIZER_ID,
               "recognition_policy_digest": policy["recognition_policy_digest"],
               "spoken_form_bindings": [{"spoken_form_id": f["spoken_form_id"], "intent_id": f["intent_id"]} for f in active["forms"]]}
    return {"binding": binding, "active": active, "backend": backend, "policy": policy, "phase_a": effects}

def advise(catalog: dict, candidate: dict) -> list[dict]:
    """Rank proposed reviewed variants by minimum edit distance, never safety probability."""
    reports = [pair_report(candidate, form, catalog["invocation"]) for intent in catalog["intents"] for form in intent["spoken_forms"]]
    return sorted(reports, key=lambda row: (not row["context_overlap"], row["minimum_distance"], row["form_ids"]))
