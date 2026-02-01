#! /bin/sh
firstGuess=$1
if [ "$firstGuess" = "" ]
then
    firstGuess="raise"
fi
args=
while 1
do
    guess="$(./wordler.exe --no-verbose --init=$firstGuess $args)"
    if [ $? -ne 0 ]
    then
        echo ERROR
        break
    fi
    echo "Guess: $guess"
    echo -e "Hint:  \c"
    read hint
    if [ "$hint" = "" ]
    then
        break
    fi
    args="$args $guess $hint"
done
