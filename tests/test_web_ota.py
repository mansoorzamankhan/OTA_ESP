"""Run the actual HTTP upload handler with fake HTTP/flash backends."""
import os
import pathlib
import subprocess
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FOUND 3
#define HTTPD_SOCK_ERR_TIMEOUT -3
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_RESP_USE_STRLEN -1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef void *httpd_handle_t;
typedef struct {size_t content_len;} httpd_req_t;
typedef struct {unsigned size;const char *label;} esp_partition_t;
typedef int esp_ota_handle_t;
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void*);const char *name;} esp_timer_create_args_t;
typedef struct {unsigned server_port,stack_size,recv_wait_timeout;bool lru_purge_enable;} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t*);} httpd_uri_t;
typedef struct {const char *project_name,*version;} esp_app_desc_t;
typedef struct cJSON cJSON;
void esp_restart(void);
int httpd_resp_set_status(httpd_req_t*,const char*);
int httpd_resp_set_type(httpd_req_t*,const char*);
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*);
int httpd_resp_sendstr(httpd_req_t*,const char*);
int httpd_resp_send(httpd_req_t*,const char*,int);
int httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t);
int httpd_req_recv(httpd_req_t*,char*,size_t);
int httpd_start(httpd_handle_t*,const httpd_config_t*);
int httpd_stop(httpd_handle_t);
int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);
const esp_partition_t *esp_ota_get_next_update_partition(const void*);
int esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
int esp_ota_write(esp_ota_handle_t,const void*,size_t);
int esp_ota_abort(esp_ota_handle_t);
int esp_ota_end(esp_ota_handle_t);
int esp_ota_set_boot_partition(const esp_partition_t*);
const esp_app_desc_t *esp_app_get_description(void);
const char *esp_err_to_name(int);
int64_t esp_timer_get_time(void);
int esp_timer_start_once(esp_timer_handle_t,uint64_t);
int esp_timer_create(const esp_timer_create_args_t*,esp_timer_handle_t*);
int esp_timer_delete(esp_timer_handle_t);
cJSON *cJSON_CreateObject(void);
void cJSON_AddStringToObject(cJSON*,const char*,const char*);
void cJSON_AddNumberToObject(cJSON*,const char*,double);
void cJSON_AddBoolToObject(cJSON*,const char*,bool);
char *cJSON_PrintUnformatted(cJSON*);
void cJSON_Delete(cJSON*);
'''
TEST = r'''
#include <assert.h>
#include <stdio.h>
#include "OtaWeb.c"
static esp_partition_t slot={8192,"ota_1"};
static bool missing,fail_begin,fail_write,fail_end,fail_boot,fail_timer;
static int aborts,ends,boots,begins,timers;
static int mode;
static size_t offset;
static const char *type="application/octet-stream";
static char status[64],response[256];
static uint8_t image[5000],stored[8192];
int httpd_resp_set_status(httpd_req_t*r,const char*s){(void)r;strcpy(status,s);return 0;}
int httpd_resp_set_type(httpd_req_t*r,const char*s){(void)r;(void)s;return 0;}
int httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v){(void)r;(void)k;(void)v;return 0;}
int httpd_resp_sendstr(httpd_req_t*r,const char*s){(void)r;strcpy(response,s);return 0;}
int httpd_req_get_hdr_value_str(httpd_req_t*r,const char*k,char*v,size_t n){
 (void)r;(void)k;snprintf(v,n,"%s",type);return 0;
}
int httpd_req_recv(httpd_req_t*r,char*b,size_t n){
 (void)r;if(mode==1)return HTTPD_SOCK_ERR_TIMEOUT;
 if(mode==2 && offset>0)return 0;
 if(n>317)n=317;memcpy(b,image+offset,n);offset+=n;return n;
}
const esp_partition_t *esp_ota_get_next_update_partition(const void*p){(void)p;return missing?NULL:&slot;}
int esp_ota_begin(const esp_partition_t*p,size_t n,esp_ota_handle_t*h){
 (void)p;(void)n;begins++;*h=1;return fail_begin?-1:0;
}
int esp_ota_write(esp_ota_handle_t h,const void*p,size_t n){
 (void)h;if(fail_write)return -1;memcpy(stored+offset-n,p,n);return 0;
}
int esp_ota_abort(esp_ota_handle_t h){(void)h;aborts++;return 0;}
int esp_ota_end(esp_ota_handle_t h){(void)h;ends++;return fail_end?-1:0;}
int esp_ota_set_boot_partition(const esp_partition_t*p){(void)p;boots++;return fail_boot?-1:0;}
int64_t esp_timer_get_time(void){return 0;}
int esp_timer_start_once(esp_timer_handle_t h,uint64_t n){(void)h;assert(n==2000000);timers++;return fail_timer?-1:0;}
static void reset(void){
 missing=fail_begin=fail_write=fail_end=fail_boot=fail_timer=committed=false;
 aborts=ends=boots=begins=timers=mode=0;offset=0;
 type="application/octet-stream";status[0]=0;response[0]=0;
}
int main(void){
 httpd_req_t r={sizeof(image)};
 for(unsigned i=0;i<sizeof(image);i++)image[i]=i*13;
 reset();type="multipart/form-data";assert(upload(&r)==ESP_FAIL && !begins && !strncmp(status,"415",3));
 reset();r.content_len=0;upload(&r);assert(!begins && !strncmp(status,"413",3));
 reset();r.content_len=slot.size+1;upload(&r);assert(!begins);
 r.content_len=sizeof(image);
 reset();missing=true;upload(&r);assert(!begins && !strncmp(status,"500",3));
 reset();fail_begin=true;upload(&r);assert(!aborts && !boots);
 reset();mode=1;upload(&r);assert(aborts==1 && !ends && !boots && !committed);
 reset();mode=2;upload(&r);assert(aborts==1 && !ends && !boots);
 reset();fail_write=true;upload(&r);assert(aborts==1 && !ends && !boots);
 reset();fail_end=true;upload(&r);assert(!aborts && ends==1 && !boots && !timers);
 reset();fail_boot=true;upload(&r);assert(ends==1 && boots==1 && !committed && !timers);
 reset();assert(upload(&r)==ESP_OK);assert(committed && boots==1 && timers==1 && !aborts);
 assert(!memcmp(image,stored,sizeof(image)) && !strncmp(status,"202",3));
 upload(&r);assert(begins==1 && !strncmp(status,"409",3));
 reset();fail_timer=true;assert(upload(&r)==ESP_OK && committed && strstr(response,"RESET"));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="web-ota-test-") as directory:
    tmp=pathlib.Path(directory)
    (tmp/'mock.h').write_text(HEADER)
    for name in ['esp_err.h','esp_http_server.h','esp_ota_ops.h','esp_app_desc.h',
                 'esp_system.h','esp_log.h','esp_timer.h','cJSON.h']:
        (tmp/name).write_text('#include "mock.h"\n')
    (tmp/'test.c').write_text(TEST)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',
        '-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-fsanitize=address,undefined',f'-I{tmp}',
        f'-I{ROOT / "components/OtaWeb"}',f'-I{ROOT / "components/OtaWeb/include"}',
        str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True,
                   env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
print('Web OTA upload handler tests passed (ASan/UBSan)')
