#ifndef AUTOMATA_H
#define AUTOMATA_H

#define NO_TRANS -1

typedef int StateIndex;

class State
{
public:
    explicit State () :
        trans_letter {
            NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS,
            NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS,
            NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS,
            NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS,
            NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS, NO_TRANS,
            NO_TRANS },
        ptr(NULL)
    {

    }

    /* Set transitions */
    void setLetterTransition (const char t, StateIndex index) {
        trans_letter[t-'a'] = index;
    }

    void setPtr (void* _ptr) {
        ptr = _ptr;
    }

    /* Get transitions */
    StateIndex operator[] (const char t) const {
        return trans_letter[t-'a'];
    }

    bool isFinal () const {
        return ptr!=NULL;
    }

    void* getPtr() const {
        return ptr;
    }

protected:
    StateIndex trans_letter[26];    /// Suport only letters [a-z]
    void* ptr;
};

class DFA
{
public:
    DFA (): num_final_states(0) {
        states.emplace_back();
    }

    StateIndex getInitState() const {
        return 0;
    }

    unsigned stateCount () const {
        return states.size();
    }

    unsigned finalStatesCount() const {
        return num_final_states;
    }

    State& operator[] (const int index){
        return states[index];
    }

protected:
    vector<State> states;
    unsigned num_final_states;

};

#include "word.hpp"

class DFATrie : public DFA
{
public:
    bool insertWord (const Word* word) {
        StateIndex cur=0;
        for (int i=0 ; word->txt.chars[i] ; i++) {
            if (states[cur][word->txt.chars[i]] == NO_TRANS) {
                states[cur].setLetterTransition(word->txt.chars[i], states.size());
                states.emplace_back();
            }
            cur = states[cur][word->txt.chars[i]];
        }
        if (states[cur].getPtr()==NULL) {
            states[cur].setPtr( (void*) word);
            num_final_states++;
            return true;
        }
        else return false;
    }

    unsigned size () const {
        return finalStatesCount();
    }

    void clear() {
        states.clear();
        states.emplace_back();
        num_final_states=0;
    }

};

#endif
