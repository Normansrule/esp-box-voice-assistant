#!/usr/bin/env python3
"""
test_brain_tools.py — The home brain's exact answers, and the checks around them.

SPDX-License-Identifier: Unlicense OR CC0-1.0

No models needed. The tool-calling loop is run against a scripted stand-in for
Ollama that behaves like llama3.2 really did when this was tested live: it
calls the calculator correctly and then writes a different number anyway.

    python tools/home_brain/test_brain_tools.py
"""

import datetime
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brain_tools as tools  # noqa: E402
import home_brain  # noqa: E402


class Calculator(unittest.TestCase):
    def test_arithmetic(self):
        self.assertEqual(tools.calculate("(3*12)-5"), 31)
        self.assertEqual(tools.calculate("2^10"), 1024)
        self.assertEqual(tools.calculate("1,250 + 750"), 2000)
        self.assertAlmostEqual(tools.calculate("sqrt(2)"), 1.4142135624)

    def test_percentages_as_people_say_them(self):
        self.assertEqual(tools.calculate("17% of 2350"), 399.5)
        self.assertEqual(tools.calculate("17 percent of 2350"), 399.5)
        self.assertEqual(tools.calculate("40 - 25%*40"), 30)

    def test_refuses_anything_that_is_not_arithmetic(self):
        for hostile in ("__import__('os').system('dir')", "open('x')", "William Shakespeare",
                        "().__class__", "9**99999", "1/0"):
            with self.assertRaises(Exception, msg=hostile):
                tools.calculate(hostile)
        # And through run_tool, the error is text for the model, not a crash.
        self.assertTrue(tools.run_tool("calculate", {"expression": "open('x')"}).startswith("error"))


class DatesAndTimes(unittest.TestCase):
    TODAY = datetime.date(2026, 10, 24)

    def test_date_info(self):
        self.assertEqual(tools.date_info("2026-12-25", self.TODAY),
                         "2026-12-25 is a Friday, in 62 days")
        self.assertEqual(tools.date_info("2026-10-23", self.TODAY),
                         "2026-10-23 is a Friday, 1 day ago")
        self.assertIn("today", tools.date_info("2026-10-24", self.TODAY))

    def test_add_time(self):
        self.assertEqual(tools.add_time("2:40 pm", 95), "2:40 pm plus 95 minutes is 4:15 PM")
        self.assertIn("12:15 AM the next day", tools.add_time("23:30", 45))
        self.assertIn("11:30 AM", tools.add_time("12:00 pm", -30))
        with self.assertRaises(ValueError):
            tools.add_time("lunchtime", 30)


class Units(unittest.TestCase):
    def test_conversions(self):
        self.assertEqual(tools.convert_units(750, "ml", "cup"), "750 ml is 3.17 cups")
        self.assertEqual(tools.convert_units(68, "F", "C"), "68°F is 20°C")
        self.assertEqual(tools.convert_units(1, "mile", "km"), "1 mile is 1.609 km")
        self.assertEqual(tools.convert_units(10, "pounds", "kg"), "10 pounds is 4.536 kg")

    def test_nonsense_is_refused(self):
        with self.assertRaises(ValueError):
            tools.convert_units(1, "kg", "km")
        with self.assertRaises(ValueError):
            tools.convert_units(1, "C", "m")


class AnswersForTheBox(unittest.TestCase):
    def test_tidy_removes_reasoning_and_markdown(self):
        self.assertEqual(tools.tidy_for_speech("<think>long chain</think>[Happy] **Yes!**"),
                         "[happy] Yes!")
        self.assertEqual(tools.tidy_for_speech("just words"), "[neutral] just words")
        self.assertTrue(tools.tidy_for_speech("[sad] " + "word " * 100).endswith("..."))

    def test_agreement_check(self):
        self.assertFalse(tools.answer_agrees_with_tool("You have 27 eggs left", "31"))
        self.assertTrue(tools.answer_agrees_with_tool("That's 31 eggs left!", "31"))
        self.assertFalse(tools.answer_agrees_with_tool("about 2.53 cups", "750 ml is 3.17 cups"))
        self.assertTrue(tools.answer_agrees_with_tool("It is a Sunday!",
                                                      "2027-03-14 is a Sunday, in 141 days"))
        self.assertTrue(tools.answer_agrees_with_tool("It arrives at 4:15 PM.",
                                                      "2:40 pm plus 95 minutes is 4:15 PM"))


class ScriptedOllama(home_brain.OllamaMind):
    """Plays back replies in order instead of asking a real model."""

    def __init__(self, replies):
        super().__init__("http://unused", "fake", 120)
        self.replies = list(replies)
        self.requests = []

    def _chat(self, body):
        self.requests.append(body)
        return self.replies.pop(0)


def tool_call(name, **arguments):
    return {"message": {"role": "assistant", "content": "",
                        "tool_calls": [{"function": {"name": name, "arguments": arguments}}]}}


def says(text):
    return {"message": {"role": "assistant", "content": text}, "eval_count": 5}


QUESTION = [{"role": "system", "content": "be a robot"},
            {"role": "user", "content": "3 boxes of 12 eggs, 5 break. How many left?"}]


class ToolLoop(unittest.TestCase):
    def test_the_tool_wins_when_the_model_says_a_different_number(self):
        mind = ScriptedOllama([tool_call("calculate", expression="3*12-5"),
                               says("[happy] You have 27 eggs left!")])
        text, _ = mind.reply(QUESTION, 160, 0.7)
        self.assertEqual(text, "[happy] That comes to 31.")

    def test_a_correct_answer_is_kept_in_the_model_words(self):
        mind = ScriptedOllama([tool_call("calculate", expression="3*12-5"),
                               says("[happy] That leaves 31 eggs.")])
        text, _ = mind.reply(QUESTION, 160, 0.7)
        self.assertEqual(text, "[happy] That leaves 31 eggs.")

    def test_tools_and_instructions_are_offered(self):
        mind = ScriptedOllama([says("[neutral] Hello.")])
        mind.reply(QUESTION, 160, 0.7)
        request = mind.requests[0]
        self.assertEqual([t["function"]["name"] for t in request["tools"]],
                         ["calculate", "date_info", "add_time", "convert_units"])
        # The box's personality first, then the tool instructions.
        self.assertEqual(request["messages"][0]["content"], "be a robot")
        self.assertIn("never guess", request["messages"][1]["content"])

    def test_a_failed_tool_does_not_override_the_answer(self):
        mind = ScriptedOllama([tool_call("calculate", expression="William Shakespeare"),
                               says("[neutral] William Shakespeare wrote it.")])
        text, _ = mind.reply(QUESTION, 160, 0.7)
        self.assertEqual(text, "[neutral] William Shakespeare wrote it.")

    def test_endless_tool_calls_are_cut_off(self):
        mind = ScriptedOllama([tool_call("calculate", expression="1+1")] * 10)
        text, _ = mind.reply(QUESTION, 160, 0.7)
        self.assertLessEqual(len(mind.requests), home_brain.OllamaMind.MAX_TOOL_ROUNDS + 1)
        self.assertTrue(text.startswith("["))

    def test_thinking_asks_for_room_to_think(self):
        mind = ScriptedOllama([says("<think>step by step</think>[curious] Four.")])
        mind.think = True
        text, _ = mind.reply(QUESTION, 160, 0.7)
        self.assertTrue(mind.requests[0]["think"])
        self.assertGreater(mind.requests[0]["options"]["num_predict"], 160)
        self.assertEqual(text, "[curious] Four.")


if __name__ == "__main__":
    unittest.main(verbosity=1)
