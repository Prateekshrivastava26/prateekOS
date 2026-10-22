#include "calculator.h"
#include <stdint.h>
#define SCALE 10000LL

typedef struct {const char *p;const char *error;unsigned depth;} parser_t;
static void copy(char *out,const char *s){size_t i=0;while(s[i]&&i<63){out[i]=s[i];i++;}out[i]=0;}
static void spaces(parser_t *p){while(*p->p==' ')p->p++;}
static int64_t expression(parser_t *p);
static int64_t factor(parser_t *p){
    spaces(p);if(p->error)return 0;
    if(++p->depth>32){p->error="Expression too deep";p->depth--;return 0;}
    int negative=0;while(*p->p=='+'||*p->p=='-'){if(*p->p++=='-')negative=!negative;spaces(p);}
    int64_t value=0;
    if(*p->p=='('){p->p++;value=expression(p);spaces(p);if(*p->p!=')')p->error="Missing closing bracket";else p->p++;}
    else{
        unsigned digits=0;
        while(*p->p>='0'&&*p->p<='9'){
            int d=*p->p++-'0';digits++;
            if(__builtin_mul_overflow(value,10LL,&value)||__builtin_add_overflow(value,(int64_t)d,&value)){p->error="Number too large";break;}
        }
        if(!p->error && __builtin_mul_overflow(value,SCALE,&value))p->error="Number too large";
        if(*p->p=='.'&&!p->error){p->p++;int64_t place=1000;
            while(*p->p>='0'&&*p->p<='9'){
                int d=*p->p++-'0';digits++;
                if(!place){p->error="Maximum 4 decimal places";break;}
                if(__builtin_add_overflow(value,d*place,&value))p->error="Number too large";
                place/=10;
            }
        }
        if(!digits&&!p->error)p->error="Enter a number";
    }
    if(negative&&__builtin_sub_overflow(0LL,value,&value))p->error="Number too large";
    p->depth--;return value;
}
static int64_t term(parser_t *p){
    int64_t a=factor(p);spaces(p);
    while(!p->error&&(*p->p=='*'||*p->p=='/')){
        char op=*p->p++;int64_t b=factor(p),raw;
        if(p->error)break;
        if(op=='*'){
            if(__builtin_mul_overflow(a,b,&raw)){p->error="Number too large";break;}
            a=raw/SCALE;
        }else{
            if(!b){p->error="Cannot divide by zero";break;}
            if(__builtin_mul_overflow(a,SCALE,&raw)||(raw==INT64_MIN&&b==-1)){p->error="Number too large";break;}
            a=raw/b;
        }
        spaces(p);
    }return a;
}
static int64_t expression(parser_t *p){
    int64_t a=term(p);spaces(p);
    while(!p->error&&(*p->p=='+'||*p->p=='-')){
        char op=*p->p++;int64_t b=term(p);if(p->error)break;
        if(op=='+'?__builtin_add_overflow(a,b,&a):__builtin_sub_overflow(a,b,&a)){p->error="Number too large";break;}
        spaces(p);
    }return a;
}
static void format(int64_t v,char out[64]){
    size_t n=0;uint64_t magnitude=v<0?(uint64_t)(-(v+1))+1:(uint64_t)v;
    if(v<0)out[n++]='-';
    uint64_t whole=magnitude/SCALE;char reverse[20];size_t k=0;
    do{reverse[k++]='0'+whole%10;whole/=10;}while(whole);
    while(k)out[n++]=reverse[--k];
    uint64_t fraction=magnitude%SCALE;
    if(fraction){out[n++]='.';uint64_t place=1000;while(place){out[n++]='0'+fraction/place;fraction%=place;place/=10;}while(out[n-1]=='0')n--;}
    out[n]=0;
}
int calculator_evaluate(const char *input,char out[64]){
    if(!input||!out)return 0;
    parser_t p={input,0,0};int64_t value=expression(&p);spaces(&p);
    if(!p.error&&*p.p)p.error="Invalid expression";
    if(p.error){copy(out,p.error);return 0;}format(value,out);return 1;
}
void calculator_init(calculator_t *c){c->input[0]=0;copy(c->result,"0");c->length=0;c->evaluated=0;}
void calculator_key(calculator_t *c,char key){
    if(key=='c'||key=='C'){calculator_init(c);return;}
    if(key=='='||key=='\n'){c->evaluated=calculator_evaluate(c->input,c->result)?1:-1;return;}
    if(key=='\b'){if(c->length)c->input[--c->length]=0;c->evaluated=0;return;}
    if(!((key>='0'&&key<='9')||key=='.'||key=='+'||key=='-'||key=='*'||key=='/'||key=='('||key==')'))return;
    if(c->evaluated){
        if(c->evaluated==1&&(key=='+'||key=='-'||key=='*'||key=='/')){copy(c->input,c->result);c->length=0;while(c->input[c->length])c->length++;}
        else{c->input[0]=0;c->length=0;}
        c->evaluated=0;
    }
    if(c->length+1<sizeof c->input){c->input[c->length++]=key;c->input[c->length]=0;}
}
