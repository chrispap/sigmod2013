#ifndef AUTOMATA_H
#define AUTOMATA_H

#define NO_TRANS -1

typedef int StateIndex;

class State
{
public:
    explicit State () :
        trans_letter {
            NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS,
            NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS },
        ptr(NULL) {}

    /** Set transition */
    void setLetterTransition (const char t, StateIndex index) { trans_letter[t-'a'] = index;}
    void setPtr (void* _ptr) { ptr = _ptr;}

    /** Get transition */
    StateIndex operator[] (const char t) const { return trans_letter[t-'a'];}
    bool isFinal () const { return ptr!=NULL; }
    void* getPtr() const { return ptr; }

protected:
    StateIndex trans_letter[26];    /// Suport only letters [a-z]
    void* ptr;
};

class DFA
{
public:
    DFA (): num_final_states(0) { states.emplace_back();}

    unsigned stateCount () const { return states.size();}
    unsigned finalStateCount() const { return num_final_states;}
    State& operator[] (const int index) { return states[index];}

protected:
    vector<State> states;
    unsigned num_final_states;
};

#include "word.hpp"

class DFATrie : public DFA
{
public:
    bool insert (WordText &wtxt,  Word** inserted_word) {
        StateIndex cur=0;
        for (int i=0 ; wtxt.chars[i] ; i++) {
            if (states[cur][wtxt.chars[i]] == NO_TRANS) {
                states[cur].setLetterTransition(wtxt.chars[i], states.size());
                states.emplace_back();
            }
            cur = states[cur][wtxt.chars[i]];
        }
        if (states[cur].getPtr()==NULL) {
            *inserted_word = new Word (wtxt, num_final_states);
            states[cur].setPtr( (void*) *inserted_word);
            num_final_states++;
            return true;
        }
        else {
            *inserted_word = (Word*) states[cur].getPtr();
            return false;
        }

    }

    unsigned size () const {
        return finalStateCount();
    }

    void clear() {
        states.clear();
        states.emplace_back();
        num_final_states=0;
    }

};

#endif
