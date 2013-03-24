#ifndef AUTOMATA_H
#define AUTOMATA_H

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <set>

#include "indexHashTable.hpp"

#define NO_TRANS -1

using namespace std;

typedef int StateIndex;

class State
{
public:
    explicit State () {
        for (int i=0; i<26; i++) trans_letter[i]=NO_TRANS;
        trans_star[0] = NO_TRANS;
        trans_star[1] = NO_TRANS;
        trans_epsilon = NO_TRANS;
        word=NULL;
    }

    /* Set transitions */
    void setLetterTransition (const char t, StateIndex index) {
        trans_letter[t-'a'] = index;
    }

    void setEpsilonTransition (StateIndex index) {
        trans_epsilon = index;
    }

    void setStarTransitions (StateIndex index1, StateIndex index2=NO_TRANS) {
        trans_star[0] = index1;
        trans_star[1] = index2;
    }

    void setWord (void* ptr) {
        word = ptr;
    }

    /* Get transitions */
    StateIndex operator[] (const char t) const {
        return trans_letter[t-'a'];
    }

    StateIndex epsilonTransition () const {
        return trans_epsilon;
    }

    bool appendStarTransitions (IndexHashTable &state_set) const {
        if (trans_star[0] != NO_TRANS) {
            state_set.insert(trans_star[0]);
            if (trans_star[1] != NO_TRANS)
                state_set.insert(trans_star[1]);
            return true;
        } else return false;
    }

    bool isFinal () const {
        return word!=NULL;
    }

    void* getWord() const {
        return word;
    }

protected:
    StateIndex trans_letter[26];    /// Suport only letters [a-z]
    StateIndex trans_star[2];       /// Support max 2 star transitions
    StateIndex trans_epsilon;       /// Support max 1 epsilon transition
    void* word;
};

class NFA
{
public:
    NFA (): num_final_states(0) {
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

    void loadStartState (IndexHashTable &state_set) const {
        state_set.insert(getInitState());
        epsilonExpand(state_set);
    }

    bool epsilonExpand (IndexHashTable &state_set) const {
        bool flag=false;
        /* Append epsilon transitions recursively for the given set */
        for (StateIndex i : state_set.indexVec) {
            StateIndex ie = i;
            while ((ie=states[ie].epsilonTransition()) != NO_TRANS) {
                state_set.insert(ie);
                flag=true;
            }
        }

        return flag;
    }

    bool expand (const IndexHashTable &state_set_cur, IndexHashTable &state_set_next, const char letter=0) const {
        bool flag=false;

        /* Append normal and (*) transitions. */
        for (StateIndex i : state_set_cur.indexVec) {
            if (letter && states[i][letter] != NO_TRANS) {
                state_set_next.insert(states[i][letter]);flag=true;
            }

            if (states[i].appendStarTransitions(state_set_next)) flag = true;
        }

        epsilonExpand(state_set_next);
        return flag;
    }

    bool isFinalState (IndexHashTable &state_set) const {
        for (StateIndex i : state_set.indexVec)
            if (states[i].isFinal()) return true;
            return false;
    }

    void printStates (IndexHashTable &states) const {
        printf("States {");
        for (StateIndex i : states.indexVec) printf("%2d, ", i);
        printf("} %s \n", isFinalState(states)? " <--" : "");
    }

protected:
    vector<State> states;
    unsigned num_final_states;

    State& operator[](const int index){
        return states[index];
    }

};

class NFALevenstein : public NFA
{
public:
    /**
     * Construct levenstein automaton.
     * @param str   The word for which automaton will be constructed
     * @param t     Threshold
     */
    NFALevenstein (const char* str, int t) {
        int len = strlen(str);
        StateIndex i;
        int err, cons;

        /* Construct all the states. They are currently empty. */
        states.resize((len+1) * (t+1));

        /* Construct inner states */
        for (err=0 ; err<t ; err++) {
            for (cons=0 ; cons<len ; cons++) {
                i = err*(len+1)+cons;
                states[i].setLetterTransition(str[cons], i+1);
                states[i].setEpsilonTransition(i+len+2);
                states[i].setStarTransitions(i+len+1, i+len+2);
            }
        }

        /* Construct upper line */
        err = t;
        for (int cons=0 ; cons<len ; cons++){
            i = err*(len+1)+cons;
            states[i].setLetterTransition(str[cons], i+1);
        }

        /* Construct right-most column */
        cons = len;
        for (int err=0 ; err<t+1 ; err++) {
            i = err*(len+1)+cons;
            states[i].setStarTransitions(i+len+1);
            states[i].setWord((void*) 0x01);      /// Important! This is a final state.
            num_final_states++;
        }

    }

};

class DFALevenstein : public NFA
{
public:
    DFALevenstein (const NFALevenstein &nfa) {
        int nfa_states_num = nfa.stateCount();
        printf(">> NFA has %d states \n", nfa_states_num);

        IndexHashTable Q1 (nfa_states_num);
        nfa.loadStartState(Q1);
        nfa.printStates(Q1);

        while () {
            IndexHashTable statesFromNfa (nfa_states_num);
            for (char t='a' ; t<='z'; t++) {
                nfa.expand(, statesFromNfa, t);

            }


        }

    }

};

class DFATrie : public NFA
{
public:
    bool insertWord (const char* str) {
        StateIndex cur=0;
        for (int i=0 ; str[i] ; i++) {
            if (states[cur][str[i]] == NO_TRANS) {
                states[cur].setLetterTransition(str[i], states.size());
                states.emplace_back();
            }
            cur = states[cur][str[i]];
        }
        if (states[cur].getWord()==NULL) {
            states[cur].setWord((void*) 0x01); //TODO: Here I should store pointer to the word structure !!!
            num_final_states++;
            return true;
        }
        else return false;
    }

    bool searchWord (const char* str) const {
        StateIndex cur=0;
        for (int i=0 ; str[i] ; i++)
            if ((cur=states[cur][str[i]]) == NO_TRANS) return false;
        if (states[cur].getWord()==NULL) return false;
        else return true;
    }

    unsigned wordCount () const {
        return num_final_states;
    }

    void match (const char* str, int t) const {
        /* Construct the Levenstein DFA */
        DFALevenstein L(NFALevenstein (str, t));


    }

};

#endif
