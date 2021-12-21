/******************************************************************************
 * Copyright (C) 2021 aistream <aistream@yeah.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/sysinfo.h>
#include <ctype.h>
#include <execinfo.h>
#include <memory>
#include "cJSON.h"
#include "log.h"

static int CreateDir(const char *path) {
    char dirName[4096];
    strcpy(dirName, path);

    int len = strlen(dirName);
    if(dirName[len-1]!='/')
        strcat(dirName, "/");

    len = strlen(dirName);
    int i=0;
    for(i=1; i<len; i++) {
        if(dirName[i]=='/') {
            dirName[i] = 0;
            if(access(dirName, R_OK)!=0) {
                if(mkdir(dirName, 0755)==-1) {
                    AppError("create path %s failed, %s", dirName, strerror(errno));
                    return -1;
                }
            }
            dirName[i] = '/';
        }
    }
    return 0;
}

int DirCheck(const char *dir) {
    DIR *pdir = opendir(dir);
    if(pdir == NULL)
        return CreateDir(dir);
    else
        return closedir(pdir);   
}

int GetLocalIp(char host_ip[128]) {
    int sock;
    struct ifconf conf;
    struct ifreq *ifr;
    char buff[BUFSIZ];
    int num;
    int i;
    unsigned int u32_addr = 0;
    char str_ip[16] = {0};

    sock = socket(PF_INET, SOCK_DGRAM, 0);
    conf.ifc_len = BUFSIZ;
    conf.ifc_buf = buff;
    ioctl(sock, SIOCGIFCONF, &conf);
    num = conf.ifc_len / sizeof(struct ifreq);
    ifr = conf.ifc_req;

    for (i = 0; i < num; i++) {
        ioctl(sock, SIOCGIFFLAGS, ifr);
        u32_addr = ((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr;

        if (((ifr->ifr_flags & IFF_LOOPBACK) == 0) && (ifr->ifr_flags & IFF_UP)) {
            if(strstr(ifr->ifr_name,"docker") || 
                    strstr(ifr->ifr_name,"cicada") ||
                    strstr(ifr->ifr_name,":")) {
                continue;
            }

            inet_ntop(AF_INET, &u32_addr, str_ip, (socklen_t )sizeof(str_ip));

            if(strstr(ifr->ifr_name,"eth")||strstr(ifr->ifr_name,"em") ) {
                break;
            }
        }
        ifr++;
    }

    if(str_ip == NULL) {
        printf("str_ip is NULL\n");
        return -1;
    }
    strncpy(host_ip, str_ip, 128);

    return 0;
}

std::unique_ptr<char[]> ReadFile2Buf(const char *filename) {
    int size;
    std::unique_ptr<char[]> buf = nullptr;
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL) {
        AppError("fopen %s failed", filename);
        return buf;
    }
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    buf = std::make_unique<char[]>(size+1);
    memset(buf.get(), 0, size+1);
    if(fread(buf.get(), 1, size, fp)){
    }
    fclose(fp);
    return buf;
}

int ReadFile(const char *filename, void *buf, int size) {
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL) {
        AppError("fopen %s failed", filename);
        return -1;
    }
    int n = fread(buf, 1, size, fp);
    fclose(fp);
    return n;
}

int ReadFile2(const char *filename, void *buf, int max) {
    struct stat f_stat;
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL) {
        AppError("fopen %s failed", filename);
        return -1;
    }
    if(stat(filename, &f_stat) == 0) {
        if(f_stat.st_size > max) {
            return -1;
        }
    }
    int n = fread(buf, 1, f_stat.st_size, fp);
    fclose(fp);
    return n;
}

int GetIntValFromJson(char *buf, const char *name1, const char *name2, const char *name3) {
    int val = -1;
    char name[256];
    cJSON *root, *pSub1, *pSub2, *pSub3;
    root = cJSON_Parse(buf);
    if(root == NULL) {
        AppError("cJSON_Parse err, buf:%s", buf);
        goto end;
    }
    if(name1 == NULL) {
        AppError("name1 is null");
        goto end;
    }
    strncpy(name, name1, sizeof(name));
    pSub1 = cJSON_GetObjectItem(root, name);
    if(pSub1 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    if(name2 == NULL) {
        val = pSub1->valueint;
        goto end;
    }
    strncpy(name, name2, sizeof(name));
    pSub2 = cJSON_GetObjectItem(pSub1, name);
    if(pSub2 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    if(name3 == NULL) {
        val = pSub2->valueint;
        goto end;
    }
    strncpy(name, name3, sizeof(name));
    pSub3 = cJSON_GetObjectItem(pSub2, name);
    if(pSub3 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    val = pSub3->valueint;
end:
    if(root != NULL) {
        cJSON_Delete(root);
    }
    return val;
}

std::unique_ptr<char[]> GetStrValFromJson(char *buf, 
        const char *name1, const char *name2, const char *name3) {
    char name[256];
    char *tmp = NULL;
    cJSON *root, *pSub1, *pSub2, *pSub3;
    root = cJSON_Parse(buf);
    if(root == NULL) {
        AppError("cJSON_Parse err, buf:%s", buf);
        goto end;
    }
    if(name1 == NULL) {
        //printf("get json null, %s\n", name);
        goto end;
    }
    strncpy(name, name1, sizeof(name));
    pSub1 = cJSON_GetObjectItem(root, name);
    if(pSub1 == NULL) {
        //printf("get json null, %s\n", name);
        goto end;
    }
    if(name2 == NULL) {
        tmp = pSub1->valuestring;
        goto end;
    }
    strncpy(name, name2, sizeof(name));
    pSub2 = cJSON_GetObjectItem(pSub1, name);
    if(pSub2 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    if(name3 == NULL) {
        tmp = pSub2->valuestring;
        goto end;
    }
    strncpy(name, name3, sizeof(name));
    pSub3 = cJSON_GetObjectItem(pSub2, name);
    if(pSub3 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    tmp = pSub3->valuestring;
end:
    std::unique_ptr<char[]> val = nullptr;
    if(tmp != NULL) {
        int len = strlen(tmp) + 1;
        val = std::make_unique<char[]>(len);
        char *ptr = val.get();
        strcpy(ptr, tmp);
    }
    if(root != NULL) {
        cJSON_Delete(root);
    }
    return val;
}

int GetIntValFromFile(const char *filename, const char *name1, const char *name2, const char *name3) {
    auto buf = ReadFile2Buf(filename);
    if(buf == nullptr) {
        AppWarn("%s, ReadFile2Buf failed", filename);
        return -1;
    }
    return GetIntValFromJson(buf.get(), name1, name2, name3);
}

std::unique_ptr<char[]>  GetStrValFromFile(const char *filename, 
        const char *name1, const char *name2, const char *name3) {
    auto buf = ReadFile2Buf(filename);
    if(buf == NULL) {
        AppWarn("%s, ReadFile2Buf failed", filename);
        return nullptr;
    }
    return GetStrValFromJson(buf.get(), name1, name2, name3);
}

std::unique_ptr<char[]> GetArrayBufFromJson(char *buf, 
        const char *name1, const char *name2, const char *name3, int &size) {
    char name[256];
    cJSON *pArray = NULL;
    cJSON *root, *pSub1, *pSub2, *pSub3;
    root = cJSON_Parse(buf);
    if(root == NULL) {
        AppError("cJSON_Parse err, buf:%s", buf);
        goto end;
    }
    if(name1 == NULL) {
        AppError("name1 is null");
        goto end;
    }
    strncpy(name, name1, sizeof(name));
    pSub1 = cJSON_GetObjectItem(root, name);
    if(pSub1 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    if(name2 == NULL) {
        pArray = pSub1;
        goto end;
    }
    strncpy(name, name2, sizeof(name));
    pSub2 = cJSON_GetObjectItem(pSub1, name);
    if(pSub2 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    if(name3 == NULL) {
        pArray = pSub2;
        goto end;
    }
    strncpy(name, name3, sizeof(name));
    pSub3 = cJSON_GetObjectItem(pSub2, name);
    if(pSub3 == NULL) {
        printf("get json null, %s\n", name);
        goto end;
    }
    pArray = pSub3;
end:
    std::unique_ptr<char[]> val = nullptr;
    if(pArray != NULL) {
        size = cJSON_GetArraySize(pArray);
        char* tmp = cJSON_Print(pArray);
        int len = strlen(tmp) + 1;
        val = std::make_unique<char[]>(len);
        strcpy(val.get(), tmp);
        free(tmp);
    }
    else {
        printf("pArray is null\n");
    }
    if(root != NULL) {
        cJSON_Delete(root);
    }
    return val;
}

std::unique_ptr<char[]> GetArrayBufFromFile(const char *filename, 
        const char *name1, const char *name2, const char *name3, int &size) {
    auto buf = ReadFile2Buf(filename);
    if(buf == nullptr) {
        AppWarn("%s, ReadFile2Buf failed", filename);
        return nullptr;
    }
    return GetArrayBufFromJson(buf.get(), name1, name2, name3, size);
}

std::unique_ptr<char[]> GetBufFromArray(char *buf, int index) {
    cJSON *root, *pSub;
    std::unique_ptr<char[]> val = nullptr;
    root = cJSON_Parse(buf);
    if(root == NULL) {
        AppError("cJSON_Parse err, buf:%s", buf);
        return val;
    }
    int size = cJSON_GetArraySize(root);
    if(index >= 0 && index < size) {
        pSub = cJSON_GetArrayItem(root, index);
        char* tmp = cJSON_Print(pSub);
        int len = strlen(tmp) + 1;
        val = std::make_unique<char[]>(len);
        strcpy(val.get(), tmp);
        free(tmp);
    }
    cJSON_Delete(root);
    return val;
}

int GetFileSize(const char *filename) {
    struct stat statbuf;
    stat(filename, &statbuf);
    return statbuf.st_size;
}
