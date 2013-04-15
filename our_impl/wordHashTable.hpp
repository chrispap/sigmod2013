#ifndef WORD_HASH_TABLE_H
#define WORD_HASH_TABLE_H

class WordHashTable
{
    Word**              hash_table;
    vector<Word*>       wvec;
    unsigned            capacity;
    pthread_mutex_t     mutex;

    unsigned hash(WordText &wtxt) const {
        unsigned val = 0;
        char *c = wtxt.chars;
        while (*c) val = ((*c++) + 61 * val);
        val=val%capacity;
        return val;
    }

    void lock() { pthread_mutex_lock(&mutex); }
    void unlock() { pthread_mutex_unlock(&mutex); }

public:
    WordHashTable(unsigned _capacity) {
        pthread_mutex_init(&mutex,   NULL);
        capacity = _capacity;
        hash_table = (Word**) malloc (capacity * sizeof(Word*));
        for (unsigned i=0 ; i<capacity ; i++) hash_table[i] = 0;
    }

    ~WordHashTable() {
        free(hash_table);
    }

    /**
     * Inserts the word with text: `wtxt`.
     * Actually a new word is inserted and space is allicated ONLY
     * when the word did not already exist.
     */
    bool insert (WordText &wtxt, Word** inserted_word) {
        lock();
        unsigned index = hash(wtxt);
        while (hash_table[index] && !hash_table[index]->equals(wtxt))
            index = (index+1) % capacity;

        if (!hash_table[index]) {
            unsigned wid = wvec.size();
            wvec.push_back((*inserted_word = hash_table[index] = new Word(wtxt, wid)));
            unlock();
            return true;
        }
        *inserted_word = hash_table[index];
        unlock();
        return false;
    }

    Word *getWord (unsigned wid) const {
        return wvec[wid];
    }

    unsigned size() const {
        return wvec.size();
    }

};

#endif
