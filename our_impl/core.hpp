#include <core.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>
#include <atomic>

using namespace std;

struct Word
{
    int         length;                                 ///< Strlen(txt);
    unsigned    letterBits;                             ///< 1 bit for every char [a-z]
    set<QueryID> querySet[3];                           ///< Sets of queries matching this word. One set for each MatchType
    char        txt[MAX_WORD_LENGTH+1];                 ///< The actual word :P

    /** Store the new word and populate the data structures */
    Word (const char *c1, const char *c2)
    {
        letterBits=0;
        int i=0;
        while (c1!=c2) {
            letterBits |= 1 << (*c1-'a');
            txt[i++] = *c1++;
        }
        length = i;
        while (i<MAX_WORD_LENGTH+1) txt[i++] = 0;
    }

    bool equals(const char *c1, const char *c2) const
    {
        const char *_txt = txt;
        while (c1!=c2 && *_txt) if (*_txt++ != *c1++) return false;
        if (c1==c2 && !*_txt) return true;
        else return false;
    }

    bool equals(Word* w) const
    {
        const long *i1= (long*) txt;
        const long *i2= (long*) w->txt;
        while ( i1!=(long*)(txt+(MAX_WORD_LENGTH+1)) && *i1==*i2) {++i1; ++i2;}
        if (i1!=(long*)(txt+(MAX_WORD_LENGTH+1))) return true;
        else return false;
    }

    int letterDiff(Word *w )
    {
        return __builtin_popcount(this->letterBits ^ w->letterBits);
    }

    int EditDist(Word *w)
    {
        char* a = w->txt;
        int na = w->length;
        char* b = this->txt;
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

    int HammingDist(Word *w)
    {
        char* a = w->txt;
        int na = w->length;
        char* b = this->txt;
        int nb = this->length;

        int j, oo=0x7FFFFFFF;
        if(na!=nb) return oo;

        unsigned int num_mismatches=0;
        for(j=0;j<na;j++) if(a[j]!=b[j]) num_mismatches++;

        return num_mismatches;
    }

};

struct Query
{
    MatchType       type;
    char            dist;
    char            numWords;
    Word*           words[MAX_QUERY_WORDS];
};

class WordHashTable
{
    Word**          table;
    unsigned        capacity;
    atomic_flag     guard=ATOMIC_FLAG_INIT;

    unsigned hash(const char *c1, const char *c2) const
    {
        unsigned val = 0;
        while (c1!=c2) val = ((*c1++) + 61 * val);
        val=val%capacity;
        return val;
    }

    unsigned hash(const Word* w) const
    {
        const char *c = w->txt;
        unsigned val = 0;
        while (*c) val = ((*c++) + 61 * val);
        val=val%capacity;
        return val;
    }

    void lock() { while (guard.test_and_set(memory_order_acquire)); }

    void unlock() { guard.clear(std::memory_order_release); }

public:
    WordHashTable(unsigned _capacity)
    {
        capacity = _capacity;
        table = (Word**) malloc (capacity * sizeof(Word*));
        for (unsigned i=0 ; i<capacity ; i++) table[i] = 0;
    }

    ~WordHashTable()
    {
        free(table);
    }

    /**
     * Inserts the word that begins in c1 and terminates in c2
     * If the word was NOT in the table we allocate space for the word,
     * copy the word and store the address. If the word was already in
     * the table we store nothing.
     *
     *  @param c1 The first char of the string to insert.
     *  @param c2 One char past the last char of the string to insert.
     *  @param c2 One char past the last char of the string to insert.
     *  @return true if a new Word was created or false if an equivalent Word already existed
     */
    bool insert (const char *c1, const char *c2, unsigned* inserted_word_index, Word** inserted_word)
    {
        lock();
        unsigned index = hash(c1, c2);
        while (table[index] && !table[index]->equals(c1, c2)) index = (index+1) % capacity;
        if (!table[index]) {
            table[index] = new Word(c1, c2);
            *inserted_word_index = index;
            *inserted_word = table[index];
            unlock();
            return true;
        }
        *inserted_word_index = index;
        *inserted_word = table[index];
        unlock();
        return false;
    }

    Word* getWord(unsigned index) const
    {
        if (index>=capacity) return NULL;
        else return table[index];
    }
};

class IndexHashTable
{
    typedef unsigned unit;

    unit*               units;
    unsigned            capacity;
    unsigned            numUnits;
    unsigned            bitsPerUnit;
    bool                keepIndexVec;
    atomic_flag         guard=ATOMIC_FLAG_INIT;

    void lock() { while (guard.test_and_set(memory_order_acquire)); }

    void unlock() { guard.clear(std::memory_order_release);}

public:
    IndexHashTable(unsigned _capacity, bool _keepIndexVec=true)
    {
        keepIndexVec = _keepIndexVec;
        capacity = _capacity;
        bitsPerUnit = (sizeof(unit) * 8);
        numUnits = capacity/bitsPerUnit;
        if (capacity%bitsPerUnit) numUnits++;                   // An to capacity den einai akeraio pollaplasio tou bitsPerUnit, tote theloume allo ena unit.
        units = (unit*) malloc ( numUnits*sizeof(unit));        // Allocate space with capacity bits. (NOT BYTES, BITS!)
        if (units==0)
        for (unsigned i=0 ; i<numUnits ; i++) units[i]=0;
    }

    ~IndexHashTable()
    {
        free(units);
    }

    vector<unsigned> indexVec;

    bool insert (unsigned index)
    {
        if (index >= capacity) {fprintf(stderr, "CANNOT STORE INDEX %u (TOO LARGE) \n", index); fflush(stderr); return false;}
        lock();
        unsigned unit_offs = index / bitsPerUnit;
        unsigned unit_bit  = index % bitsPerUnit;
        unit mask = 1 << unit_bit;
        if (units[unit_offs] & mask) {
            unlock();
            return false;
        }
        else {
            units[unit_offs] |= mask;
            if (keepIndexVec) indexVec.push_back(index);
            unlock();
            return true;
        }
    }

    void clear ()
    {
        for (unsigned i=0 ; i<numUnits ; i++) units[i]=0;
        indexVec.clear();
    }

};

struct Document
{
    DocID           id;
    char            *str;
    IndexHashTable  *wordIndices;
    unsigned        numRes;
    QueryID*        matchingQueryIDs;
};
