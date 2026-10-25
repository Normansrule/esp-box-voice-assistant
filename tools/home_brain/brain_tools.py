"""
brain_tools.py — Things the home brain works out exactly instead of guessing.

SPDX-License-Identifier: Unlicense OR CC0-1.0

A 3-billion-parameter model is good at understanding a question and bad at
arithmetic, dates and unit conversions: it produces a confident number that is
often wrong. So the model is given tools for exactly those, and asked to use
them rather than guess. Each tool is plain Python, so its answers are right by
construction, and the tests check them.

Also here: tidying what the model says into something a palm-sized robot can
read aloud (no markdown, no hidden reasoning, a mood tag, short).
"""

import ast
import datetime
import math
import operator
import re

# --------------------------------------------------------------------------- #
# Calculator                                                                   #
# --------------------------------------------------------------------------- #

_BINARY = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
           ast.Div: operator.truediv, ast.FloorDiv: operator.floordiv,
           ast.Mod: operator.mod, ast.Pow: operator.pow}
_UNARY = {ast.UAdd: operator.pos, ast.USub: operator.neg}
_FUNCTIONS = {"sqrt": math.sqrt, "sin": math.sin, "cos": math.cos, "tan": math.tan,
              "log": math.log, "log10": math.log10, "exp": math.exp, "abs": abs,
              "round": round, "floor": math.floor, "ceil": math.ceil,
              "radians": math.radians, "degrees": math.degrees}
_CONSTANTS = {"pi": math.pi, "e": math.e}


def calculate(expression):
    """
    Evaluate arithmetic safely: numbers, + - * / // % **, brackets, and a few
    maths functions. Never eval(): this is text from a model, which is text
    from whoever spoke to the box.
    """
    text = str(expression).lower().replace("^", "**").replace("×", "*").replace("÷", "/")
    text = text.replace(",", "").replace(" x ", " * ")
    # "17% of 2350", "17 percent of 2350", and a bare "15%".
    text = re.sub(r"(\d+(?:\.\d+)?)\s*(?:%|percent)\s*of\s*", r"(\g<1>/100)*", text)
    text = re.sub(r"(\d+(?:\.\d+)?)\s*(?:%|percent)", r"(\g<1>/100)", text)
    tree = ast.parse(text, mode="eval")

    def walk(node):
        if isinstance(node, ast.Expression):
            return walk(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            return node.value
        if isinstance(node, ast.BinOp) and type(node.op) in _BINARY:
            left, right = walk(node.left), walk(node.right)
            if isinstance(node.op, ast.Pow) and abs(right) > 1000:
                raise ValueError("exponent too large")
            return _BINARY[type(node.op)](left, right)
        if isinstance(node, ast.UnaryOp) and type(node.op) in _UNARY:
            return _UNARY[type(node.op)](walk(node.operand))
        if isinstance(node, ast.Name) and node.id in _CONSTANTS:
            return _CONSTANTS[node.id]
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                and node.func.id in _FUNCTIONS and not node.keywords):
            return _FUNCTIONS[node.func.id](*[walk(a) for a in node.args])
        raise ValueError("not plain arithmetic")

    value = walk(tree)
    if isinstance(value, float):
        if math.isnan(value) or math.isinf(value):
            raise ValueError("no finite answer")
        if value.is_integer() and abs(value) < 1e15:
            return int(value)
        return round(value, 10)
    return value


# --------------------------------------------------------------------------- #
# Dates                                                                        #
# --------------------------------------------------------------------------- #

def now_description(now=None):
    """Today's date and time, worded for the model."""
    now = now or datetime.datetime.now().astimezone()
    return now.strftime("%A %d %B %Y, %H:%M (%Z)")


def date_info(date, today=None):
    """Weekday of an ISO date, and how many days away it is."""
    today = today or datetime.date.today()
    target = datetime.date.fromisoformat(str(date).strip())
    delta = (target - today).days
    if delta == 0:
        when = "today"
    elif delta > 0:
        when = "in %d day%s" % (delta, "" if delta == 1 else "s")
    else:
        when = "%d day%s ago" % (-delta, "" if delta == -1 else "s")
    # Worded to be read aloud: "Friday 25 December 2026, in 62 days".
    return "%s %d %s, %s" % (target.strftime("%A"), target.day, target.strftime("%B %Y"), when)


def add_time(start, minutes):
    """A clock time plus (or minus) some minutes: "2:40 pm" + 95 -> 4:15 PM."""
    text = str(start).strip().lower().replace(".", "")
    found = re.match(r"^(\d{1,2})(?::(\d{2}))?\s*(am|pm)?$", text)
    if not found:
        raise ValueError("start must look like 14:40 or 2:40 pm")
    hour, minute, half = int(found.group(1)), int(found.group(2) or 0), found.group(3)
    if half == "pm" and hour != 12:
        hour += 12
    if half == "am" and hour == 12:
        hour = 0
    if hour > 23 or minute > 59:
        raise ValueError("not a clock time")
    total = hour * 60 + minute + int(round(float(minutes)))
    days, total = divmod(total, 24 * 60)
    result = datetime.time(total // 60, total % 60).strftime("%I:%M %p").lstrip("0")
    later = " the next day" if days == 1 else (" %d days later" % days if days > 1 else "")
    if days < 0:
        later = " the day before"
    if float(minutes) < 0:
        return "%g minutes before %s is %s%s" % (-float(minutes), start, result, later)
    return "%s plus %g minutes is %s%s" % (start, float(minutes), result, later)


# --------------------------------------------------------------------------- #
# Units                                                                        #
# --------------------------------------------------------------------------- #

# Each unit as (dimension, factor to the base unit of that dimension).
_UNITS = {
    # length, base metre
    "mm": ("length", 0.001), "cm": ("length", 0.01), "m": ("length", 1.0),
    "km": ("length", 1000.0), "in": ("length", 0.0254), "ft": ("length", 0.3048),
    "yd": ("length", 0.9144), "mi": ("length", 1609.344),
    # mass, base kilogram
    "g": ("mass", 0.001), "kg": ("mass", 1.0), "oz": ("mass", 0.028349523125),
    "lb": ("mass", 0.45359237), "t": ("mass", 1000.0),
    # volume, base litre
    "ml": ("volume", 0.001), "l": ("volume", 1.0), "tsp": ("volume", 0.00492892159375),
    "tbsp": ("volume", 0.01478676478125), "cup": ("volume", 0.2365882365),
    "floz": ("volume", 0.0295735295625), "gal": ("volume", 3.785411784),
    # speed, base metre per second
    "m/s": ("speed", 1.0), "km/h": ("speed", 1 / 3.6), "mph": ("speed", 0.44704),
    "knot": ("speed", 0.514444444),
    # time, base second
    "s": ("time", 1.0), "min": ("time", 60.0), "h": ("time", 3600.0),
    "day": ("time", 86400.0), "week": ("time", 604800.0),
}

_ALIASES = {
    "millimetre": "mm", "millimeter": "mm", "centimetre": "cm", "centimeter": "cm",
    "metre": "m", "meter": "m", "kilometre": "km", "kilometer": "km", "inch": "in",
    "foot": "ft", "feet": "ft", "yard": "yd", "mile": "mi", "gram": "g",
    "kilogram": "kg", "kilo": "kg", "ounce": "oz", "pound": "lb", "lbs": "lb",
    "tonne": "t", "millilitre": "ml", "milliliter": "ml", "litre": "l", "liter": "l",
    "teaspoon": "tsp", "tablespoon": "tbsp", "fluid ounce": "floz", "fl oz": "floz",
    "gallon": "gal", "kph": "km/h", "kmh": "km/h", "miles per hour": "mph",
    "kilometres per hour": "km/h", "kilometers per hour": "km/h", "knots": "knot",
    "second": "s", "sec": "s", "minute": "min", "hour": "h", "hr": "h", "days": "day",
    "weeks": "week", "celsius": "c", "°c": "c", "fahrenheit": "f", "°f": "f",
    "kelvin": "k",
}


def _unit(name):
    key = str(name).strip().lower()
    key = _ALIASES.get(key, key)
    if key.endswith("s") and key[:-1] in _ALIASES:          # "inches", "pounds"
        key = _ALIASES[key[:-1]]
    elif key.endswith("es") and key[:-2] in _ALIASES:
        key = _ALIASES[key[:-2]]
    elif key.endswith("s") and key[:-1] in _UNITS:
        key = key[:-1]
    return key


def convert_units(value, from_unit, to_unit):
    """Convert between common units. Temperatures are handled separately."""
    value = float(value)
    source, target = _unit(from_unit), _unit(to_unit)

    temperatures = {"c", "f", "k"}
    if source in temperatures or target in temperatures:
        if not (source in temperatures and target in temperatures):
            raise ValueError("cannot convert a temperature to %s" % to_unit)
        celsius = {"c": value, "f": (value - 32) * 5 / 9, "k": value - 273.15}[source]
        result = {"c": celsius, "f": celsius * 9 / 5 + 32, "k": celsius + 273.15}[target]
        names = {"c": "°C", "f": "°F", "k": " K"}
        return "%g%s is %s%s" % (value, names[source], "%.4g" % result, names[target])
    else:
        if source not in _UNITS or target not in _UNITS:
            raise ValueError("unknown unit: %s" % (from_unit if source not in _UNITS else to_unit))
        (dim_a, fa), (dim_b, fb) = _UNITS[source], _UNITS[target]
        if dim_a != dim_b:
            raise ValueError("cannot convert %s to %s" % (dim_a, dim_b))
        result = value * fa / fb

    shown = "%.4g" % result
    word = str(to_unit).strip()
    # "3.17 cups", not "3.17 cup"; symbols like km and mph stay as they are.
    if shown != "1" and target in _PLURAL_WORDS and not word.endswith("s"):
        word += "s"
    return "%g %s is %s %s" % (value, from_unit, shown, word)


# Units whose names take an s in the plural when the model passed the bare word.
_PLURAL_WORDS = {"cup", "day", "week", "knot"}


# --------------------------------------------------------------------------- #
# The tool table the model sees                                                #
# --------------------------------------------------------------------------- #

def _schema(name, description, properties, required):
    return {"type": "function",
            "function": {"name": name, "description": description,
                         "parameters": {"type": "object", "properties": properties,
                                        "required": required}}}


TOOL_SCHEMAS = [
    _schema("calculate", "Work out arithmetic exactly. Use only when the question needs "
            "a sum, percentage or maths; never do that arithmetic in your head. Do not "
            "use it for questions without numbers.",
            {"expression": {"type": "string",
                            "description": "e.g. '17 * 23', '15% of 80' as '0.15*80', 'sqrt(2)'"}},
            ["expression"]),
    _schema("date_info", "Weekday of a date and how many days from today it is.",
            {"date": {"type": "string", "description": "ISO date, YYYY-MM-DD"}},
            ["date"]),
    _schema("add_time", "Add minutes to a clock time, e.g. when a trip ends.",
            {"start": {"type": "string", "description": "e.g. '2:40 pm' or '14:40'"},
             "minutes": {"type": "number", "description": "minutes to add; negative to subtract"}},
            ["start", "minutes"]),
    _schema("convert_units", "Convert length, weight, volume, speed, time or temperature.",
            {"value": {"type": "number"},
             "from_unit": {"type": "string", "description": "e.g. km, lb, cup, mph, F"},
             "to_unit": {"type": "string", "description": "e.g. mi, kg, ml, km/h, C"}},
            ["value", "from_unit", "to_unit"]),
]

_TOOLS = {"calculate": lambda a: calculate(a["expression"]),
          "date_info": lambda a: date_info(a["date"]),
          "add_time": lambda a: add_time(a["start"], a["minutes"]),
          "convert_units": lambda a: convert_units(a["value"], a["from_unit"], a["to_unit"])}


def tool_call_is_meaningful(name, arguments):
    """
    Whether a tool result is worth overriding the model's answer with.

    A calculation with no operation in it ("12%", "27") only restates a number
    the model already had; in live testing that is exactly how a correct
    "about 12% battery" got replaced by "That comes to 0.12". Converting a unit
    to itself is the same kind of no-op.
    """
    if name == "calculate":
        expression = str((arguments or {}).get("expression", ""))
        stripped = re.sub(r"^\s*\d[\d,.]*\s*(%|percent)?\s*$", "", expression)
        return bool(stripped) and bool(re.search(r"[-+*/^×÷]|\bof\b|[a-z]\(", expression))
    if name == "convert_units":
        a = arguments or {}
        return _unit(a.get("from_unit", "")) != _unit(a.get("to_unit", ""))
    return True


def answer_ignores_tool(answer, tool_result):
    """An answer with no number or weekday in it, after a tool found one."""
    weekdays = ("monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday")
    return (bool(_numbers(tool_result)) and not _numbers(_MOOD.sub("", answer or "", count=1))
            and not any(day in (answer or "").lower() for day in weekdays))


def compose_prompt(question, results):
    """
    A fresh, short request to answer from exact results, used when the model's
    own answer ignored them. Deliberately not a continuation of the chat.
    """
    return ("Question: %s\nExact results already worked out: %s\nAnswer the question in "
            "one short, friendly sentence that uses the exact result. Start with a mood "
            "tag." % (question, "; ".join(results)))


def looks_like_a_leaked_tool_call(text):
    """True for an answer that is really a tool call written out as JSON."""
    stripped = _MOOD.sub("", (text or "").strip(), count=1).strip()
    # Loose on purpose: the leaked JSON is often malformed ("parameters{").
    return stripped.startswith("{") and "name" in stripped and (
        "parameters" in stripped or "arguments" in stripped)


_NUMBER_WORDS = ("one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
                 "ten", "eleven", "twelve", "fifteen", "twenty", "thirty", "forty", "fifty",
                 "hundred", "thousand", "million", "half", "quarter", "twice", "double",
                 "dozen", "percent")
_TOOL_CUES = ("how many", "how much", "how long", "how far", "what time", "when ",
              "what day", "which day", "days until", "days till", "until ", "convert",
              "in celsius", "in fahrenheit", "times", "divided", "plus", "minus",
              "square root", "average", "total", "per ", "split", "share")
_UNIT_WORDS = tuple(sorted(set(list(_UNITS) + list(_ALIASES)), key=len, reverse=True))


def latest_question(messages):
    """The newest user message, without any readings appended to it."""
    for message in reversed(messages):
        if message.get("role") == "user" and isinstance(message.get("content"), str):
            return message["content"].split("\n(My own sensors right now:", 1)[0]
    return ""


def question_needs_tools(question):
    """
    Whether to offer the calculator, date, time and unit tools at all.

    Generous: a false yes only costs the model the chance to call a tool it
    does not need. A false no costs an exact answer, so anything with a digit,
    a number word, a date or a unit in it gets the tools.
    """
    text = " %s " % (question or "").lower()
    if re.search(r"\d", text):
        return True
    if any(re.search(r"\b%s\b" % re.escape(word), text) for word in _NUMBER_WORDS):
        return True
    if any(cue in text for cue in _TOOL_CUES):
        return True
    months = ("january", "february", "march", "april", "may ", "june", "july", "august",
              "september", "october", "november", "december", "christmas", "birthday",
              "tomorrow", "yesterday", "weekend", "next week")
    if any(month in text for month in months):
        return True
    return any(re.search(r"\b%s\b" % re.escape(unit), text)
               for unit in _UNIT_WORDS if len(unit) > 2)


def tool_answer_sentence(name, result):
    """A tool's result as a sentence the box can say on its own."""
    if name == "calculate":
        return "That comes to %s." % result
    return result[0].upper() + result[1:] + "."


def _numbers(text):
    return {n.replace(",", "").rstrip(".") for n in re.findall(r"-?\d[\d,]*\.?\d*", text or "")}


def answer_agrees_with_tool(answer, tool_result):
    """
    True when the model's answer contains the number the tool worked out.

    Small models call the calculator and then write a different number anyway.
    The tool is right by construction, so when they disagree the tool wins.
    """
    wanted = _numbers(tool_result)
    if not wanted:
        return True

    # Only a CONFLICT counts. An answer with no number (or weekday) in it did
    # not use the tool at all: "It's quite warm in here" or "Canberra" are
    # answers in their own right, and live testing showed that replacing them
    # with the tool's line ("27.5°C is 81.5°F") made them worse, not better.
    weekdays = ("monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday")
    if not _numbers(answer) and not any(day in (answer or "").lower() for day in weekdays):
        return True
    # The answer is the LAST number in a result ("750 ml is 3.17 cup" -> 3.17),
    # except for dates, where the day count is what matters.
    key = re.findall(r"-?\d[\d,]*\.?\d*", tool_result.replace(",", ""))
    key = [k.rstrip(".") for k in key]
    weekday = tool_result.split(" ", 1)[0]
    if weekday in ("Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"):
        # A date only CONFLICTS over a weekday or a day count. "About 12
        # percent" is not a disagreement with "Saturday 24 October, today",
        # which is how a battery answer once became a date.
        said = (answer or "").lower()
        named = [d for d in weekdays if d in said]
        if named and weekday.lower() not in named:
            return False
        counts = re.findall(r"(\d[\d,]*)\s+days?", said)
        if counts and key[-1] not in [c.replace(",", "") for c in counts]:
            return False
        return True
    if ":" in tool_result and ("AM" in tool_result or "PM" in tool_result):
        clock = re.search(r"\d{1,2}:\d{2}", tool_result.split(" is ", 1)[-1]).group(0)
        return clock in (answer or "")
    return key[-1] in _numbers(answer)


def run_tool(name, arguments):
    """Run one tool call. Errors come back as text, for the model to read."""
    if name not in _TOOLS:
        return "error: no tool called %s" % name
    try:
        return str(_TOOLS[name](arguments or {}))
    except Exception as error:
        return "error: %s" % error


_BOX_LINE = re.compile(r"\s*\[box: [^\]\n]*\]\s*$")


def split_box_context(messages):
    """
    Take the "[box: ...]" readings line off every user message.

    The box adds one to each question in Home AI mode (voice_box_context.c),
    and keeps them in its history, so earlier turns carry old ones. Returns
    the cleaned messages and the NEWEST line, or None when there is none.
    """
    cleaned, newest = [], None
    for message in messages:
        content = message.get("content")
        if message.get("role") == "user" and isinstance(content, str):
            found = _BOX_LINE.search(content)
            if found:
                newest = found.group(0).strip()
                message = dict(message, content=content[:found.start()].rstrip())
        cleaned.append(message)
    return cleaned, newest


def restate_box_context(messages, box_line):
    """Put the readings, in plain words, after the newest user question."""
    readings = box_line[len("[box: "):-1]
    for index in range(len(messages) - 1, -1, -1):
        if messages[index].get("role") == "user":
            updated = list(messages)
            updated[index] = dict(messages[index], content="%s\n(My own sensors right now: %s.)"
                                  % (messages[index]["content"], readings))
            return updated
    return messages


def box_context_instruction(box_line):
    """How the model is told about the box's readings."""
    readings = box_line[len("[box: "):-1]
    return ("Live readings from your own sensors right now: %s. They are real "
            "measurements, except the battery percentage, which is an estimate. Use "
            "them when the question is about the room, the time, your battery, or "
            "whether to open a window or put on a jumper. Do not mention them "
            "otherwise, and never invent a reading that is not listed. Say a percentage "
            "as a percentage (\"about 12 percent\"), never as a fraction." % readings)


# Always given, tools or not. In live testing llama3.2 once answered "What will
# the weather be tomorrow?" with an invented forecast ("mostly sunny, 22°C").
# Worded with care: a blunter "you have no internet" made llama3.2 refuse
# "Why is the sky blue?" because it decided it knew nothing about the world.
HONESTY_RULES = (
    "Answer general knowledge, science and history questions confidently from what "
    "you know. The one thing you cannot do is look anything up: you have no live "
    "weather forecast, news, sports scores or prices, so if asked for one, say "
    "plainly that you cannot check it, and never make one up.")


def tools_instruction(now=None):
    """Added after the box's own system prompt."""
    return ("It is now %s. You have tools for arithmetic, dates and unit conversion: "
            "use them whenever a number is involved, and never guess one. "
            "If you do not know something, say so briefly instead of inventing it. "
            "Think the question through, then give only the final answer. "
            "No lists, headings or markdown: this is read aloud." % now_description(now))


# --------------------------------------------------------------------------- #
# Making an answer fit the box                                                 #
# --------------------------------------------------------------------------- #

MOODS = ("happy", "excited", "curious", "surprised", "sad", "annoyed", "sleepy",
         "love", "confused", "neutral")

_THINK = re.compile(r"<think>.*?</think>", re.S | re.I)
_MOOD = re.compile(r"^\s*\[(\w+)\]\s*")


def tidy_for_speech(text, max_chars=220):
    """
    Turn what a model wrote into something the box can show and say:
    hidden reasoning removed, markdown removed, one mood tag at the front,
    and cut at a sentence boundary when it runs long.
    """
    text = _THINK.sub("", text or "")
    # llama3.2 sometimes opens with the role name after a mid-chat instruction.
    text = re.sub(r"^\s*assistant\s*:?\s*", "", text, flags=re.I)
    if "</think>" in text.lower():                 # An unclosed opening tag.
        text = text[text.lower().rindex("</think>") + len("</think>"):]

    mood = "neutral"
    # Every bracketed word at the front comes off; the first real mood wins.
    # Models invent moods ("[low]", "[comfortable]") and stack them, and an
    # invented one would otherwise be read aloud.
    found = _MOOD.match(text)
    while found:
        if found.group(1).lower() in MOODS and mood == "neutral":
            mood = found.group(1).lower()
        text = text[found.end():]
        found = _MOOD.match(text)

    text = re.sub(r"[*_`#]+", "", text)                    # bold, italics, code, headings
    text = re.sub(r"^\s*([-•]|\d+[.)])\s+", "", text, flags=re.M)   # list markers
    text = re.sub(r"\s+", " ", text).strip()

    if len(text) > max_chars:
        cut = text[:max_chars]
        stop = max(cut.rfind(". "), cut.rfind("! "), cut.rfind("? "))
        text = cut[:stop + 1] if stop > 40 else cut.rsplit(" ", 1)[0] + "..."

    return "[%s] %s" % (mood, text or "I am not sure about that one.")
