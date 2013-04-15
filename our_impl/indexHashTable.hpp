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
    IndexHashTable (unsigned _capacity, bool _keepIndexVec=false) :
        mSize(0),
        keepIndexVec(_keepIndexVec),
        capacity(_capacity)
    {
        numUnits = capacity/BITS_PER_UNIT;
        if (capacity%BITS_PER_UNIT) numUnits++;
        capacity = numUnits*BITS_PER_UNIT;
        units = (unit*) malloc (numUnits*sizeof(unit));
        for (unsigned i=0 ; i<numUnits ; i++) units[i]=0;
    }

    ~IndexHashTable () {
        free(units);
    }

    bool insert (unsigned index) {
        unsigned unit_offs = index / BITS_PER_UNIT;
        unsigned unit_bit  = index % BITS_PER_UNIT;
        unit mask = 1L << unit_bit;

        if (index<capacity) {
            if (units[unit_offs] & mask) return false;
            else {
                units[unit_offs] |= mask;
                mSize++;
                if (keepIndexVec) indexVec.push_back(index);
                return true;
            }
        }
        else {
            unsigned numUnits_old=numUnits;
            capacity = index * 2;
            numUnits = capacity/BITS_PER_UNIT;
            if (capacity%BITS_PER_UNIT) numUnits++;
            capacity = numUnits*BITS_PER_UNIT;
            units = (unit*) realloc (units, numUnits*sizeof(unit));
            for (unsigned i=numUnits_old ; i<numUnits ; i++) units[i]=0;
            units[unit_offs] |= mask;
            mSize++;
            if (keepIndexVec) indexVec.push_back(index);
            return true;
        }

    }

    bool exists (unsigned index) {
        if (index>=capacity) return false;
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
