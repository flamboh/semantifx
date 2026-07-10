import unittest

from semantifx_inference.errors import SemantiFXError
from semantifx_inference.protocol import validate_request


def request():
    return {
        "protocol_version": "1",
        "type": "analyze",
        "request_id": "test-1",
        "prompt": "warm",
        "audio": {"path": "/tmp/input.wav"},
        "effects": ["eq"],
        "options": {"backend": "mock", "iterations": 10},
    }


class ProtocolTests(unittest.TestCase):
    def test_accepts_minimal_valid_request(self):
        value = request()
        self.assertIs(validate_request(value), value)

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
        self.assertEqual(raised.exception.details["supported_versions"], ["1"])


if __name__ == "__main__":
    unittest.main()

