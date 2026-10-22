/*
 * voice_number_words.c — Numbers, written the way they are said.
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * EVERY STRING IN THIS FILE IS RECORDED AT BUILD TIME.
 *
 * scripts/make_voice_clips.py takes every literal here and renders it to an mp3
 * named after its hash, exactly as it does for the fixed replies. Change the
 * spelling of one and its recording changes name with it, so the two halves
 * cannot drift apart — a reworded entry goes silent rather than saying the
 * wrong number.
 */

#include "voice_number_words.h"

/*
 * Written out rather than generated at runtime, because the words are the
 * lookup key for the recordings. "twenty one" and "twenty-one" are different
 * files, so the spelling has to be pinned down somewhere, and a plain table is
 * the place where that is obvious.
 */
static const char *const s_words[] = {
    "zero",                      /* 0 */
    "one",                       /* 1 */
    "two",                       /* 2 */
    "three",                     /* 3 */
    "four",                      /* 4 */
    "five",                      /* 5 */
    "six",                       /* 6 */
    "seven",                     /* 7 */
    "eight",                     /* 8 */
    "nine",                      /* 9 */
    "ten",                       /* 10 */
    "eleven",                    /* 11 */
    "twelve",                    /* 12 */
    "thirteen",                  /* 13 */
    "fourteen",                  /* 14 */
    "fifteen",                   /* 15 */
    "sixteen",                   /* 16 */
    "seventeen",                 /* 17 */
    "eighteen",                  /* 18 */
    "nineteen",                  /* 19 */
    "twenty",                    /* 20 */
    "twenty one",                /* 21 */
    "twenty two",                /* 22 */
    "twenty three",              /* 23 */
    "twenty four",               /* 24 */
    "twenty five",               /* 25 */
    "twenty six",                /* 26 */
    "twenty seven",              /* 27 */
    "twenty eight",              /* 28 */
    "twenty nine",               /* 29 */
    "thirty",                    /* 30 */
    "thirty one",                /* 31 */
    "thirty two",                /* 32 */
    "thirty three",              /* 33 */
    "thirty four",               /* 34 */
    "thirty five",               /* 35 */
    "thirty six",                /* 36 */
    "thirty seven",              /* 37 */
    "thirty eight",              /* 38 */
    "thirty nine",               /* 39 */
    "forty",                     /* 40 */
    "forty one",                 /* 41 */
    "forty two",                 /* 42 */
    "forty three",               /* 43 */
    "forty four",                /* 44 */
    "forty five",                /* 45 */
    "forty six",                 /* 46 */
    "forty seven",               /* 47 */
    "forty eight",               /* 48 */
    "forty nine",                /* 49 */
    "fifty",                     /* 50 */
    "fifty one",                 /* 51 */
    "fifty two",                 /* 52 */
    "fifty three",               /* 53 */
    "fifty four",                /* 54 */
    "fifty five",                /* 55 */
    "fifty six",                 /* 56 */
    "fifty seven",               /* 57 */
    "fifty eight",               /* 58 */
    "fifty nine",                /* 59 */
    "sixty",                     /* 60 */
    "sixty one",                 /* 61 */
    "sixty two",                 /* 62 */
    "sixty three",               /* 63 */
    "sixty four",                /* 64 */
    "sixty five",                /* 65 */
    "sixty six",                 /* 66 */
    "sixty seven",               /* 67 */
    "sixty eight",               /* 68 */
    "sixty nine",                /* 69 */
    "seventy",                   /* 70 */
    "seventy one",               /* 71 */
    "seventy two",               /* 72 */
    "seventy three",             /* 73 */
    "seventy four",              /* 74 */
    "seventy five",              /* 75 */
    "seventy six",               /* 76 */
    "seventy seven",             /* 77 */
    "seventy eight",             /* 78 */
    "seventy nine",              /* 79 */
    "eighty",                    /* 80 */
    "eighty one",                /* 81 */
    "eighty two",                /* 82 */
    "eighty three",              /* 83 */
    "eighty four",               /* 84 */
    "eighty five",               /* 85 */
    "eighty six",                /* 86 */
    "eighty seven",              /* 87 */
    "eighty eight",              /* 88 */
    "eighty nine",               /* 89 */
    "ninety",                    /* 90 */
    "ninety one",                /* 91 */
    "ninety two",                /* 92 */
    "ninety three",              /* 93 */
    "ninety four",               /* 94 */
    "ninety five",               /* 95 */
    "ninety six",                /* 96 */
    "ninety seven",              /* 97 */
    "ninety eight",              /* 98 */
    "ninety nine",               /* 99 */
    "one hundred",               /* 100 */
    "one hundred and one",       /* 101 */
    "one hundred and two",       /* 102 */
    "one hundred and three",     /* 103 */
    "one hundred and four",      /* 104 */
    "one hundred and five",      /* 105 */
    "one hundred and six",       /* 106 */
    "one hundred and seven",     /* 107 */
    "one hundred and eight",     /* 108 */
    "one hundred and nine",      /* 109 */
    "one hundred and ten",       /* 110 */
    "one hundred and eleven",    /* 111 */
    "one hundred and twelve",    /* 112 */
    "one hundred and thirteen",  /* 113 */
    "one hundred and fourteen",  /* 114 */
    "one hundred and fifteen",   /* 115 */
    "one hundred and sixteen",   /* 116 */
    "one hundred and seventeen", /* 117 */
    "one hundred and eighteen",  /* 118 */
    "one hundred and nineteen",  /* 119 */
    "one hundred and twenty",    /* 120 */
};

#define WORD_COUNT  (sizeof(s_words) / sizeof(s_words[0]))

const char *voice_number_words(int value)
{
    if (value < 0 || value >= (int)WORD_COUNT) {
        return NULL;
    }
    return s_words[value];
}

size_t voice_number_words_count(void)
{
    return WORD_COUNT;
}
