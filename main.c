#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
int token;          // current token
char *src,*old_src; // pointer to souce code string
int poolsize;       // default size of text/data/stack
int line;           // line number

//用于词法分析，获取下一个标记
void next(){
    token=*src++;
    return;
}

//解析表达式
void expression(int level){

}

//词法分析的入口
void program(){
    next();
    while(token>0){
        printf("token is :%c\n",token);
        next();
    }
}

//虚拟机入口，用来解释目标代码
int eval(){
    return 0;
}

int main(int argc,char *argv[])
{
    int i,fd;

    argc--;
    argv++;
    poolsize=256*1024;

    line=1;

    if((fd=open(*argv,0))<0){
        printf("could not open(%s)\n",*argv);
        return -1;
    }

    if(!(src=old_src=(char*)malloc(poolsize))<=0){
        printf("could not malloc(%d) for source area\n",poolsize);
        return -1;
    }

    if((i=read(fd,src,poolsize-1))<=0){
        printf("read() returned %d\n",i);
        return -1;
    }
    src[i]=0;
    close(fd);

    program();
    return eval();
}

