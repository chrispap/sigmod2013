#ifndef WORD_HASH_TABLE_H
#define WORD_HASH_TABLE_H

class WordDB
{
    DFATrie             trie;
    vector<Word*>       wvec;
    unsigned            capacity;
    pthread_mutex_t     mutex;

    void lock()   { pthread_mutex_lock(&mutex); }

    void unlock() { pthread_mutex_unlock(&mutex); }

public:
    WordDB () { pthread_mutex_init(&mutex,   NULL);}

    Word *getWord (unsigned wid) const { return wvec[wid]; }

    unsigned size() const { return wvec.size(); }

    /**
     * Inserts the word with text: `wtxt`.
     * Actually a new word is inserted and space is allocated, ONLY
     * when the word does not already exist in our storage.
     */
    bool insert (WordText &wtxt, Word** inserted_word) {
        lock();

        if (trie.insert(wtxt, inserted_word)) {
            wvec.push_back(*inserted_word);
            unlock();
            return true;
        }

        unlock();
        return false;
    }

};

#endif
