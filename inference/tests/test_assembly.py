import unittest

from semantifx_inference.assembly import (
    CATALOGUE_DESCRIPTIONS,
    NO_CHANGE_THRESHOLD,
    order_effects,
    order_questions,
    select_effects,
    selection_questions,
    to_chain,
)


class AssemblyTests(unittest.TestCase):
    def test_selection_questions_match_catalogue_plus_no_change(self):
        questions = selection_questions(CATALOGUE_DESCRIPTIONS)
        self.assertEqual(len(questions), len(CATALOGUE_DESCRIPTIONS) + 1)
        self.assertEqual(set(questions), {"use_eq", "use_distortion", "use_reverb", "no_change"})

    def test_no_change_wins(self):
        answers = {
            "use_eq": {"noul": 0.9},
            "use_distortion": {"noul": 0.9},
            "no_change": {"noul": NO_CHANGE_THRESHOLD},
        }
        self.assertEqual(select_effects(answers, CATALOGUE_DESCRIPTIONS), [])

    def test_order_questions_for_three_effects(self):
        selected = ["eq", "distortion", "compressor"]
        questions = order_questions(selected)
        self.assertEqual(set(questions), {
            "before_eq_distortion",
            "before_eq_compressor",
            "before_distortion_compressor",
        })

    def test_order_effects_consistent_preferences(self):
        selected = ["eq", "distortion", "compressor"]
        answers = {
            "before_eq_distortion": {"noul": 0.9},
            "before_eq_compressor": {"noul": 0.9},
            "before_distortion_compressor": {"noul": 0.9},
        }
        self.assertEqual(order_effects(selected, answers), selected)

    def test_order_effects_cycle_is_deterministic(self):
        selected = ["eq", "distortion", "compressor"]
        answers = {
            "before_eq_distortion": {"noul": 1.0},
            "before_eq_compressor": {"noul": 0.0},
            "before_distortion_compressor": {"noul": 1.0},
        }
        self.assertEqual(order_effects(selected, answers), ["eq", "distortion", "compressor"])

    def test_to_chain_defaults_and_repeat_numbering(self):
        chain = to_chain(["distortion", "eq", "distortion"])
        self.assertEqual([item["instance_id"] for item in chain], ["distortion-1", "eq-1", "distortion-2"])
        self.assertEqual(chain[0]["parameters"]["drive_db"], 0.0)
        self.assertEqual(chain[1]["parameters"]["low_shelf_gain_db"], 0.0)


if __name__ == "__main__":
    unittest.main()
