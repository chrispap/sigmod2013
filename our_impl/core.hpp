#include <core.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>
#include <pthread.h>

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
        if (this==w) return true;
        const int *i1= (int*) txt;
        const int *i2= (int*) w->txt;
        while ( i1!=(int*)(txt+(MAX_WORD_LENGTH+1)) && *i1==*i2) {++i1; ++i2;}
        if (i1!=(int*)(txt+(MAX_WORD_LENGTH+1))) return true;
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

class WordHashTable
{
    Word**              table;
    unsigned            capacity;
    unsigned            mSize;
    pthread_mutex_t     mutex;

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

    void lock() { pthread_mutex_lock(&mutex); }

    void unlock() { pthread_mutex_unlock(&mutex); }

public:
    WordHashTable(unsigned _capacity)
    {
        pthread_mutex_init(&mutex,   NULL);
        mSize=0;
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
            mSize++;
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

    unsigned size() const { return mSize; }

};

class IndexHashTable
{
    typedef unsigned unit;

    unit*               units;
    unsigned            capacity;
    unsigned            numUnits;
    unsigned            bitsPerUnit;
    unsigned            mSize;
    pthread_mutex_t     mutex;
    bool                keepIndexVec;

    void lock() { pthread_mutex_lock(&mutex); }

    void unlock() { pthread_mutex_unlock(&mutex); }

public:
    IndexHashTable(unsigned _capacity, bool _keepIndexVec=true)
    {
        pthread_mutex_init(&mutex,   NULL);
        mSize=0;
        keepIndexVec = _keepIndexVec;
        capacity = _capacity;
        bitsPerUnit = (sizeof(unit) * 8);
        numUnits = capacity/bitsPerUnit;
        if (capacity%bitsPerUnit) numUnits++;                   // An to capacity den einai akeraio pollaplasio tou bitsPerUnit, tote theloume allo ena unit.
        units = (unit*) malloc ( numUnits*sizeof(unit));        // Allocate space with capacity bits. (NOT BYTES, BITS!)
        if (units==0) {fprintf(stderr, "Could not allocate memory for IndexHashTable"); exit(-1);}
        for (unsigned i=0 ; i<numUnits ; i++) units[i]=0;
    }

    ~IndexHashTable()
    {
        free(units);
    }

    vector<unsigned> indexVec;

    bool insert (unsigned index)
    {
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
            mSize++;
            if (keepIndexVec) indexVec.push_back(index);
            unlock();
            return true;
        }
    }

    unsigned size() const {return mSize;}

    void clear ()
    {
        for (unsigned i=0 ; i<numUnits ; i++) units[i]=0;
        indexVec.clear();
        mSize=0;
    }

};

struct Query
{
    MatchType       type;
    char            dist;
    char            numWords;
    Word*           words[MAX_QUERY_WORDS];
};

struct Document
{
    DocID           id;
    char            *str;
    IndexHashTable  *wordIndices;
    unsigned        numRes;
    QueryID*        matchingQueryIDs;
};
