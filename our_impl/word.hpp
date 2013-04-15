#ifndef WORD_H
#define WORD_H

typedef unsigned long wunit;

#define WUNITS_MAX ((MAX_WORD_LENGTH+1)/sizeof(wunit))

union WordText {
    wunit ints[WUNITS_MAX];
    char chars[MAX_WORD_LENGTH+1];
};

struct Word
{
    int                 length;
    unsigned            letterBits;

    unsigned            lastCheck_edit;
    unsigned            lastCheck_hamm;

    int                 qwindex[3];
    unsigned            wid;

    WordText            txt;

    vector<unsigned>    editMatches[4];
    vector<unsigned>    hammMatches[4];

    Word (WordText &wtxt, unsigned globindex) :
        letterBits(0),
        lastCheck_edit(0),
        lastCheck_hamm(0),
        wid(globindex),
        txt(wtxt)
    {
        unsigned wi;
        for (wi=0; txt.chars[wi]; wi++) letterBits |= 1 << (txt.chars[wi]-'a');
        length = wi;
    }

    bool equals(WordText &wtxt) const {
        for (unsigned i=0; i<WUNITS_MAX; i++) if (wtxt.ints[i]!=txt.ints[i]) return false;
        return true;
    }

    int letterDiff(Word *w ) {
        return __builtin_popcount(this->letterBits ^ w->letterBits);
    }

    int letterDiff(unsigned _letterBits) {
        return __builtin_popcount(letterBits ^ _letterBits);
    }

    static int letterDiff (unsigned lb1, unsigned lb2) {
        return __builtin_popcount(lb1^ lb2);
    }

};

#endif
