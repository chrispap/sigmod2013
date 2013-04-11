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
    /* dword */
    unsigned            lastCheck_edit;
    unsigned            lastCheck_hamm;
    vector<unsigned>    editMatches[4];                 ///< Lists of words. One for each edit distance.
    vector<unsigned>    hammMatches[4];                 ///< Lists of words. One for each hamming distance.

    /* qword */
    unsigned            gwdbIndex;
    int                 qwindex[3];                     ///< Index of this word to the query word tables.

    /* general */
    int                 length;                         ///< strlen(txt);
    unsigned            letterBits;                     ///< 1 bit for every char [a-z]
    WordText            txt;                            ///< The actual word :P

    Word (WordText &wtxt, unsigned globindex) :
        lastCheck_edit(0),
        lastCheck_hamm(0),
        gwdbIndex(globindex),
        letterBits(0),
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

    int EditDist(Word *w) {
        int oo=0x7FFFFFFF;

        char* qs = w->txt.chars;
        int qn = w->length;
        char* ds = txt.chars;
        int dn = this->length;

        int T[2][MAX_WORD_LENGTH+1];

        int qi, di, cur=0;

        for(di=0;di<=dn;di++)
            T[cur][di]=di;

        cur=1;
        for(qi=1;qi<=qn;qi++)
        {
            T[cur][0]=qi;
            for(di=1;di<=dn;di++)
                T[cur][di]=oo;

            for(di=1;di<=dn;di++)
            {
                int ret=oo;

                int d1=T[1-cur][di]+1;
                int d2=T[cur][di-1]+1;
                int d3=T[1-cur][di-1]; if(qs[qi-1]!=ds[di-1]) d3++;

                if(d1<ret) ret=d1;
                if(d2<ret) ret=d2;
                if(d3<ret) ret=d3;

                if (di==(qi-qn+dn) && ret>3) return oo;

                T[cur][di]=ret;
            }

            cur=1-cur;
        }

        return T[1-cur][dn];
    }

    int HammingDist(Word *w) {
        char* a = w->txt.chars;
        int na = w->length;
        char* b = txt.chars;
        int nb = this->length;

        int j, oo=0x7FFFFFFF;
        if(na!=nb) return oo;

        unsigned int num_mismatches=0;
        for(j=0;j<na;j++) {
            if(a[j]!=b[j]) num_mismatches++;
            if (num_mismatches>3) break;
        }

        return num_mismatches;
    }

};

#endif
