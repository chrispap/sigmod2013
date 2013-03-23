#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <set>

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

    bool appendStarTransitions (set<StateIndex> &state_set) const {
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

    void loadStartState (set<StateIndex> &state_set) const {
        state_set.insert(getInitState());
        epsilonExpand(state_set);
    }

    bool epsilonExpand (set<StateIndex> &state_set) const {
        bool flag=false;
        /* Append epsilon transitions recursively */
        for (StateIndex i : state_set) {
            StateIndex ie = i;
            while ((ie=states[ie].epsilonTransition()) != NO_TRANS) {
                state_set.insert(ie);
                flag=true;
            }
        }

        return flag;
    }

    bool makeTransitions (set<StateIndex> &old_state_set, const char* trans=0) const {
        set<StateIndex> new_state_set;
        bool flag=false;

        /* Append normal and (*) transitions. */
        for (StateIndex i : old_state_set) {
            if (trans) {
                if (states[i][*trans] != NO_TRANS) new_state_set.insert(states[i][*trans]);
                flag=true;
                //trans++;
            }

            if (states[i].appendStarTransitions(new_state_set))
                flag = true;
        }

        epsilonExpand(new_state_set);
        old_state_set = new_state_set;

        return flag;
    }

    bool isFinalState (set<StateIndex> &state_set) const {
        for (StateIndex i : state_set)
            if (states[i].isFinal()) return true;
            return false;
    }

    void printStates (set<StateIndex> &states) const {
        printf("States {");
        for (StateIndex i : states) printf("%2d, ", i);
        printf("}  - %s \n", isFinalState(states)? " <--" : "");
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
        set<StateIndex> currentstate;

        nfa.loadStartState(currentstate);



        nfa.printStates(currentstate);

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
