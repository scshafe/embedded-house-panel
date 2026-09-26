from __future__ import annotations
import copy
import json
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from shared.spoken_command_catalog.catalog import CatalogError, advise, build_bundle, build_policy, canonical, digest, load_json, metrics, normalize_text, phase_a, phase_b, validate_catalog, verify_policy
from fixtures import ROOT

class CatalogTest(unittest.TestCase):
    def setUp(self):
        self.catalog = load_json(ROOT / "docs/interfaces/spoken-command-catalog.json")
        self.graph = load_json(ROOT / "shared/spoken_command_catalog/intent_effect_graph.json")
        self.bundle = build_bundle(ROOT, ["study_back", "study_history"])

    def rehash(self):
        self.catalog["catalog_revision"] = digest({key: value for key, value in self.catalog.items() if key != "catalog_revision"})

    def test_projection_and_policy_chain_reproduce_exactly(self):
        self.assertEqual(canonical(self.bundle), canonical(build_bundle(ROOT, ["study_back", "study_history"])))
        verify_policy(self.bundle["policy"], self.bundle["backend"])
        self.assertEqual(len(self.bundle["active"]["forms"]), 7)
        for field in ("recognition_policy_digest", "backend_artifact_digest", "recognizer_build_model_identity"):
            tampered = copy.deepcopy(self.bundle["policy"]); tampered[field] += "bad"
            with self.assertRaises(CatalogError): verify_policy(tampered, self.bundle["backend"])
        backend = copy.deepcopy(self.bundle["backend"])
        backend["recognizer_build_model_identity"] = "unselected-real-engine"
        with self.assertRaises(CatalogError): verify_policy(self.bundle["policy"], backend)

    def test_every_decision_setting_changes_policy_identity(self):
        template = load_json(ROOT / "config/voice/recognition-policies/fixture-policy-template.json")
        base = build_policy(template, self.bundle["backend"])["recognition_policy_digest"]
        fields = [("vad", "minimum_speech_ms"), ("endpointing", "maximum_utterance_ms"), ("thresholds", "minimum_confidence_milli"),
                  ("thresholds", "minimum_margin_milli"), ("silence", "timeout_ms"), (None, "cooldown_ms"), (None, "debounce_ms")]
        for parent, field in fields:
            value = copy.deepcopy(template)
            if parent: value[parent][field] += 1
            else: value[field] += 1
            self.assertNotEqual(base, build_policy(value, self.bundle["backend"])["recognition_policy_digest"])
        template["unexpected_setting"] = 1
        with self.assertRaises(CatalogError): build_policy(template, self.bundle["backend"])

    def test_exact_collision_cannot_be_waived(self):
        first = self.catalog["intents"][0]["spoken_forms"][0]
        second = self.catalog["intents"][1]["spoken_forms"][0]
        second["phrase"] = first["phrase"]
        second["pronunciations"][0]["phones"] = first["pronunciations"][0]["phones"]
        self.rehash()
        with self.assertRaises(CatalogError) as error: validate_catalog(self.catalog)
        self.assertEqual(error.exception.code, "VOICE_COMMAND.EXACT_COLLISION")

    def test_unreviewed_pronunciation_is_not_accepted(self):
        self.catalog["intents"][0]["spoken_forms"][0]["pronunciations"][0]["review_status"] = "generated"
        self.rehash()
        with self.assertRaises(CatalogError): validate_catalog(self.catalog)

    def test_phase_a_includes_touch_only_and_expands_compatible_dispatch(self):
        effects = phase_a(self.graph, set(self.graph["intents"]))
        self.assertEqual(effects["agenda.submit_wish"]["effects"], ["external_submission"])
        self.assertIn("capture_start", effects["agenda.start_wish_recording"]["effects"])
        self.graph["handlers"]["study.flip.handler"] = [{"compatible_targets": ["study.navigate", "personal_context.dispatch"]}]
        expanded = phase_a(self.graph, set(self.graph["intents"]))
        with self.assertRaises(CatalogError): phase_b(self.bundle["active"], expanded, self.catalog)
        self.graph["handlers"]["study.flip.handler"] = [{"dynamic": "unenumerable"}]
        with self.assertRaises(CatalogError): phase_a(self.graph, set(self.graph["intents"]))

    def test_touch_only_projection_rejected_even_without_forbidden_effect(self):
        active = copy.deepcopy(self.bundle["active"])
        active["forms"].append({"intent_id": "study.rating.undo", "spoken_form_id": "invented"})
        with self.assertRaises(CatalogError): phase_b(active, self.bundle["phase_a"], self.catalog)

    def test_shell_navigation_is_registered_reversible_and_never_projected(self):
        projected = {form["intent_id"] for form in self.bundle["active"]["forms"]}
        for intent in ("shell.home", "shell.open_study", "shell.open_agenda", "shell.apps_previous", "shell.apps_next"):
            with self.subTest(intent=intent):
                self.assertEqual(self.bundle["phase_a"][intent]["effects"], ["reversible_local"])
                self.assertNotIn(intent, projected)

    def test_metric_golden_empty_normalization_and_denominators(self):
        goldens = load_json(ROOT / "media_broker/tests/metric-goldens.json")
        for item in goldens:
            self.assertEqual(metrics(item["left"], item["right"]), item["expected"])
        self.assertEqual(normalize_text("  LEXI,   flip! "), ["lexi", "flip"])
        candidate = self.catalog["intents"][0]["spoken_forms"][0]
        self.assertEqual(advise(self.catalog, candidate)[0]["minimum_distance"], 0)

if __name__ == "__main__": unittest.main()
