#ifndef WORD_HASH_TABLE_H
#define WORD_HASH_TABLE_H

class WordHashTable
{
    Word**              table;
    unsigned            capacity;
    unsigned            mSize;
    pthread_mutex_t     mutex;

    unsigned hash(WordText &wtxt) const {
        unsigned val = 0;
        char *c = wtxt.chars;
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
     * Inserts the word with text: `wtxt`.
     * If the word was NOT in the table we allocate space for the word,
     * copy the word and store the address. If the word was already in
     * the table we store nothing.
     */
    bool insert (WordText &wtxt, Word** inserted_word) {
        lock();
        unsigned index = hash(wtxt);
        while (table[index] && !table[index]->equals(wtxt)) index = (index+1) % capacity;
        if (!table[index]) {
            *inserted_word = table[index] = new Word(wtxt, index);
            mSize++;
            unlock();
            return true;
        }
        *inserted_word = table[index];
        unlock();
        return false;
    }

    Word* getWord(unsigned index) const {
        return table[index];
    }

    unsigned size() const { return mSize; }

};

#endif
