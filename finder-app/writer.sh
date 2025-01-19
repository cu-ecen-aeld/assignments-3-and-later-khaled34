#!/bin/sh 

writefile=$1
writestr=$2

if [ $# != 2 ]
then 
    echo "Improper usage of the writer script:"
    echo "writer.sh <Valid Path For a File>  <String to Write in the File>"
    exit 1
fi

#check the passed string first 
if [ -z ${writestr} -o -z ${writefile} ]
then 
    echo "No string passed for one of the passed parameters"
    echo "writer.sh <Valid Path For a File>  <String to Write in the File>"
    exit 1
fi


DIRECTORY_PATH=`dirname ${writefile}`
FILE_NAME=`basename ${writefile}`
# if the directory path isn't exist then allocate the directory 
if [ ! -d ${DIRECTORY_PATH} ]
then 
    mkdir -p ${DIRECTORY_PATH} 
fi

#if the file isn't exist touch it 
if [ ! -f ${FILE_NAME} ]
then 
    touch ${FILE_NAME} 
fi

#echo the string passed to the file 
echo ${writestr} > ${writefile}