#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SIZE 32*4

/**
 *  @member nc:             The number of characters consumed so far.
 *  @member ne:             The number of errors so far.
 *  @member transition:     Pointeres to the 3 kind of trasitions:
 *
 *  [0] Normal transition.           The next char in the word is the expected.
 *  [1] Insertion.                   The next char is inserted in the original word.
 *  [2] Deletion / Substituition.    The next char is not the one expected.
 *
 */
typedef struct _State {
    struct _State * transition[3];
    int nc;
    int ne;
} State;

typedef struct  _NFA {
    State startstate;
    State finalStates[4]; //4 states for the acceptable edit dist = 0,1,2,3.
}NFA;

typedef struct _Stack {
    State * array[MAX_SIZE];
    int size;
} Stack;

State*  CreateState (int, int);
void    CreateNFA (State *, int, int );
Stack*  AddTransition (State *, int, int, Stack *);

void init (Stack *s)
{
    s = malloc(sizeof(Stack));
    s->size = -1;
}

void push (Stack *s, State * x)
{
    s->size++;
    if (s->size == MAX_SIZE) {
        printf("Error: Stack overflow\n");fflush(NULL);
        abort();
    } else s->array[s->size] = x;
}

State* pop (Stack *s)
{
    if (s->size == -1){
        printf("Error: Stack underflow\n");fflush(NULL);
        abort();
    } else
    return s->array[s->size--];
}

int empty (Stack *s)
{
    if (s->size == -1) return 1;
    else return 0;
}


void clear (Stack *s)
{
    int i;
    for( i = s->size; i >-1; i--)
        s->array[i] = NULL;
    s->size = -1;
}

State* CreateState (int nc, int ne)
{
    State *s = malloc(sizeof(State));
    int i;
    s->nc = nc;
    s->ne = ne;
    s->transition[0] = NULL;
    s->transition[1] = NULL;
    s->transition[2] = NULL;
    return s;
}

/** Epistrefw ton prwto deikth sta transitions */
Stack* AddTransition (State * prev, int thres, int length, Stack *s)
{
    if(prev->nc == length) {
        if(prev->ne == thres)
            return s;
        else if(prev->ne < thres)   // Insertion
                push(s,prev->transition[1] = CreateState(prev->nc, prev->ne+1));
        return s;
    }
    if(prev->ne == thres && prev->nc!= length) {    // Normal Transition
         push(s,prev->transition[0] = CreateState(prev->nc+1,prev->ne));
         return s;
    }
    if(prev->ne < thres && prev->nc < length) {
         push(s,prev->transition[0] = CreateState(prev->nc+1,prev->ne));    // Normal Transition
         push(s,prev->transition[1] = CreateState(prev->nc,prev->ne+1));    // Insertion
         push(s,prev->transition[2] = CreateState(prev->nc+1,prev->ne+1));  // Insertion
         return s;
    }
}

void CreateNFA (State *start, int thres, int length)
{
    Stack *s;
    init(s);
    s = AddTransition(start,thres,length,s);
    while(!empty(s)) {
        s = AddTransition(pop(s),thres,length,s);
    }
    return;
}

int main()
{
    State* start;
    start = CreateState(0,0);
    printf("(%d, %d)\n",start->nc,start->ne);
    CreateNFA(start,2,4);

    return 0;
}
