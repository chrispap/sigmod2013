#~/bin/bash

L=1
while :
do
    ./testdriver test_data/big_test.txt 2> err.txt
    sort err.txt > doc6562.txt
    diff doc6562.txt doc6562_sorted_unique.txt > diff.txt

    if [ $? -ne 0 ]
    then
        echo "Problem in loop" $L ":("
        break
    fi

    echo "Loop" $L
    L=$[L+1]
done
