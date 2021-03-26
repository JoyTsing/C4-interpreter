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

int token_val;  //current token
int *current_id,//current parsed ID
    *symbols;   //symbol table

int basetype;   //type of a global_declaration
int expr_type;  //type of an expression

enum{Token,Hash,Name,Type,Class,Value,BType,BClass,BValue,IdSize};

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

//type of variable/function
enum{
    CHAR,INT,PTR
};


/**
 program ::= {global_declaration}+

 global_declaration ::= enum_decl | variable_decl | function_decl

 enum_decl ::= 'enum' [id] '{' id ['=' 'num'] {',' id ['=' 'num'] '}'

 variable_decl ::= type {'*'} id { ',' {'*'} id  } ';'

 function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'

 parameter_decl ::= type {'*'} id {',' type {'*'} id}

 body_decl ::= {variable_decl}, {statement}

 statement ::= non_empty_statement | empty_statement

 non_empty_statement ::= if_statement | while_statement | '{' statement '}'
                      | 'return' expression | expression ';'

 if_statement ::= 'if' '(' expression ')' statement ['else' non_empty_statement]

 while_statement ::= 'while' '(' expression ')' non_empty_statement}'}'
 */
//词法分析的标记
enum{
    Num = 128, Fun, Sys, Glo, Loc, Id,
    Char, Else, Enum, If, Int, Return, Sizeof, While,
    Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};

struct identifier{
    int token;//标识符返回的标记
    int hash;//标识符的哈希值,用于标识符的快速比较
    char *name;//存放标识符自身的字符串
    int class;//标识符的列表,数字还是全局变量还是局部变量
    int type;//标识符的类型
    int value;//值
    int Bclass;//局部还是全局
    int Btype;
    int Bvalue;
};

//用于词法分析，获取下一个标记
void next(){
    char *last_pos;
    int hash;

    while(token=*src){
        ++src;

        //parse token here
        if(token=='\n'){
            ++line;
        }else if(token=='#'){
            //skip macro
            while(*src!=0&&*src!='\n'){
                src++;
            }
        }else if((token>='a'&&token<='z')||(token>='A'&&token<='Z')||(token=='_')){
            //parse identifier
            last_pos=src-1;
            hash=token;
            while((*src>='a'&&*src<='z')||(*src>='A'&&*src<='Z')||(*src=='_')){
                hash=hash*147+*src;
                    src++;
            }

            //look for existing identifier
            current_id=symbols;
            while(current_id[Token]){
                if(current_id[Hash]==hash&&!memcmp((char*)current_id[Name],last_pos,src-last_pos)){
                   //found
                    token=current_id[Token];
                   return;
                }
                current_id=current_id+IdSize;
            }

            //store new ID
            current_id[Name]=(int)last_pos;
            current_id[Hash]=hash;
            token=current_id[Token]=Id;
            return ;
        }else if(token>= '0' && token<='9'){
            token_val=token-'0';
            if(token_val>0){
                //dec
                while(*src>='0'&&*src<='9'){
                    token_val=token_val*10+*src++-'9';
                }
            }else{
                if(*src=='x'|*src=='X'){
                    //hex
                    token=*++src;
                    while((token>='0'&&token<='9')||(token>='a'&&token<='f')||(token>='A'&&token<='F')){
                        token_val=token_val*16+(token&15)+(token>='A'?9:0);
                        token=*++src;
                    }
                }else{
                    //oct
                    while(*src>='0'&&*src<='7'){
                        token_val=token_val*8+*src++-'0';
                    }
                }
            }
            token=Num;
            return;
        }else if(token =='"'||token=='\''){
            last_pos=data;
            while(*src!=0&&*src!=token){
                token_val=*src++;
                if(token_val=='\\'){
                    token_val=*src++;
                    if(token_val=='n'){
                        token_val='\n';
                    }
                }

                if(token=='"'){
                    *data++=token_val;
                }
            }
            src++;
            if(token=='"'){
                token_val=(int)last_pos;
            }else{
                token=Num;
            }
            return;
        }else if(token=='/'){
            if(*src=='/'){
                while(*src!=0&&*src!='\n'){
                    ++src;
                }
            }else{
                token=Div;
                return;
            }
        }else if(token=='='){
            if(*src=='='){
                src++;
                token=Eq;
            }else{
                token=Assign;
            }
            return;
        }else if(token=='+'){
            if(*src=='+'){
                src++;
                token=Inc;
            }else{
                token=Add;
            }
            return;
        }else if(token=='-'){
            if (*src == '-') {
                src ++;
                token = Dec;
            }else {
                token = Sub;
            }
                return;
        }else if (token == '!') {
             // parse '!='
            if (*src == '=') {
                src++;
                token = Ne;
            }
            return ;
        } else if (token == '<') {
            // parse '<=', '<<' or '<'
            if (*src == '=') {
                src ++;
                token = Le;
            }else if (*src == '<') {
                src ++;
                token = Shl;
            }else {
                token = Lt;
            }
            return;
        }else if(token=='>'){
            if (*src == '=') {
                src ++;
                token = Ge;
            }else if (*src == '>') {
                src ++;
                token = Shr;
            }else {
                token = Gt;
            }
            return;
        }else if(token=='|'){
            if (*src == '|') {
                src ++;
                token = Lor;
            }else {
                token = Or;
            }
            return;
        }else if(token == '&'){
            if (*src == '&') {
                 src ++;
                  token = Lan;
            }else {
                token = And;
            }
            return;
        }else if (token == '^') {
            token = Xor;
            return;
        }else if (token == '%') {
            token = Mod;
            return;
        }else if (token == '*') {
            token = Mul;
            return;
        }else if (token == '[') {
            token = Brak;
            return;
        } else if (token == '?') {
            token = Cond;
            return;
        }else if(token=='~'||token==';'||token=='{'||token=='}'){
            return;
        }
    }
    return;
}


//解析表达式
void expression(int level){

}

void match(int tk){
    if(token==tk){
        next();
    }else{
        printf("%d: expected token: %d\n",line,tk);
        exit(-1);
    }
}

/*
 |    ....       | high address
 +---------------+
 | arg: param_a  |    new_bp + 3
 +---------------+
 | arg: param_b  |    new_bp + 2
 +---------------+
 |return address |    new_bp + 1
 +---------------+
 | old BP        | <- new BP
 +---------------+
 | local_1       |    new_bp - 1
 +---------------+
 | local_2       |    new_bp - 2
 +---------------+
 |    ....       |  low address
 */
int index_of_bp;//new_bp

void function_parameter(){
    int type;
    int params=0;
    while(token!=')'){
        type=INT;
        if(token==Int){
            match(Int);
        }else if(token=Char){
            type=CHAR;
            match(Char);
        }

        while(token==Mul){
            match(Mul);
            type=type+PTR;
        }
        if(token!=Id){
            printf("%d: bad parameter declartion\n",line);
            exit(-1);
        }
        if(current_id[Class]==Loc){
            printf("%d: duplicate paramter declartion\n",line);
            exit(-1);
        }

        match(Id);

        //
        current_id[BClass]=current_id[Class];current_id[Class]=Loc;
        current_id[BType]=current_id[Type];current_id[Type]=type;
        current_id[BType]=current_id[Value];current_id[Value]=params++;

        if(token==','){
            match(',');
        }
    }
    index_of_bp=params+1;
}

void function_body(){
    //type func_name(....){....}
    //                 -->|    |<--
    //

    int pos_local;
    int type;
    pos_local=index_of_bp;

    while(token==Int||token==Char){
        basetype=(token==Int)?INT:CHAR;
        match(token);

        while(token!=';'){
            type=basetype;
            while(token==Mul){
                match(Mul);
                type=type+PTR;
            }

            if(token!=Id){
                printf("%d: bad local declartion\n",line);
                exit(-1);
            }

            if(current_id[Class]==Loc){
                printf("%d: duplicate local declartion\n",line);
                exit(-1);
            }

            match(Id);

            current_id[BClass]=current_id[Class];current_id[Class]=Loc;
            current_id[BType]=current_id[Type];current_id[Type]=type;
            current_id[BType]=current_id[Value];current_id[Value]=++pos_local;

            if(token==','){
                match(',');
            }
        }
        match(';');

    }

    //生成汇编代码
    *++text=ENT;
    *++text=pos_local-index_of_bp;

    while(token!='}'){
        statement();
    }
    *++text=LEV;

}

void function_declartion(){
    //type func_name (....){....}
    //              |  this
    match('(');
    function_parameter();
    match(')');
    match('{');
    function_body();

    //将符号表中的信息恢复成全局的信息
    current_id=symbols;
    while(current_id[Token]){
        if(current_id[Class]==Loc){
            current_id[Class]=current_id[BClass];
            current_id[Type]=current_id[BType];
            current_id[Value]=current_id[BValue];
        }
        current_id=current_id+IdSize;
    }
}

void enum_declartion(){
    //enum [id] {a=1,b=3};
    int i;
    i=0;
    while(token!='}'){
        if(token!=Id){
            printf("%d: bad enum identifier %d\n",line,token);
            exit(-1);
        }
        next();
        if(token==Assign){
            next();
            if(token!=Num){
                printf("%d: bad enum initializer\n",line);
                exit(-1);
            }
            i=token_val;
            next();
        }

        current_id[Class]=Num;
        current_id[Type]=INT;
        current_id[Value]=i++;

        if(token==','){
            next();
        }
    }
}

void global_declaration(){

// global_declaration ::= enum_decl | variable_decl | function_decl
//
// enum_decl ::= 'enum' [id] '{' id ['=' 'num'] {',' id ['=' 'num'} '}']}'}'
//
// variable_decl ::= type {'*'} id { ',' {'*'} id  } ';'
//
// function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'
    int type;
    int i;

    basetype=INT;

    if(token==Enum){
        match(Enum);
        if(token!='{'){
            match(Id);
        }else{
            match('{');
            enum_declartion();
            match('}');
        }
        match(';');
        return;
    }

    if(token==Int){
        match(Int);
    }else if(token==Char){
        match(Char);
        basetype=CHAR;
    }

    //逗号分割变量
    while(token!=';'&&token!='}'){
        type=basetype;
        //pointer type. int ********x
        while(token==Mul){
            match(Mul);
            type=type+PTR;
        }
        if(token!=Id){
            printf("%d: bad global declartion\n",line);
            exit(-1);
        }
        if(current_id[Class]){
            printf("%d: duplicate global declartion\n",line);
            exit(-1);
        }
        match(Id);
        current_id[Type]=type;

        if(token=='('){
            current_id[Class]=Fun;
            current_id[Value]=(int)(text+1);
            function_declartion();
        }else{
            current_id[Class]=Glo;
            current_id[Value]=(int)data;
            data=data+sizeof(int);
        }

        if(token==','){
            match(',');
        }
    }
    next();
}

//词法分析的入口
void program(){
    next();
    while(token>0){
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

enum{CHAR,INT,PTR};
int *idmain;

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

    if(!(symbols=(int*)malloc(poolsize))){
        printf("could not malloc(%d) for symbol table\n",poolsize);
    }

    memset(text,0,poolsize);
    memset(data,0,poolsize);
    memset(stack,0,poolsize);
    memset(symbols,0,poolsize);

    bp=sp=(int*)((int)stack+poolsize);//(int*)((int)p+offset);
    ax=0;

    src="char else enum if int sizeof while "
        "open read close printf malloc memset memcmp exit void main";
    i=Char;
    while(i<=While){
        next();
        current_id[Token]=i++;
    }
    i=OPEN;
    while(i<=EXIT){
        next();
        current_id[Class]=Sys;
        current_id[Type]=INT;
        current_id[Value]=i++;
    }
    next();current_id[Token]=Char;
    next();idmain=current_id;

    if((fd=open(*argv,0))<0){
        printf("could not open(%s)\n",*argv);
        return -1;
    }

    if(!(src=old_src=(char*)malloc(poolsize))){
        printf("could not malloc(%d) for source area\n",poolsize);
        return -1;
    }

    if((i=read(fd,src,poolsize-1))<=0){
        printf("read() return %d\n",i);
        return -1;
    }


    src[i]=0;
    close(fd);

    program();
    return eval();
}

