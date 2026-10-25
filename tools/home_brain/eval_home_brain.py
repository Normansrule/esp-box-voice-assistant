#!/usr/bin/env python3
"""
eval_home_brain.py — Ask the real home brain a bank of questions and score it.

SPDX-License-Identifier: Unlicense OR CC0-1.0

The unit tests prove the tools are right. This measures what actually matters:
does the box end up SAYING the right thing, with a real model in the loop? Each
question has a check on the final answer (what the box would show and speak),
so a change to the prompt, the tools or the model can be judged by a number
rather than by a few hand-picked examples.

Needs Ollama running with the model pulled. Nothing else.

    python tools/home_brain/eval_home_brain.py
    python tools/home_brain/eval_home_brain.py --model qwen3:4b --think

Dates are relative to "today" so the bank does not go stale.
"""

import argparse
import datetime
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import home_brain  # noqa: E402

TODAY = datetime.date.today()
CHRISTMAS = datetime.date(TODAY.year if TODAY <= datetime.date(TODAY.year, 12, 25)
                          else TODAY.year + 1, 12, 25)
DAYS_TO_CHRISTMAS = (CHRISTMAS - TODAY).days

# Readings like the box sends on a warm evening with a low battery.
WARM_BOX = "[box: room 27.5 C; humidity 63%; battery about 12% (estimate, low); someone nearby]"
COLD_BOX = "[box: room 14.0 C; humidity 35%; battery about 90% (estimate); nobody nearby]"


def any_of(*needles):
    return lambda answer: any(n.lower() in answer.lower() for n in needles)


def none_of(*needles):
    return lambda answer: not any(n.lower() in answer.lower() for n in needles)


def both(*checks):
    return lambda answer: all(check(answer) for check in checks)


def number(value):
    """The answer contains this number (commas ignored)."""
    wanted = str(value)
    return lambda answer: wanted in re.sub(r"(?<=\d),(?=\d)", "", answer)


NO_LEAKS = none_of('{"', "That comes to 0.", "[box:")

# (category, question, box readings or None, check)
BANK = [
    ("maths", "What is 17 percent of 2350?", None, number("399.5")),
    ("maths", "What is 144 divided by 12?", None, number(12)),
    ("maths", "What's 23 times 47?", None, number(1081)),
    ("maths", "What is the square root of 81?", None, number(9)),
    ("maths", "If a shirt costs 40 dollars and is 25 percent off, what do I pay?", None, number(30)),
    ("reasoning", "I have 3 boxes of 12 eggs and 5 break. How many are left?", None, number(31)),
    ("reasoning", "A pizza has 8 slices and 3 friends share it equally with me. "
                  "How many slices do we each get?", None, number(2)),
    ("reasoning", "I read 15 pages a day. How many days to finish a 300 page book?", None, number(20)),
    ("reasoning", "Tom is older than Ann, and Ann is older than Sam. Who is youngest?", None,
     any_of("Sam")),
    ("reasoning", "If it takes 5 minutes to boil one egg, how long for three eggs boiled "
                  "together in one pot?", None, both(number(5), none_of("15 minutes"))),
    ("dates", "How many days until Christmas?", None, number(DAYS_TO_CHRISTMAS)),
    ("dates", "What day of the week is %s?" % (TODAY + datetime.timedelta(days=10)).strftime("%d %B %Y"),
     None, any_of((TODAY + datetime.timedelta(days=10)).strftime("%A"))),
    ("dates", "What day of the week was 1 January 2000?", None, any_of("Saturday")),
    ("time", "A train leaves at 2:40 pm and the trip is 95 minutes. When does it arrive?",
     None, any_of("4:15")),
    ("time", "My flight is at 6:50 am and I must be there 2 hours early. When should I arrive?",
     None, any_of("4:50")),
    ("time", "I start cooking at 18:15 and it takes 50 minutes. When is dinner ready?",
     None, any_of("7:05", "19:05")),
    ("units", "How many cups is 750 millilitres?", None, number("3.17")),
    ("units", "What is 68 Fahrenheit in Celsius?", None, number(20)),
    ("units", "How many kilometres is 10 miles?", None, number("16.09")),
    ("units", "How many pounds is 5 kilograms?", None, number("11.02")),
    ("knowledge", "What is the capital of Australia?", None, any_of("Canberra")),
    ("knowledge", "Who wrote Romeo and Juliet?", None, any_of("Shakespeare")),
    ("knowledge", "What planet is known as the red planet?", None, any_of("Mars")),
    ("knowledge", "Is a dolphin a fish?", None, any_of("mammal", "not a fish")),
    ("knowledge", "Why is the sky blue?", None, any_of("scatter")),
    ("room", "Is it warm in here?", WARM_BOX, any_of("warm", "27", "hot", "80", "81")),
    ("room", "Is it cold in here?", COLD_BOX, any_of("cold", "chilly", "cool", "14")),
    ("room", "How is your battery?", WARM_BOX, both(any_of("12", "low"), none_of("0.12"))),
    ("room", "How humid is it?", WARM_BOX, number(63)),
    ("honesty", "What will the weather be tomorrow?", None,
     any_of("can't", "cannot", "don't", "do not", "not able", "unable", "no way", "not sure",
            "check", "forecast")),
]


def main(argv=None):
    parser = argparse.ArgumentParser(description="Score the home brain on a bank of questions.")
    parser.add_argument("--ollama", default=home_brain.DEFAULT_OLLAMA)
    parser.add_argument("--model", default=home_brain.DEFAULT_MODEL)
    parser.add_argument("--think", action="store_true")
    parser.add_argument("--no-tools", action="store_true")
    parser.add_argument("--only", help="run one category")
    args = parser.parse_args(argv)

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    mind = home_brain.OllamaMind(args.ollama, args.model, 120,
                                 use_tools=not args.no_tools, think=args.think)
    mind.check()
    mind.reply([{"role": "user", "content": "Say hi."}], 1, 0, raw=True)   # warm up

    results, slowest = {}, 0.0
    for category, question, box, check in BANK:
        if args.only and category != args.only:
            continue
        content = question + ("\n" + box if box else "")
        started = time.time()
        answer, _ = mind.reply([{"role": "system", "content": home_brain.BOX_SYSTEM_PROMPT},
                                {"role": "user", "content": content}], 160, 0.7)
        took = time.time() - started
        slowest = max(slowest, took)
        passed = check(answer) and NO_LEAKS(answer)
        results.setdefault(category, []).append(passed)
        print("%s %4.1fs  %-9s %s\n               -> %s" % ("PASS" if passed else "FAIL", took,
                                                         category, question, answer))

    total = sum(len(r) for r in results.values())
    passed = sum(sum(r) for r in results.values())
    print("\n%s%s%s" % (mind.description, " (thinking)" if args.think else "", ""))
    for category, marks in results.items():
        print("  %-10s %d/%d" % (category, sum(marks), len(marks)))
    print("  %-10s %d/%d  (slowest answer %.1f s)" % ("TOTAL", passed, total, slowest))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
