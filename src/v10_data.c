#include "qn_v10_data.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum { T_EOF=0,T_NL,T_LET,T_IDENT,T_COLON,T_EQ,T_NUM,T_STR,T_BYTES } TK;
typedef struct { TK k; int line,col; char text[QN_NAME_CAP]; float f; uint8_t *p; uint32_t n; } Tok;
typedef struct { const char *s; size_t i; int line,col; Tok hold; bool has_hold; QNDiagnostic *d; } Lex;

static void de(QNDiagnostic *d,const char *c,int l,int p,const char *m){
    if(!d) return;
    memset(d,0,sizeof(*d)); d->line=l; d->column=p;
    if(c)snprintf(d->code,sizeof(d->code),"%s",c);
    if(m)snprintf(d->message,sizeof(d->message),"%s",m);
}
static void tf(Tok *t){ if(t){free(t->p); memset(t,0,sizeof(*t));} }
static int hx(unsigned char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static bool ab(uint8_t **p,uint32_t *n,uint32_t *cap,uint8_t v,uint32_t lim,QNDiagnostic*d,int l,int c){
    if(*n>=lim){de(d,"QN-E7811",l,c,"V10 literal exceeds bounded byte limit");return false;}
    if(*n==*cap){uint32_t nc=*cap?*cap*2u:64u;if(nc>lim)nc=lim;if(nc<=*cap){de(d,"QN-E7811",l,c,"V10 literal capacity overflow");return false;}uint8_t*q=realloc(*p,nc);if(!q){de(d,"QN-E7811",l,c,"out of memory reading V10 literal");return false;}*p=q;*cap=nc;}
    (*p)[(*n)++]=v; return true;
}
static bool quoted(Lex*l,Tok*t,bool bytes){
    int sc=l->col; if(bytes){l->i++;l->col++;} if(l->s[l->i]!='"')return false; l->i++;l->col++;
    uint8_t*p=NULL;uint32_t n=0,cap=0,lim=bytes?QN_MAX_BYTES_BUFFER:QN_MAX_STRING_BYTES;
    while(l->s[l->i]&&l->s[l->i]!='"'){
        unsigned char c=(unsigned char)l->s[l->i]; if(c=='\n'||c=='\r'){free(p);de(l->d,"QN-E7811",t->line,sc,"newline inside V10 quoted literal");return false;}
        if(c=='\\'){
            l->i++;l->col++; unsigned char e=(unsigned char)l->s[l->i]; uint8_t v=0;
            if(!e){free(p);de(l->d,"QN-E7811",t->line,sc,"unterminated V10 escape");return false;}
            if(e=='n')v='\n';else if(e=='r')v='\r';else if(e=='t')v='\t';else if(e=='\\')v='\\';else if(e=='"')v='"';
            else if(e=='x'&&bytes){int a=hx((unsigned char)l->s[l->i+1]),b=hx((unsigned char)l->s[l->i+2]);if(a<0||b<0){free(p);de(l->d,"QN-E7811",t->line,l->col,"bytes \\x escape requires two hex digits");return false;}v=(uint8_t)((a<<4)|b);l->i+=3;l->col+=3;if(!ab(&p,&n,&cap,v,lim,l->d,t->line,sc)){free(p);return false;}continue;}
            else{free(p);de(l->d,"QN-E7811",t->line,l->col,"unsupported V10 literal escape");return false;}
            l->i++;l->col++; if(!ab(&p,&n,&cap,v,lim,l->d,t->line,sc)){free(p);return false;} continue;
        }
        l->i++;l->col++; if(!ab(&p,&n,&cap,c,lim,l->d,t->line,sc)){free(p);return false;}
    }
    if(l->s[l->i]!='"'){free(p);de(l->d,"QN-E7811",t->line,sc,"unterminated V10 quoted literal");return false;}
    l->i++;l->col++; t->k=bytes?T_BYTES:T_STR;t->p=p;t->n=n;return true;
}
static QNStatus next0(Lex*l,Tok*t){
    memset(t,0,sizeof(*t));
    for(;;){unsigned char c=(unsigned char)l->s[l->i];
        if(!c){t->k=T_EOF;t->line=l->line;t->col=l->col;return QN_OK;}
        if(c==' '||c=='\t'||c=='\r'){l->i++;l->col++;continue;}
        if(c=='#'||(c=='/'&&l->s[l->i+1]=='/')){if(c=='/'){l->i+=2;l->col+=2;}while(l->s[l->i]&&l->s[l->i]!='\n'){l->i++;l->col++;}continue;}
        break;
    }
    unsigned char c=(unsigned char)l->s[l->i];t->line=l->line;t->col=l->col;
    if(c=='\n'||c==';'){t->k=T_NL;l->i++;if(c=='\n'){l->line++;l->col=1;}else l->col++;return QN_OK;}
    if(c==':'){t->k=T_COLON;l->i++;l->col++;return QN_OK;} if(c=='='){t->k=T_EQ;l->i++;l->col++;return QN_OK;}
    if(c=='"') return quoted(l,t,false)?QN_OK:QN_ERR_LEX;
    if(c=='b'&&l->s[l->i+1]=='"') return quoted(l,t,true)?QN_OK:QN_ERR_LEX;
    if(isdigit(c)||(c=='-'&&isdigit((unsigned char)l->s[l->i+1]))){
        size_t st=l->i;int sc=l->col;if(l->s[l->i]=='-'){l->i++;l->col++;}while(isdigit((unsigned char)l->s[l->i])){l->i++;l->col++;}
        if(l->s[l->i]=='.'){l->i++;l->col++;if(!isdigit((unsigned char)l->s[l->i])){de(l->d,"QN-E7811",t->line,sc,"f32 decimal point requires digits");return QN_ERR_LEX;}while(isdigit((unsigned char)l->s[l->i])){l->i++;l->col++;}}
        if(l->s[l->i]=='e'||l->s[l->i]=='E'){l->i++;l->col++;if(l->s[l->i]=='+'||l->s[l->i]=='-'){l->i++;l->col++;}if(!isdigit((unsigned char)l->s[l->i])){de(l->d,"QN-E7811",t->line,sc,"f32 exponent requires digits");return QN_ERR_LEX;}while(isdigit((unsigned char)l->s[l->i])){l->i++;l->col++;}}
        size_t n=l->i-st;if(n>=128){de(l->d,"QN-E7811",t->line,sc,"f32 literal too long");return QN_ERR_LEX;}char b[128];memcpy(b,l->s+st,n);b[n]=0;errno=0;char*e=NULL;float v=strtof(b,&e);if(errno==ERANGE||!e||*e||!isfinite(v)){de(l->d,"QN-E7811",t->line,sc,"invalid or non-finite f32 literal");return QN_ERR_LEX;}t->k=T_NUM;t->f=v;return QN_OK;
    }
    if(isalpha(c)||c=='_'){size_t st=l->i;while(isalnum((unsigned char)l->s[l->i])||l->s[l->i]=='_'){l->i++;l->col++;}size_t n=l->i-st;if(n>=sizeof(t->text)){de(l->d,"QN-E7811",t->line,t->col,"V10 identifier too long");return QN_ERR_LEX;}for(size_t j=0;j<n;j++)t->text[j]=(char)tolower((unsigned char)l->s[st+j]);t->text[n]=0;t->k=!strcmp(t->text,"let")?T_LET:T_IDENT;return QN_OK;}
    de(l->d,"QN-E7811",t->line,t->col,"unexpected character in V10 native-data source");return QN_ERR_LEX;
}
static QNStatus nx(Lex*l,Tok*t){if(l->has_hold){*t=l->hold;memset(&l->hold,0,sizeof(l->hold));l->has_hold=false;return QN_OK;}return next0(l,t);} 
static QNStatus ex(Lex*l,TK k,Tok*t,const char*m){QNStatus s=nx(l,t);if(s!=QN_OK)return s;if(t->k!=k){de(l->d,"QN-E7812",t->line,t->col,m);tf(t);return QN_ERR_PARSE;}return QN_OK;}
static bool exists(const QNV10DataProgram*p,const char*n){for(uint16_t i=0;i<p->count;i++)if(!strcmp(p->declarations[i].name,n))return true;return false;}
static QNStatus vd(QNV10DataDecl*d,QNDiagnostic*x){if(d->kind==QN_VALUE_F32)return qn_f32_validate(d->as.f32,x);if(d->kind==QN_VALUE_STRING){QNStringView v={(const char*)d->as.blob.data,d->as.blob.byte_length};return qn_string_validate(v,QN_MAX_STRING_BYTES,true,x);}if(d->kind==QN_VALUE_BYTES){QNBytesView v={d->as.blob.data,d->as.blob.byte_length};return qn_bytes_validate(v,QN_MAX_BYTES_BUFFER,x);}de(x,"QN-E7815",d->line,d->column,"unsupported V10 AST kind");return QN_ERR_SEMANTIC;}

QNStatus qn_v10_data_parse_source(const char *source,QNV10DataProgram*out,QNDiagnostic*diag){
    if(!source||!out||!diag) return QN_ERR_RUNTIME;
    memset(out,0,sizeof(*out)); size_t z=strlen(source);
    if(z>QN_MAX_SOURCE_BYTES){de(diag,"QN-E7810",1,1,"V10 source exceeds source byte limit");return QN_ERR_LIMIT;}
    Lex l={.s=source,.line=1,.col=1,.d=diag};QNStatus st=QN_OK;Tok a={0},n={0},c={0},ty={0},eq={0},v={0},end={0};
    for(;;){st=nx(&l,&a);if(st!=QN_OK)goto fail;while(a.k==T_NL){tf(&a);st=nx(&l,&a);if(st!=QN_OK)goto fail;}if(a.k==T_EOF){tf(&a);break;}if(a.k!=T_LET){de(diag,"QN-E7812",a.line,a.col,"expected 'let' in V10 declaration");st=QN_ERR_PARSE;goto fail;}
        if(out->count>=QN_V10_MAX_DECLS){de(diag,"QN-E7812",a.line,a.col,"V10 declaration limit exceeded");st=QN_ERR_LIMIT;goto fail;}
        if((st=ex(&l,T_IDENT,&n,"expected V10 declaration name"))!=QN_OK) goto fail;
        if((st=ex(&l,T_COLON,&c,"expected ':' after V10 declaration name"))!=QN_OK) goto fail;
        if((st=ex(&l,T_IDENT,&ty,"expected V10 native-data type"))!=QN_OK) goto fail;
        if((st=ex(&l,T_EQ,&eq,"expected '=' before V10 literal"))!=QN_OK) goto fail;
        if((st=nx(&l,&v))!=QN_OK) goto fail;
        if(exists(out,n.text)){de(diag,"QN-E7813",n.line,n.col,"duplicate V10 native-data declaration");st=QN_ERR_SEMANTIC;goto fail;}
        QNV10DataDecl*d=&out->declarations[out->count];memset(d,0,sizeof(*d));d->line=a.line;d->column=a.col;snprintf(d->name,sizeof(d->name),"%s",n.text);
        if(!strcmp(ty.text,"f32")){if(v.k!=T_NUM){de(diag,"QN-E7814",v.line,v.col,"f32 requires an f32 literal");st=QN_ERR_PARSE;goto fail;}d->kind=QN_VALUE_F32;d->as.f32=v.f;}
        else if(!strcmp(ty.text,"string")){if(v.k!=T_STR){de(diag,"QN-E7814",v.line,v.col,"string requires a quoted UTF-8 literal");st=QN_ERR_PARSE;goto fail;}d->kind=QN_VALUE_STRING;d->as.blob.data=v.p;d->as.blob.byte_length=v.n;v.p=NULL;}
        else if(!strcmp(ty.text,"bytes")){if(v.k!=T_BYTES){de(diag,"QN-E7814",v.line,v.col,"bytes requires a b\"...\" literal");st=QN_ERR_PARSE;goto fail;}d->kind=QN_VALUE_BYTES;d->as.blob.data=v.p;d->as.blob.byte_length=v.n;v.p=NULL;}
        else{de(diag,"QN-E7813",ty.line,ty.col,"V10 Step2 type must be f32, string, or bytes");st=QN_ERR_PARSE;goto fail;}
        st=vd(d,diag);if(st!=QN_OK)goto fail;out->count++;
        if((st=nx(&l,&end))!=QN_OK) goto fail;
        if(end.k!=T_NL&&end.k!=T_EOF){de(diag,"QN-E7812",end.line,end.col,"expected end of line after V10 declaration");st=QN_ERR_PARSE;goto fail;}
        if(end.k==T_EOF){tf(&end);break;}
        tf(&a);tf(&n);tf(&c);tf(&ty);tf(&eq);tf(&v);tf(&end);
    }
    if(!out->count){de(diag,"QN-E7812",1,1,"V10 native-data program contains no declarations");st=QN_ERR_PARSE;goto fail;}
    tf(&a);tf(&n);tf(&c);tf(&ty);tf(&eq);tf(&v);tf(&end);tf(&l.hold);return QN_OK;
fail:tf(&a);tf(&n);tf(&c);tf(&ty);tf(&eq);tf(&v);tf(&end);tf(&l.hold);qn_v10_data_program_free(out);return st;
}
void qn_v10_data_program_free(QNV10DataProgram*p){if(!p)return;for(uint16_t i=0;i<p->count;i++)if(p->declarations[i].kind==QN_VALUE_STRING||p->declarations[i].kind==QN_VALUE_BYTES)free(p->declarations[i].as.blob.data);memset(p,0,sizeof(*p));}
QNStatus qn_v10_data_qir_build(const QNV10DataProgram*p,QNV10DataQIRProgram*q,QNDiagnostic*d){
    if(!p||!q||!d) return QN_ERR_RUNTIME;
    memset(q,0,sizeof(*q));
    if(!p->count||p->count>QN_V10_MAX_DECLS){de(d,"QN-E7816",1,1,"invalid V10 AST declaration count");return QN_ERR_SEMANTIC;}
    uint64_t total=0;
    for(uint16_t i=0;i<p->count;i++){QNValueKind k=p->declarations[i].kind;if(k==QN_VALUE_STRING||k==QN_VALUE_BYTES){total+=p->declarations[i].as.blob.byte_length;if(total>QN_V10_MAX_CONSTANT_POOL_BYTES){de(d,"QN-E7816",p->declarations[i].line,p->declarations[i].column,"V10 constant pool exceeds bounded limit");return QN_ERR_LIMIT;}}}
    if(total){q->constant_bytes=malloc((size_t)total);if(!q->constant_bytes){de(d,"QN-E7816",1,1,"out of memory building V10 QIR constant pool");return QN_ERR_RUNTIME;}}
    q->abi_version=QN_V10_DATA_ABI_V1;q->value_count=p->count;uint32_t off=0;for(uint16_t i=0;i<p->count;i++){const QNV10DataDecl*x=&p->declarations[i];QNV10DataQIRValue*y=&q->values[i];snprintf(y->name,sizeof(y->name),"%s",x->name);y->kind=x->kind;y->constant_offset=UINT32_MAX;if(x->kind==QN_VALUE_F32)memcpy(&y->f32_bits,&x->as.f32,sizeof(y->f32_bits));else if(x->kind==QN_VALUE_STRING||x->kind==QN_VALUE_BYTES){y->constant_offset=off;y->byte_length=x->as.blob.byte_length;if(y->byte_length){memcpy(q->constant_bytes+off,x->as.blob.data,y->byte_length);off+=y->byte_length;}}else{qn_v10_data_qir_free(q);de(d,"QN-E7816",x->line,x->column,"unsupported V10 QIR value kind");return QN_ERR_SEMANTIC;}}
    q->constant_bytes_size=off;q->requires_qbc_v10=true;return QN_OK;
}
void qn_v10_data_qir_free(QNV10DataQIRProgram*q){if(!q)return;free(q->constant_bytes);memset(q,0,sizeof(*q));}
QNStatus qn_v10_data_qbc_guard(const QNV10DataQIRProgram*q,QNDiagnostic*d){if(!q||!d)return QN_ERR_RUNTIME;if(q->abi_version!=QN_V10_DATA_ABI_V1||!q->value_count||!q->requires_qbc_v10){de(d,"QN-E7817",0,0,"invalid V10 native-data QIR program");return QN_ERR_SEMANTIC;}de(d,"QN-E7818",0,0,"V10 native-data QIR requires QBC v10; legacy QBC emission is forbidden");return QN_ERR_QBC;}
