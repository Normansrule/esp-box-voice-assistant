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
    return "%s is a %s, %s" % (target.isoformat(), target.strftime("%A"), when)


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
    _schema("calculate", "Work out arithmetic exactly. Use for ANY sum, percentage, "
            "or maths; never do arithmetic in your head.",
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
    # The answer is the LAST number in a result ("750 ml is 3.17 cup" -> 3.17),
    # except for dates, where the day count is what matters.
    key = re.findall(r"-?\d[\d,]*\.?\d*", tool_result.replace(",", ""))
    key = [k.rstrip(".") for k in key]
    if " day" in tool_result and "is a " in tool_result:
        weekday = tool_result.split("is a ", 1)[1].split(",", 1)[0]
        return weekday.lower() in (answer or "").lower() or key[-1] in _numbers(answer)
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
    if "</think>" in text.lower():                 # An unclosed opening tag.
        text = text[text.lower().rindex("</think>") + len("</think>"):]

    mood = "neutral"
    found = _MOOD.match(text)
    if found and found.group(1).lower() in MOODS:
        mood = found.group(1).lower()
        text = text[found.end():]

    text = re.sub(r"[*_`#]+", "", text)                    # bold, italics, code, headings
    text = re.sub(r"^\s*([-•]|\d+[.)])\s+", "", text, flags=re.M)   # list markers
    text = re.sub(r"\s+", " ", text).strip()

    if len(text) > max_chars:
        cut = text[:max_chars]
        stop = max(cut.rfind(". "), cut.rfind("! "), cut.rfind("? "))
        text = cut[:stop + 1] if stop > 40 else cut.rsplit(" ", 1)[0] + "..."

    return "[%s] %s" % (mood, text or "I am not sure about that one.")
