#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>

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


void test (Trie &trie)
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

}

int main()
{
    Trie myTrie;

    test(myTrie);

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
            myTrie.insertWord(str);
            printf("The word is inserted properly.\n");
            break;
        case 2:
            printf("Enter the word you want to serch for(up to 32 chars):\n");
            scanf("%s",str);
            res = myTrie.searchWord(str);
            if(res == 1)
            printf("The word exists in the dictionary.\n");
            else if(res == -1)
            printf("Sorry, the word does not exist in the dictionary.\n");
            break;
        case 3:
            myTrie.print();
            break;
        default:
            printf("Invalid choice \n");
        }

    } while (select != 0);

    return 0;
}
