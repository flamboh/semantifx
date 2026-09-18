import unittest

from semantifx_inference.catalogue import default_chain
from semantifx_inference.errors import SemantiFXError
from semantifx_inference.protocol import validate_request


def request(chain=None):
    return {
        "type": "analyze",
        "request_id": "test-1",
        "prompt": "warm and lightly saturated",
        "audio": {"path": "/tmp/input.wav"},
        "chain": default_chain() if chain is None else chain,
        "options": {"backend": "clap", "iterations": 10, "parameter_initialization": "current"},
    }


class ProtocolTests(unittest.TestCase):
    def test_accepts_minimal_valid_request(self):
        value = request()
        self.assertIs(validate_request(value), value)

    def test_accepts_empty_chain(self):
        value = request([])
        self.assertIs(validate_request(value), value)

    def test_accepts_eight_instances(self):
        value = request(default_chain(["eq", "distortion", "eq", "distortion", "eq", "distortion", "eq", "distortion"]))
        self.assertIs(validate_request(value), value)

    def test_rejects_nine_instances(self):
        value = request(default_chain(["eq"] * 9))
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "chain")

    def test_rejects_duplicate_effect_instance(self):
        value = request()
        value["chain"][1]["instance_id"] = value["chain"][0]["instance_id"]
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "chain")

    def test_rejects_unknown_effect_type(self):
        value = request()
        value["chain"][0]["effect_type"] = "verb"
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "chain[0].effect_type")

    def test_accepts_opaque_instance_id(self):
        value = request([{
            "instance_id": "verb-9",
            "effect_type": "distortion",
            "parameters": default_chain(["distortion"])[0]["parameters"],
        }])
        self.assertIs(validate_request(value), value)

    def test_rejects_missing_chain_parameter(self):
        value = request()
        del value["chain"][1]["parameters"]["mix"]
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "chain[1].parameters")

    def test_rejects_out_of_range_parameter(self):
        value = request()
        value["chain"][0]["parameters"]["band1_gain_db"] = 21
        with self.assertRaises(SemantiFXError) as raised:
            validate_request(value)
        self.assertEqual(raised.exception.details["field"], "chain[0].parameters.band1_gain_db")

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
