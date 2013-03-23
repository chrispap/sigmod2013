#include <cstdlib>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <sys/time.h>

#define MAX_WORD_LENGTH 31

struct TrieNode
{
    /* Data */
    char key;
    bool wordEnd;
    struct TrieNode* kids[26];
};

class Trie
{
    /** Root Node */
    TrieNode* root;

    /* Static Methods */
    static TrieNode* createNode(char key, bool wordEnd)
    {
        TrieNode *n = (TrieNode*) malloc(sizeof(TrieNode));
        n->key = key;
        n->wordEnd = wordEnd;
        for(int i=0; i<='z'-'a' ; i++)
            n->kids[i] = NULL;
        return n;
    }

    static TrieNode* addNode (TrieNode* parent, char key, bool wordEnd)
    {
        int k = key - 'a';
        if(parent->kids[k] == NULL)
            parent->kids[k] = createNode(key, wordEnd);
        else
            parent->kids[k]->wordEnd |= wordEnd;
        return parent->kids[k];
    }

    static void printNode (char c, int h)
    {
        for (int i= 0;i<h; i++)
            printf(" ");
        printf("%c\n",c);
        return;
    }

    static void print (TrieNode *newnode, int h)
    {
        if(newnode == NULL) {
            printNode('*', h);
            return;
        }
        printNode(newnode->key, h);
        int i, k, l = h+1;
        for(i = 'a'; i<='z'; i++) {
            k = i - 'a';
            if(newnode->kids[k] != NULL)
                print(newnode->kids[k],l);
        }
        return;
    }


public:
    Trie()
    {
        root = createNode(0, false);
    }

    void insertWord (const char * str)
    {
        TrieNode* t = root;
        int last = strlen(str)-1;
        for (int i=0 ; str[i]!=0 ; i++)
            t = t->kids[str[i]-'a'] = addNode(t, str[i], i==last);
        return;
    }

    int searchWord (const char * str)
    {
        TrieNode* t = root;
        int i, last = strlen(str)-1;
        int k = 0;
        i = 0;
        while(str[i] != 0) {
            k = str[i]-'a';
            if(t->kids[k] == NULL) return 0;
            else {
                if(t->kids[k]->key != str[i])
                    return 0;
                else {
                    if(i == last && t->kids[k]->wordEnd)
                        return 1;
                }
            }
            t = t->kids[str[i]-'a'];
            i++;
        }
        return 0;
    }

    void print ()
    {
        print(root, 0);
    }

};

int test (Trie &trie)
{
    const char* words_in[] = {
        "chris",
        "christos",
        "christ",
        "papapaulou",
        "christospapapavlou",
        "mariapredari",
        "mariaki",
        "maria",
    };

    const char* words_not_in[] = {
        "abc",
        "ch",
        "mar",
        "c",
        "m",
    };

    for (const char *w : words_in) trie.insertWord(w);

    for (const char *w : words_in)
        if (!trie.searchWord(w))
            printf("Did not find word: %s although it has been inserted. \n", w);

    for (const char *w : words_not_in)
        if (trie.searchWord(w))
            printf("Find word: %s although it has NOT been inserted. \n", w);

    return 0;
}

int load (Trie &trie, const char* filename)
{
    char W[32];
    int count=0;
    FILE* file = fopen(filename, "rt");
    if (!file) return -1;
    while (EOF != fscanf(file,"%s", W)) {
        trie.insertWord(W);
        count++;
    }
    fclose(file);
    return count;
}

void ui (Trie &trie)
{
    int res, select;
    char str[MAX_WORD_LENGTH];

    printf("\nHello User. \nPlease select one of these choices: \n");
    printf("----------------------------------------------------\n");
    printf("0. QUIT \n");
    printf("1. INSERT a word in the Trie \n");
    printf("2. SEARCH a word in the Trie \n");
    printf("3. PRINT the Trie \n");
    printf("----------------------------------------------------\n");

    do
    {
        scanf("%d",&select);

        switch(select)
        {
        case 0:
            break;
        case 1:
            printf("Enter the word you want to insert(up to 32 chars):\n");
            scanf("%s",str);
            trie.insertWord(str);
            printf("The word is inserted properly.\n");
            break;
        case 2:
            printf("Enter the word you want to serch for(up to 32 chars):\n");
            scanf("%s",str);
            res = trie.searchWord(str);
            if (res)
                printf("The word exists in the dictionary.\n");
            else
                printf("Sorry, the word does not exist in the dictionary.\n");
            break;
        case 3:
            trie.print();
            break;
        default:
            printf("Invalid choice \n");
        }

    } while (select != 0);

}

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

int main(int argc, char* argv[])
{
    Trie myTrie;

    if (argc > 1) {
        int v=GetClockTimeInMilliSec();
        int count = 0;
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        count += load(myTrie, argv[1]);
        if ( count >=0 ) {
            v=GetClockTimeInMilliSec()-v;
            printf("Loaded %d words in: ", count); PrintTime(v); printf("\n");
        }
    }

    ui(myTrie);
    return 0;
}
