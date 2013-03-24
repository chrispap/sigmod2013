#include <cstdlib>
#include <cstdio>
#include <sys/time.h>

#include "../our_impl/automata.hpp"

/**
 * Compile with:
 *      g++ -std=c++11 -Wall -O3 -g test_automata.cpp -o test_automata
 */

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
    printf("]\n");
}

const char* words_in[] = {
    "abcdefghijklmnop",
    "abcdefghijklmno",
    "abcdefghijklmn",
    "abcdefghijklm",
    "abcdefghijkl",
    "abcdefghijk",
    "abcdefghij",
    "abcdefghi",
    "abcdefgh",
    "abcdefg",
    "abcdef",
    "abcde",
    "christos",
    "christo",
    "christ",
    "chris",
    "chri",
    "chr",
    "mariaki",
    "mariak",
    "maria",
    "mari",
    "foodxxx",
    "foodxx",
    "foodx",
    "food",
    "fod",
    "foo",
    "ood",
    "oo",
};

const char* words_not_in[] = {
    "abc",
    "ch",
    "mar",
    "c",
    "m",
};

int loadWords (DFATrie &trie)
{
    for (const char *w : words_in) trie.insertWord(w);

    for (const char *w : words_in)
        if (!trie.searchWord(w)) printf(">> Did not find word: %s although it has been inserted. \n", w);

    for (const char *w : words_not_in)
        if (trie.searchWord(w)) printf(">> Find word: %s although it has NOT been inserted. \n", w);

    return 0;
}

int loadFile (DFATrie &trie, const char* filename)
{
    char W[32];
    int count=0;
    FILE* file = fopen(filename, "rt");
    if (!file) return -1;

    int v=GetClockTimeInMilliSec();
    while (EOF != fscanf(file,"%s", W)) {
        if (trie.insertWord(W))
            count++;
    }

    if ( count >=0 ) {
        printf(">> Loaded %d unique words in: ", count);
        PrintTime(v=GetClockTimeInMilliSec()-v);
    }

    fclose(file);
    return count;
}

void menu (DFATrie &trie)
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
    /* Populate the trie */
    DFATrie trie;
    loadWords(trie);
    loadFile(trie, "query_words.txt");
    printf(">> Trie now contains %d words. \n", trie.wordCount());
    printf(">> Trie now contains %d states. \n\n", trie.stateCount());

    /* Check for matches */
    const char* W = "christoferakos";
    int t=3;

    /* Create the Levenstein Automaton for a word*/
    int v=GetClockTimeInMilliSec();
    DFALevenstein dfa(NFALevenstein (W, t));
    printf(">> DFA Constructed in: ");PrintTime(v=GetClockTimeInMilliSec()-v);

    /* Check a batch of words for matching */
    int match_count=0;
    v=GetClockTimeInMilliSec();
    do {
        for (const char *w : words_in) {
            dfa.evaluateInput(w);
            match_count++;
            //~ printf("%s/%d - %-16s %s \n", W, t, w, dfa.evaluateInput(w)!=NO_TRANS ? "YES! " : "NO" );
        }
    } while (match_count<10000000);
    printf("Did %d matches in ", match_count); PrintTime(v=GetClockTimeInMilliSec()-v);


    //~ menu(trie);
    return 0;
}
