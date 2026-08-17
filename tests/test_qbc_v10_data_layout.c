#include "qn_qbc_v10_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *m){fprintf(stderr,"FAIL: %s\n",m);return 1;}
static uint16_t g16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}static uint32_t g32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

int main(void){
    QNV10DataQIRProgram q={0};q.abi_version=QN_V10_DATA_ABI_V1;q.value_count=3;q.requires_qbc_v10=true;
    snprintf(q.values[0].name,sizeof(q.values[0].name),"gain");q.values[0].kind=QN_VALUE_F32;q.values[0].constant_offset=UINT32_MAX;q.values[0].byte_length=4u;float f=0.75f;memcpy(&q.values[0].f32_bits,&f,4u);
    static const uint8_t greeting[]="Hi bro \xF0\x9F\x98\x8A"; static const uint8_t packet[]={'Q','B','I','T',0,'N','O','V','A'};
    q.constant_bytes_size=(uint32_t)((sizeof(greeting)-1u)+sizeof(packet));q.constant_bytes=malloc(q.constant_bytes_size);if(!q.constant_bytes)return fail("alloc");memcpy(q.constant_bytes,greeting,sizeof(greeting)-1u);memcpy(q.constant_bytes+sizeof(greeting)-1u,packet,sizeof(packet));
    snprintf(q.values[1].name,sizeof(q.values[1].name),"greeting");q.values[1].kind=QN_VALUE_STRING;q.values[1].constant_offset=0u;q.values[1].byte_length=(uint32_t)(sizeof(greeting)-1u);
    snprintf(q.values[2].name,sizeof(q.values[2].name),"packet");q.values[2].kind=QN_VALUE_BYTES;q.values[2].constant_offset=q.values[1].byte_length;q.values[2].byte_length=(uint32_t)sizeof(packet);
    uint8_t digest[32];for(unsigned i=0;i<32u;i++)digest[i]=(uint8_t)i;QNDiagnostic d={0};uint8_t*enc=NULL;size_t n=0;
    if(qn_qbc_v10_data_encode(&q,digest,&enc,&n,&d)!=QN_OK)return fail(d.message);
    if(n!=QN_QBC_V10_HEADER_SIZE+3u*QN_QBC_V10_VALUE_RECORD_SIZE+q.constant_bytes_size)return fail("size");
    if(memcmp(enc,"QBCN",4)!=0||g16(enc+4)!=10u||g16(enc+6)!=128u)return fail("header identity");
    if(g16(enc+104)!=3u||g16(enc+106)!=80u||g32(enc+112)!=128u||g32(enc+116)!=368u)return fail("section offsets");
    QNV10DataQIRProgram out={0};uint8_t digest2[32];if(qn_qbc_v10_data_decode(enc,n,&out,digest2,&d)!=QN_OK)return fail(d.message);
    if(memcmp(digest,digest2,32)!=0||out.value_count!=3u||out.constant_bytes_size!=q.constant_bytes_size)return fail("round trip metadata");
    if(out.values[0].f32_bits!=q.values[0].f32_bits||strcmp(out.values[1].name,"greeting")!=0||memcmp(out.constant_bytes,q.constant_bytes,q.constant_bytes_size)!=0)return fail("round trip content");
    free(out.constant_bytes); memset(&out,0,sizeof(out));
    uint8_t *bad=malloc(n);if(!bad)return fail("bad alloc");memcpy(bad,enc,n);bad[4]=11u;if(qn_qbc_v10_data_decode(bad,n,&out,NULL,&d)==QN_OK)return fail("bad version accepted");
    memcpy(bad,enc,n);bad[106]=79u;if(qn_qbc_v10_data_decode(bad,n,&out,NULL,&d)==QN_OK)return fail("bad record size accepted");
    memcpy(bad,enc,n);bad[116]=0u;if(qn_qbc_v10_data_decode(bad,n,&out,NULL,&d)==QN_OK)return fail("bad pool offset accepted");
    memcpy(bad,enc,n);bad[128u+68u]=0u;bad[128u+69u]=0u;bad[128u+70u]=0u;bad[128u+71u]=0u;if(qn_qbc_v10_data_decode(bad,n,&out,NULL,&d)==QN_OK)return fail("bad f32 sentinel accepted");
    memcpy(bad,enc,n);bad[128u+80u+64u]=99u;if(qn_qbc_v10_data_decode(bad,n,&out,NULL,&d)==QN_OK)return fail("bad kind accepted");
    memcpy(bad,enc,n);bad[128u+5u]=1u;if(qn_qbc_v10_data_decode(bad,n,&out,NULL,&d)==QN_OK)return fail("noncanonical name padding accepted");
    printf("QBIT_NOVA_V10_QBC_LAYOUT_STEP2B=PASS\n");
    printf("QBC_V10_VERSION_10=PASS\nQBC_V10_HEADER_128=PASS\nQBC_V10_VALUE_RECORD_80=PASS\nQBC_V10_CONSTANT_POOL=PASS\nQBC_V10_ROUND_TRIP=PASS\nQBC_V10_FAIL_CLOSED=PASS\nQBC_V10_HI_BRO_SMILE=PASS\n");
    free(bad);free(enc);free(q.constant_bytes);return 0;
}
