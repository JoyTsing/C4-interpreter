#!/bin/bash
#if [ $# != 1 ];then
#    echo "Usage:$0 <filename>"
#    exit 1
#fi
#filename=$1
#if [ -f $filename ];then
    gcc  main.c
    ./a.out hello.c
    exit 0
#else
#    echo "$filename doesn't exists"
#    exit 1
#fi
