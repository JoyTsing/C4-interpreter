#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define int long long

int token;          // current token
char *src,*old_src; // pointer to souce code string
int poolsize;       // default size of text/data/stack
int line;           // line number

int *text,      //text segment
    *old_text,  //for dump text segment
    *stack;     //stack
char *data;     //data segment

int *pc,    //程序计数器，存放下一条指令
    *bp,    //基址指针，调用函数时使用
    *sp,    //指针寄存器，总指向当前栈顶，高地址向地址增长
    ax,     //通用寄存器
    cycle;  //

/*
 *  +------------------+
 *  |    stack   |     |      high address
 *  |    ...     v     |
 *  |                  |
 *  |                  |
 *  |                  |
 *  |                  |
 *  |    ...     ^     |
 *  |    heap    |     |
 *  +------------------+
 *  | bss  segment     |
 *  +------------------+
 *  | data segment     |
 *  +------------------+
 *  | text segment     |      low address
 *  +------------------+
 * */
// instructions
// 为了简化Mov指令在这里被拆成了5条单参数指令
// IMM <num>是将num放入ax
// LC 将对应地址中的字符载入ax中，ax中存地址
// LI 将对应地址中的整数载入ax中，ax中存地址
// SC将ax中的数据作为字符存放入地址中，要求栈顶存地址
// SI将ax中的数据作为整数存放入地址中，要求栈顶存地址
enum{
    LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,
    ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,
    OR  ,XOR ,AND ,
    EQ  ,NE  ,LT  ,GT  ,LE  ,GE,
    SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
    OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT,RET
};


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

/*
 * 函数调用时栈中的调用帧
 *
 sub_function(arg1, arg2, arg3);

 |    ....       | high address
 +---------------+
 | arg: 1        |    new_bp + 4
 +---------------+
 | arg: 2        |    new_bp + 3
 +---------------+
 | arg: 3        |    new_bp + 2
 +---------------+
 |return address |    new_bp + 1
 +---------------+
 | old BP        | <- new BP
 +---------------+
 | local var 1   |    new_bp - 1
 +---------------+
 | local var 2   |    new_bp - 2
 +---------------+
 |    ....       |  low address
 +---------------+
 *
 */


//虚拟机入口，用来解释目标代码
int eval(){
    int op,*tmp;
    while(1){
        op=*pc++;

        if(op==IMM)     {ax=*pc++;}
        else if(op==LC) {ax=*(char*)ax;}
        else if(op==LI) {ax=*(int*)ax;}
        else if(op==SC) {ax=*(char*)*sp++=ax;}
        else if(op==SI) {*(int*)*sp++=ax;}
        else if(op==PUSH){*--sp=ax;}
        else if(op==JMP){pc=(int*)*pc;}
        else if(op==JZ) {pc= ax? pc+1 : (int*)*pc;}//if ax is zero
        else if(op==JNZ){pc= ax? (int*)*pc : pc+1;}//if ax is not zero
        //CALL跳到地址为<addr>的子函数，RET用来返回
        //这里是一个子过程
        else if(op==CALL){*--sp=(int)(pc+1);pc=(int*)*pc;}
        else if(op==RET){pc=(int*)*sp++;}//可以用LEV代替
        //ENT用实现函数调用前保存的功能,并预留局部变量的位置
        /*
         * 对应汇编的
         *;make new call frame
         * push ebp
         * mov ebp,esp
         * sub 1,esp    ;save stack for variable i
         * */
        else if(op==ENT){*--sp=(int)bp;bp=sp;sp=sp-*pc++;}
        //ADJ调用子函数时将压入栈中的数据清除
        else if(op==ADJ){sp=sp+*pc++;}
        //LEV实现恢复的过程
        /*
         *;restore old call frame
         * mov esp,ebp
         * pop ebp
         * ret
         * */
        else if(op==LEV){sp=bp;bp=(int*)*sp++;pc=(int*)*sp++;}
        else if(op==LEA){ax=(int)(bp+*pc++);}

        //计算的参数第一个存在栈顶，第二个存在ax中
        else if (op == OR)  ax = *sp++ | ax;
        else if (op == XOR) ax = *sp++ ^ ax;
        else if (op == AND) ax = *sp++ & ax;
        else if (op == EQ)  ax = *sp++ == ax;
        else if (op == NE)  ax = *sp++ != ax;
        else if (op == LT)  ax = *sp++ < ax;
        else if (op == LE)  ax = *sp++ <= ax;
        else if (op == GT)  ax = *sp++ >  ax;
        else if (op == GE)  ax = *sp++ >= ax;
        else if (op == SHL) ax = *sp++ << ax;
        else if (op == SHR) ax = *sp++ >> ax;
        else if (op == ADD) ax = *sp++ + ax;
        else if (op == SUB) ax = *sp++ - ax;
        else if (op == MUL) ax = *sp++ * ax;
        else if (op == DIV) ax = *sp++ / ax;
        else if (op == MOD) ax = *sp++ % ax;

        //builtin
        else if (op == EXIT) { printf("exit(%d)\n", *sp); return *sp; }
        else if (op == OPEN) { ax = open((char *)sp[1], sp[0]);  }
        else if (op == CLOS) { ax = close(*sp); }
        else if (op == READ) { ax = read(sp[2], (char *)sp[1], *sp);  }
        else if (op == PRTF) { tmp = sp + pc[1]; ax = printf((char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]);  }
        else if (op == MALC) { ax = (int)malloc(*sp); }
        else if (op == MSET) { ax = (int)memset((char *)sp[2], sp[1], *sp); }
        else if (op == MCMP) { ax = memcmp((char *)sp[2], (char *)sp[1], *sp); }
        else{
            printf("unknown instruction:%d\n",op);
            return -1;
        }
    }
    return 0;
}

#undef int
int main(int argc,char *argv[])
{
    #define int long long
    int i,fd;

    argc--;
    argv++;
    poolsize=256*1024;

    line=1;

    if((fd=open(*argv,0))<0){
        printf("could not open(%s)\n",*argv);
        return -1;
    }

    if(!(src=old_src=(char*)malloc(poolsize))){
        printf("could not malloc(%d) for source area\n",poolsize);
        return -1;
    }

    if((i=read(fd,src,poolsize-1))<=0){
        printf("read() returned %d\n",i);
        return -1;
    }
    src[i]=0;
    close(fd);

    if(!(text=old_text=(int*)malloc(poolsize))){
        printf("could not malloc(%d) for text area\n",poolsize);
        return -1;
    }

    if(!(data=(char*)malloc(poolsize))){
        printf("could not malloc(%d) for data area\n",poolsize);
        return -1;
    }

    if(!(stack=(int*)malloc(poolsize))){
        printf("could not malloc(%d) for stack area\n",poolsize);
        return -1;
    }

    memset(text,0,poolsize);
    memset(data,0,poolsize);
    memset(stack,0,poolsize);

    bp=sp=(int*)((int)stack+poolsize);//(int*)((int)p+offset);
    ax=0;

    //Test
    i = 0;
    text[i++] = IMM;
    text[i++] = 10;
    text[i++] = PUSH;
    text[i++] = IMM;
    text[i++] = 20;
    text[i++] = ADD;
    text[i++] = PUSH;
    text[i++] = EXIT;
    pc = text;
    //
    program();
    return eval();
}

