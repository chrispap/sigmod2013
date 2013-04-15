#ifndef AUTOMATA_H
#define AUTOMATA_H

#define NO_TRANS NULL

struct State
{

    State *trans_letter[26];    /// Suport only letters [a-z]
    void* ptr;

    State () : ptr(NULL) { for (int l=0; l<26;l++) trans_letter[l]=NO_TRANS;}

    State* setLetterTransition (const char t) { return (trans_letter[t-'a'] = new State());}
    State* getLetterTransition (const char t) const { return trans_letter[t-'a'];}
    bool   isFinal () const { return ptr!=NULL; }
};

class DFA
{
public:
    DFA (): num_final_states(0) { root = new State();}

    unsigned finalStateCount() const { return num_final_states;}

protected:
    State *root;
    unsigned num_final_states;
};

class DFATrie : public DFA
{
public:
    bool insert (WordText &wtxt,  Word** inserted_word) {
        State *cur=root, *next;
        for (int i=0 ; wtxt.chars[i] ; i++) {
            if ((next = cur->getLetterTransition(wtxt.chars[i])) == NO_TRANS)
                cur = cur->setLetterTransition(wtxt.chars[i]);
            else cur = next;
        }

        if (cur->ptr==NULL) {
            *inserted_word = new Word (wtxt, num_final_states);
            cur->ptr =  (void*) *inserted_word;
            num_final_states++;
            return true;
        }
        else {
            *inserted_word = (Word*) cur->ptr;
            return false;
        }
    }

    bool contains (WordText &wtxt,  Word** inserted_word) const {
        State *cur = root;
        for (int i=0 ; wtxt.chars[i] ; i++)
            if ((cur = cur->getLetterTransition(wtxt.chars[i])) == NO_TRANS) return false;

        if (cur->ptr == NULL) return false;
        *inserted_word = (Word*) cur->ptr;
        return true;
    }

    unsigned size () const {
        return finalStateCount();
    }

};

#endif
