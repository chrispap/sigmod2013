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
        clear();
    }

    ~WordHashTable()
    {
        free(table);
    }

    void clear ()
    {
         for (unsigned i=0 ; i<capacity ; i++) table[i] = 0;
    }

    /**
     * Inserts the word that begins in c1 and terminates in c2
     * If the word was NOT in the table we allocate space for the word,
     * copy the word and store the address. If the word was already in
     * the table we do nothing.
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

    /**
     * Inserts a word in the table. Space is NEVER allocated.
     * If the word doesnt exist we just store the pointer.
     *  @return true if a new Word was created or false if an equivalent Word already existed
     */
    bool insert (Word* word)
    {
        lock();
        unsigned index = hash(word);
        while (table[index] && word->equals(table[index])) index = (index+1) % capacity;
        if (!table[index]) {
            table[index] = word;
            unlock();
            return true;
        }
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
    atomic_flag         guard=ATOMIC_FLAG_INIT;

    void lock() { while (guard.test_and_set(memory_order_acquire)); }

    void unlock() { guard.clear(std::memory_order_release);}

public:
    IndexHashTable(unsigned _capacity)
    {
        capacity = _capacity;
        bitsPerUnit = (sizeof(unit) * 8);
        numUnits = capacity/bitsPerUnit;
        if (capacity%bitsPerUnit) numUnits++;                   // An to capacity den einai akeraio pollaplasio tou bitsPerUnit, tote theloume allo ena unit.
        units = (unit*) malloc ( numUnits*sizeof(unit));        // Allocate space with capacity bits. (NOT BYTES, BITS!)
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
            indexVec.push_back(index);
            unlock();
            return true;
        }
    }

};

struct PendingDoc
{
    DocID           id;
    char            *str;
    IndexHashTable  *wordIndices;
};

struct DocResult
{
    DocID       docID;
    unsigned    numRes;
    QueryID*    queryIDs;
};
