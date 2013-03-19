#include <core.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>
#include <atomic>

using namespace std;

struct DocResult
{
    DocID       docID;
    unsigned    numRes;
    QueryID*    queryIDs;
};

struct PendingDoc
{
    DocID       id;
    char        *str;
};

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
    MatchType   type;
    char        dist;
    char        numWords;
    Word*       words[MAX_QUERY_WORDS];
};

class WordHashTable
{
private:
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
        while (c) val = ((*c++) + 61 * val);
        val=val%capacity;
        return val;
    }

    void lock()
    {
         while (guard.test_and_set(memory_order_acquire));
    }

    void unlock()
    {
        guard.clear(std::memory_order_release);
    }

public:
    WordHashTable (unsigned _capacity)
    {
        capacity = _capacity;
        contents.reserve(64);
        table = (Word**) malloc (capacity * sizeof(Word*));
        clear();
    }

    ~WordHashTable()
    {
        free(table);
    }

    vector<Word*>   contents;

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
    bool insert (const char *c1, const char *c2, Word** inserted_word)
    {
        unsigned i = hash(c1, c2);
        while (table[i] && !table[i]->equals(c1, c2)) i = (i+1) % capacity;
        if (!table[i]) {
            table[i] = new Word(c1, c2);
            *inserted_word = table[i];
            contents.push_back(table[i]);
            return true;
        }
        *inserted_word = table[i];
        return false;
    }

    /**
     * Inserts a word in the table. Space is NEVER allocated.
     * If the word doesnt exist we just store the pointer.
     *  @return true if a new Word was created or false if an equivalent Word already existed
     */
    bool insert (Word* word)
    {
        unsigned i = hash(word);
        while (table[i] && word->equals(table[i])) i = (i+1) % capacity;
        if (!table[i]) {
            table[i] = word;
            contents.push_back(word);
            return true;
        }
        return false;
    }

    void clear ()
    {
         for (unsigned i=0 ; i<capacity ; i++) table[i] = 0;
    }

};
