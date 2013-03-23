#include <cstdlib>
#include <cstdio>
#include <sys/time.h>

#include "DFA.hpp"

int GetClockTimeInMilliSec()
{
    struct timeval t2; gettimeofday(&t2,NULL);
    return t2.tv_sec*1000+t2.tv_usec/1000;
}

void PrintTime(int milli_sec)
{
    int v=milli_sec;
    int hours=v/(1000*60*60); v%=(1000*60*60);
    int minutes=v/(1000*60); v%=(1000*60);
    int seconds=v/1000; v%=1000;
    int milli_seconds=v;
    int first=1;
    printf("%d[", milli_sec);
    if(hours) {if(!first) printf(":"); printf("%dh", hours); first=0;}
    if(minutes) {if(!first) printf(":"); printf("%dm", minutes); first=0;}
    if(seconds) {if(!first) printf(":"); printf("%ds", seconds); first=0;}
    if(milli_seconds) {if(!first) printf(":"); printf("%dms", milli_seconds); first=0;}
    printf("]");
}

int load (TrieDFA &trie, const char* filename)
{
    char W[32];
    int count=0;
    FILE* file = fopen(filename, "rt");
    if (!file) return -1;
    while (EOF != fscanf(file,"%s", W)) {
        if (trie.insertWord(W))
            count++;
    }
    fclose(file);
    return count;
}

void ui (TrieDFA &trie)
{
    int res, select;
    char str[32];

    printf(" ______________________________\n");
    printf("| CTRL-D: Exit                 |\n");
    printf("|------------------------------|\n");
    printf("| 1: INSERT a word in the Trie |\n");
    printf("| 2: SEARCH a word in the Trie |\n");
    printf("|______________________________|\n");

    while (EOF!=scanf("%d",&select))
    {
        switch(select)
        {
        case 1:
            printf("Enter the word you want to insert(up to 32 chars):\n");
            if (!scanf("%s",str)) break;
            trie.insertWord(str);
            printf("The word is inserted properly.\n");
            break;
        case 2:
            printf("Enter the word you want to serch for(up to 32 chars):\n");
            if (!scanf("%s",str)) break;
            res = trie.searchWord(str);
            if (res)
                printf("EXISTS! \n");
            else
                printf("NOT FOUND.\n");
            break;
        default:
            break;
        }

    }

}

int main(int argc, char* argv[])
{
    TrieDFA myTrie;

    if (argc > 1) {
        int v=GetClockTimeInMilliSec();
        int count = 0;
        count += load(myTrie, argv[1]);
        if ( count >=0 ) printf(">> Loaded %d words in: ", count); PrintTime(v=GetClockTimeInMilliSec()-v); printf("\n");
    }

    printf(">> Trie now contains %d words. \n", myTrie.wordCount());
    printf(">> Trie now contains %d states/nodes. \n", myTrie.stateCount());

    //~ LevensteinAutomaton L("food", 2);

    NFA &L = myTrie;
    set<StateIndex> states;

    int step=0;
    L.appendInitState(states);
    printf("STEP #%d: {", step); for(StateIndex i : states) printf("%2d, ", i); printf("}  - %s \n", L.isFinalState(states)? "Final State!" : "NOT Final State.");

    for (step=1; step<8; step++) {
        L.makeTransitions(states);
        printf("STEP #%d: {", step); for(StateIndex i : states) printf("%2d, ", i); printf("}  - %s \n", L.isFinalState(states)? "Final State!" : "NOT Final State.");
    }

    return 0;
}
