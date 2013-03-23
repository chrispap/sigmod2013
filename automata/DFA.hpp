#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <set>

using namespace std;

typedef int StateIndex;

struct State
{
    StateIndex trans_normal[26];
    StateIndex trans_star[2];
    StateIndex trans_epsilon;
    void* word;

    explicit State (): word(NULL) {
        for (int i=0; i<26; ++i) trans_normal[i]=-1;
        trans_epsilon = -1;
        trans_star[0] = -1;
        trans_star[1] = -1;
    }

    bool isFinal () const {
        return word!=NULL;
    }

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

    void appendInitState (set<StateIndex> &old_state_set) {
        old_state_set.insert(getInitState());
        makeEpsilonTransitions(old_state_set);
    }

    bool makeEpsilonTransitions (set<StateIndex> &old_state_set) {
        bool flag=false;
        /* Append epsilon transitions recursively */
        for (StateIndex i : old_state_set) {
            StateIndex ie = i;
            while (states[ie].trans_epsilon!=-1) {
                ie = states[ie].trans_epsilon;
                old_state_set.insert(ie);
                flag=true;
            }
        }

        return flag;
    }

    /**
     * @parap trans int array with allowable transitions
     */
    bool makeTransitions (set<StateIndex> &old_state_set, const int* trans=0) {
        set<StateIndex> new_state_set;
        bool flag=false;

        /* Append normal and (*) transitions. */
        for (StateIndex i : old_state_set) {
            if (trans) {
                if (states[i].trans_normal[*trans] !=-1) new_state_set.insert(states[i].trans_normal[*trans]);
                flag=true; trans++;
            }
            if (states[i].trans_star[0] != -1) {
                new_state_set.insert(states[i].trans_star[0]);
                flag=true;
            }

            if (states[i].trans_star[1] != -1) {
                new_state_set.insert(states[i].trans_star[1]);
                flag=true;
            }
        }

        makeEpsilonTransitions(new_state_set);
        old_state_set = new_state_set;

        return flag;
    }

    bool isFinalState (set<StateIndex> &state_set) {
        for (StateIndex i : state_set)
            if (states[i].isFinal()) return true;
            return false;
    }

protected:
    vector<State> states;
    unsigned num_final_states;

};

class LevensteinAutomaton : public NFA
{
public:
    LevensteinAutomaton (char* str, int t) {
        int len = strlen(str);
        states.resize((len+1) * (t+1));
        StateIndex i;
        int err, cons;

        for (err=0 ; err<t ; err++) {
            for (cons=0 ; cons<len ; cons++) {
                i = err*(len+1)+cons;
                states[i].trans_normal[str[cons]-'a'] = i+1;
                states[i].trans_epsilon = i+len+2;
                states[i].trans_star[0] = i+len+1;
                states[i].trans_star[1] = i+len+2;
            }
        }

        err = t;
        for (int cons=0 ; cons<len ; cons++){
            i = err*(len+1)+cons;
            states[i].trans_normal[cons-'a'] = i+1;
        }

        cons = len;
        for (int err=0 ; err<t+1 ; err++) {
            i = err*(len+1)+cons;
            states[i].trans_star[0]=i+len+1;
            states[i].word = (void*) 0x01;
            num_final_states++;
        }

    }

};

class TrieDFA : public NFA
{
public:
    bool insertWord (const char* str) {
        StateIndex cur=0;
        for (int i=0 ; str[i] ; i++) {
            int k = str[i]-'a';
            if (states[cur].trans_normal[k] < 0) {
                states[cur].trans_normal[k] = states.size();
                states.emplace_back();
            }
            cur = states[cur].trans_normal[k];
        }
        if (states[cur].word==NULL) {
            states[cur].word = (void*) 0x01; //TODO: Here I should store pointer to the word structure !!!
            num_final_states++;
            return true;
        }
        else return false;
    }

    bool searchWord (const char* str) const {
        StateIndex cur=0;
        for (int i=0 ; str[i] ; i++) {
            int k = str[i]-'a';
            if (states[cur].trans_normal[k] < 0) return false;
            cur = states[cur].trans_normal[k];
        }
        if (states[cur].word==NULL) return false;
        else return true;
    }

    unsigned wordCount () const {
        return num_final_states;
    }

    void match (LevensteinAutomaton &lev, set<StateIndex> &matches) {
        set<StateIndex> trie_set, lev_set;

        /* Initialize active state sets */
        appendInitState(trie_set);
        lev.appendInitState(lev_set);

        //int transitions[27];



    }

};
