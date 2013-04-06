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
    unsigned lastCheck_edit;
    unsigned lastCheck_hamm;
    vector<unsigned>   editMatches[4];              ///< Lists of words. One for each edit distance.
    vector<unsigned>   hammMatches[4];              ///< Lists of words. One for each hamming distance.

    /* qword */
    unsigned        gwdbIndex;
    int             qwindex[3];                     ///< Index of this word to the query word tables.

    /* general */
    int             length;                         ///< strlen(txt);
    unsigned        letterBits;                     ///< 1 bit for every char [a-z]
    WordText        txt;                            ///< The actual word :P

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
        char* a = w->txt.chars;
        int na = w->length;
        char* b = txt.chars;
        int nb = this->length;

        int oo=0x7FFFFFFF;

        int T[2][MAX_WORD_LENGTH+1];

        int ia, ib;

        int cur=0;
        ia=0;

        for(ib=0;ib<=nb;ib++)
            T[cur][ib]=ib;

        cur=1-cur;

        for(ia=1;ia<=na;ia++)
        {
            for(ib=0;ib<=nb;ib++)
                T[cur][ib]=oo;

            int ib_st=0;
            int ib_en=nb;

            if(ib_st==0)
            {
                ib=0;
                T[cur][ib]=ia;
                ib_st++;
            }

            for(ib=ib_st;ib<=ib_en;ib++)
            {
                int ret=oo;

                int d1=T[1-cur][ib]+1;
                int d2=T[cur][ib-1]+1;
                int d3=T[1-cur][ib-1]; if(a[ia-1]!=b[ib-1]) d3++;

                if(d1<ret) ret=d1;
                if(d2<ret) ret=d2;
                if(d3<ret) ret=d3;

                T[cur][ib]=ret;
            }

            cur=1-cur;
        }

        int ret=T[1-cur][nb];

        return ret;
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
