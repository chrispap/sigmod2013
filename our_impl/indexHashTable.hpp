#ifndef INDEX_HASH_TABLE_H
#define INDEX_HASH_TABLE_H

#define BITS_PER_UNIT (sizeof(unit)*8)

using namespace std;

class IndexHashTable
{
    typedef unsigned long unit;
public:
    vector<unsigned> indexVec;

private:
    unit*               units;
    unsigned            mSize;
    bool                keepIndexVec;
    unsigned            capacity;
    unsigned            numUnits;

public:
    IndexHashTable (unsigned _capacity=HASH_SIZE, bool _keepIndexVec=false) :
        mSize(0),
        keepIndexVec(_keepIndexVec),
        capacity(_capacity)
    {
        numUnits = capacity/BITS_PER_UNIT;
        if (capacity%BITS_PER_UNIT) numUnits++;     // An to capacity den einai akeraio pollaplasio tou BITS_PER_UNIT, tote theloume allo ena unit.
        units = (unit*) malloc (numUnits*sizeof(unit));     // Allocate space with capacity bits. (NOT BYTES, BITS!)
        //~ if (units==0) {fprintf(stderr, "Could not allocate memory for IndexHashTable"); exit(-1);}
        for (unsigned i=0 ; i<numUnits ; i++) units[i]=0;
    }

    ~IndexHashTable () {
        free(units);
    }

    bool insert (unsigned index) {
        unsigned unit_offs = index / BITS_PER_UNIT;
        unsigned unit_bit  = index % BITS_PER_UNIT;
        unit mask = 1L << unit_bit;
        if (units[unit_offs] & mask) {
            return false;
        }
        else {
            units[unit_offs] |= mask;
            mSize++;
            if (keepIndexVec) indexVec.push_back(index);
            return true;
        }
    }

    bool exists (int index) {
        unsigned unit_offs = index / BITS_PER_UNIT;
        unsigned unit_bit  = index % BITS_PER_UNIT;
        unit mask = 1L << unit_bit;
        if (units[unit_offs] & mask)
            return true;
        else
            return false;
    }

    unsigned size() const {
        return mSize;
    }

    void clear () {
        for (unsigned i=0 ; i<numUnits ; i++) units[i]=0;
        indexVec.clear();
        mSize=0;
    }

};

#endif
