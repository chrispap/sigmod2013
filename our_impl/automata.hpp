#ifndef AUTOMATA_H
#define AUTOMATA_H

#define NO_TRANS -1

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

struct StatePair
{
    StateIndex s1;  // state index of the first dfa
    StateIndex s2;  // state idex of the second dfa
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
        for (unsigned i=0 ; i<state_set.indexVec.size() ; i++) {
            StateIndex ie = state_set.indexVec[i];
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

        if (epsilonExpand(state_set_next)) flag = true;
        return flag;
    }

    bool isFinalState (IndexHashTable &state_set) const {
        for (StateIndex i : state_set.indexVec)
            if (states[i].isFinal()) return true;
        return false;
    }

    void printStates (IndexHashTable &states) const {
        printf("{");
        for (StateIndex i : states.indexVec) printf("%2d, ", i);
        printf("} %s \n", isFinalState(states)? " <--" : "");
    }

    StateIndex evaluateInput (const char* str) const {
        StateIndex cur=0;
        for (int i=0 ; str[i] ; i++)
            if ((cur=states[cur][str[i]]) == NO_TRANS) return NO_TRANS;
        if (states[cur].isFinal()) return cur;
        else return NO_TRANS;
    }

    State& operator[](const int index){
        return states[index];
    }

protected:
    vector<State> states;
    unsigned num_final_states;

};

class NFALevenstein : public NFA
{
public:
    /**
     * Construct levenstein automaton.
     * @param str   The word for which automaton will be constructed
     * @param t     Threshold
     */
    NFALevenstein (const char* str, long t) {
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
        for (long err=0 ; err<t ; err++) {
            i = err*(len+1)+cons;
            states[i].setStarTransitions(i+len+1);
            states[i].setWord((void*) (err+1));        /// In the final state pointer store the information about the matching distance. Add +1 so that distance `0` will not be false
            num_final_states++;
        }

        i = (len+1)*(t+1)-1;
        states[i].setWord((void*) (t+1));            /// Important! This is the case for matching with distance == threshold final state.
    }

    long minDistance (IndexHashTable &state_set) {
        long dist, min=10;
        for (StateIndex i : state_set.indexVec) {
            dist = (long) states[i].getWord();
            if (dist && dist<min) min = dist;
        }
        if (min!=10) return min;
        else return 0;
    }

};

class DFALevenstein : public NFA
{
public:
    /**
     * Converts NFA 2 FDA
     * @param nfa The NFA automaton to convert
     */
    DFALevenstein (const char* str, int t) {
        NFALevenstein nfa(str, t);
        int nfa_states_num = nfa.stateCount();

        vector<IndexHashTable> dfa_states;          // Here, I will keep track of the states to be processed
        unsigned sp;                                // Points to the state we processed so far

        dfa_states.emplace_back(nfa_states_num);    // The starting state does not need to be created because it has already been taken care by parent constructor!
        sp=0;

        nfa.loadStartState(dfa_states[0]);          // Load the start state of the NFA

        /* Here is done the whole thing... */
        IndexHashTable states_from_nfa (nfa_states_num);
        while (sp < dfa_states.size())
        {
            /* Expand a dfa_state (combination of NFA States) */
            for (char t='a' ; t<='z'; t++)
            {
                states_from_nfa.clear();
                nfa.expand(dfa_states[sp], states_from_nfa, t); //continue;

                /* Check the new set with all the existing sets (DFA States) */
                StateIndex index;
                for (index=0 ; index!=(StateIndex)dfa_states.size() ; ++index)
                    if (IndexHashTable::equals(states_from_nfa, dfa_states[index])) break;

                if (index==(StateIndex)dfa_states.size()) {     // Here we have a new state for the DFA
                    states.emplace_back();
                    states.back().setWord( (void*) nfa.minDistance(states_from_nfa));
                    dfa_states.push_back(states_from_nfa);
                }

                states[sp].setLetterTransition(t, index);
            }

            sp++;
        }

        //~ fprintf(stderr, ">> Word: %-16s NFA: %3d DFA: %3d \n", str, nfa_states_num, stateCount());
    }

    long distance (StateIndex index) {
        return (((long)(states[index].getWord()))-1);
    }

};

#include "word.hpp"

class DFATrie : public NFA
{
public:
    bool insertWord (const Word* word) {
        StateIndex cur=0;
        for (int i=0 ; word->txt[i] ; i++) {
            if (states[cur][word->txt[i]] == NO_TRANS) {
                states[cur].setLetterTransition(word->txt[i], states.size());
                states.emplace_back();
            }
            cur = states[cur][word->txt[i]];
        }
        if (states[cur].getWord()==NULL) {
            states[cur].setWord( (void*) word);
            num_final_states++;
            return true;
        }
        else return false;
    }

    bool searchWord (const char* str) const {
        return evaluateInput(str)-NO_TRANS; // if evaluate_input returns N_TRANS we return 0 --> false! :o
    }

    unsigned wordCount () const {
        return num_final_states;
    }

    void clear() {
        states.clear();
        states.emplace_back();
        num_final_states=0;
    }

    void dfaIntersect (Word *word) {
        DFATrie &trie = *this;
        DFALevenstein &autom = word->dfa != NULL ? *word->dfa : *(word->dfa=new DFALevenstein(word->txt, 3));

        unsigned sp = 0;    // StackPointer
        vector<StatePair> stack;
        stack.emplace_back();
        stack[0].s1 = 0;
        stack[0].s2 = 0;

        StatePair ns;
        StateIndex t1, t2;

        while (sp < stack.size())
        {
            /* Expand a dfa_state (combination of NFA States) */
            for (char t='a' ; t<='z'; t++)
            {
                t1 = trie[stack[sp].s1][t];
                t2 = autom[stack[sp].s2][t];
                if( t1 != NO_TRANS && t2 != NO_TRANS) {  // Same Transition in both DFAs !
                    ns.s1 = t1;
                    ns.s2 = t2;
                    stack.push_back(ns);
                    if (trie[t1].isFinal() && autom[t2].isFinal()) {
                        long d = autom.distance(t2);
                        char* dold = &(((Word*)trie[t1].getWord())->qWordsDist_edit[word->qWIndex[MT_EDIT_DIST]]);
                        if (d < *dold) *dold = d;
                    }
                }
            }
            sp++;
        }

    }

};

#endif
