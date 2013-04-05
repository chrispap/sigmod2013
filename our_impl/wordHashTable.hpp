#ifndef WORD_HASH_TABLE_H
#define WORD_HASH_TABLE_H

class WordHashTable
{
    Word**              table;
    unsigned            capacity;
    unsigned            mSize;
    pthread_mutex_t     mutex;

    unsigned hash(const char *c) const {
        unsigned val = 0;
        while (*c) val = ((*c++) + 61 * val);
        val=val%capacity;
        return val;
    }

    void lock() {
        pthread_mutex_lock(&mutex);
    }

    void unlock() {
        pthread_mutex_unlock(&mutex);
    }

public:
    WordHashTable(unsigned _capacity) {
        pthread_mutex_init(&mutex,   NULL);
        mSize=0;
        capacity = _capacity;
        table = (Word**) malloc (capacity * sizeof(Word*));
        for (unsigned i=0 ; i<capacity ; i++) table[i] = 0;
    }

    ~WordHashTable() {
        free(table);
    }

    /**
     * Inserts the word that begins in c1 and terminates in c2
     * If the word was NOT in the table we allocate space for the word,
     * copy the word and store the address. If the word was already in
     * the table we store nothing.
     */
    bool insert (WordText &wtxt, Word** inserted_word) {
        //~ lock();
        unsigned index = hash(wtxt.chars);
        while (table[index] && !table[index]->equals(wtxt)) index = (index+1) % capacity;
        if (!table[index]) {
            table[index] = new Word(wtxt, index);
            mSize++;
            *inserted_word = table[index];
            //~ unlock();
            return true;
        }
        *inserted_word = table[index];
        //~ unlock();
        return false;
    }

    Word* getWord(unsigned index) const {
        if (index>=capacity) return NULL;
        else return table[index];
    }

    unsigned size() const { return mSize; }

};

#endif
