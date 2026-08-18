import unittest

from semantifx_inference.chain_schema import default_chain
from semantifx_inference.errors import SemantiFXError
from semantifx_inference.protocol import validate_request


def request():
    return {
        "protocol_version": "2",
        "type": "analyze",
        "request_id": "test-1",
        "prompt": "warm and lightly saturated",
        "audio": {"path": "/tmp/input.wav"},
        "chain": default_chain(),
        "options": {"backend": "clap", "iterations": 10, "parameter_initialization": "current"},
    }


class ProtocolTests(unittest.TestCase):
    def test_accepts_minimal_valid_request(self):
        value = request()
        self.assertIs(validate_request(value), value)

    def test_accepts_fixed_eq_distortion_chain(self):
        value = request()
        self.assertIs(validate_request(value), value)

    def test_accepts_swapped_chain_order(self):
        value = request()
        value["chain"].reverse()
        self.assertIs(validate_request(value), value)

    def test_rejects_duplicate_effect_instance(self):
        value = request()
        value["chain"][1] = value["chain"][0]
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "chain")

    def test_rejects_missing_chain_parameter(self):
        value = request()
        del value["chain"][1]["parameters"]["mix"]
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "chain[1].parameters")

    def test_rejects_unknown_fields(self):
        value = request()
        value["surprise"] = True
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.code, "invalid_request")
        self.assertIn("surprise", raised.exception.message)

    def test_rejects_boolean_where_integer_is_expected(self):
        value = request()
        value["options"]["iterations"] = True
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "options.iterations")

    def test_reports_unsupported_version_separately(self):
        value = request()
        value["protocol_version"] = "99"
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.code, "unsupported_protocol_version")
        self.assertEqual(raised.exception.details["supported_versions"], ["2"])

    def test_accepts_uint32_random_seed(self):
        value = request()
        value["options"]["random_seed"] = 4294967295
        self.assertIs(validate_request(value), value)

    def test_rejects_random_seed_outside_uint32(self):
        value = request()
        value["options"]["random_seed"] = 4294967296
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "options.random_seed")


if __name__ == "__main__":
    unittest.main()
