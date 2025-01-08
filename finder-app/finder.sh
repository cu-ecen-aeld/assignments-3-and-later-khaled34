#!/bin/bash 

filesdir=$1
searchstr=$2

if (( $# != 2 ))
then 
    echo "Improper usage of the finder script:"
    echo "finder.sh <Valid Path To Search Into>  <String To Search of>"
    exit 1
fi
if [ -d ${filesdir} -a -n ${searchstr} ]  
then 
    cd ${filesdir}
    NumberOfFiles=`grep -rl ${searchstr} | wc -l`
    NumberOfLines=`grep -r ${searchstr} | wc -l`

    echo "The number of files are ${NumberOfFiles} and the number of matching lines are ${NumberOfLines}"
else
    echo "Improper usage of finder whether the path is incorrrect or the search pattern is empty"
    exit 1
fi