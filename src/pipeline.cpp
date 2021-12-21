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

#include "stream.h"
#include "pipeline.h"
#include <map>
#include <thread>
#include <algorithm>

Pipeline::Pipeline(MediaServer* _media)
  : media(_media) {
}

Pipeline::~Pipeline(void) {
}

static void ReleaseAlgTask(AlgTask* alg) {
    printf("enter %s\n", __func__);
    delete alg;
}

static bool ParseMap(const char* name, const char* ele_name, char* ele_buf, auto ele) {
    int size = 0;
    auto buf = GetArrayBufFromJson(ele_buf, name, NULL, NULL, size);
    if(buf == nullptr) {
        AppWarn("get %s:%s failed", ele_name, name);
        return false;
    }
    for(int i = 0; i < size; i ++) {
        auto arrbuf = GetBufFromArray(buf.get(), i);
        if(arrbuf == nullptr) {
            AppWarn("get %s:%s array[%d] failed", ele_name, name, i);
            return false;
        }
        auto key = GetStrValFromJson(arrbuf.get(), "key");
        auto val = GetStrValFromJson(arrbuf.get(), "val");
        if(key == nullptr || val == nullptr) {
            AppWarn("get %s:%s array[%d] key/val failed", ele_name, name, i);
            return false;
        }
        auto _map = std::make_shared<KeyValue>();
        strncpy(_map->key, key.get(), sizeof(_map->key));
        strncpy(_map->val, val.get(), sizeof(_map->val));
        _map->params = GetStrValFromJson(arrbuf.get(), "params");
        if(!strcmp(name, "input_map")) {
            ele->Put2InputMap(_map);
        }
        else if(!strcmp(name, "output_map")) {
            ele->Put2OutputMap(_map);
        }
    }
    return true;
}
        
static bool ParseElement(char* ptr, const char* alg_name, auto alg) {
    int size = 0;
    auto buf = GetArrayBufFromJson(ptr, "pipeline", NULL, NULL, size);
    if(buf == nullptr) {
        AppWarn("get pipeline array failed, %s", alg_name);
        return false;
    }
    for(int i = 0; i < size; i ++) {
        auto arrbuf = GetBufFromArray(buf.get(), i);
        if(arrbuf == nullptr) {
            AppWarn("get pipeline array[%d] failed, %s", i, alg_name);
            return false;
        }
        auto ele = std::make_shared<Element>();
        auto name = GetStrValFromJson(arrbuf.get(), "name");
        auto path = GetStrValFromJson(arrbuf.get(), "path");
        if(name == nullptr || path == nullptr) {
            AppWarn("get pipeline array[%d] name or path failed, %s", i, alg_name);
            return false;
        }
        if(strcmp(path.get(), "reserved") != 0 && access(path.get(), F_OK) != 0) {
            AppWarn("%s not exist, %s:%s", path.get(), alg_name, name.get());
            return false;
        }
        ele->SetName(name.get());
        ele->SetPath(path.get());
        if(ParseMap("input_map", name.get(), arrbuf.get(), ele) != true) {
            AppWarn("get input map failed, %s:%s", alg_name, name.get());
            return false;
        }
        if(ParseMap("output_map", name.get(), arrbuf.get(), ele) != true) {
            AppWarn("get output map failed, %s:%s", alg_name, name.get());
            return false;
        }
        auto async = GetStrValFromJson(arrbuf.get(), "async");
        if(async != nullptr && !strcmp(async.get(), "true")) {
            ele->SetAsync(true);
        }
        alg->Put2ElementQue(ele);
    }
    
    return true;
}

static void AddAlgSupport(char* ptr, const char* name, const char* config, auto alg, Pipeline* pipe) {
    int batch_size = GetIntValFromJson(ptr, "batch_size");
    if(batch_size <= 0) {
        batch_size = 1;
    }
    alg->SetName(name);
    alg->SetConfig(config);
    alg->SetBatchSize(batch_size);
    pipe->Put2AlgQue(alg);
    AppDebug("add alg support:%s,total:%ld", name, pipe->GetAlgNum());
}

static void UpdateTaskByConfig(auto config_map, Pipeline* pipe) {
    // check and add
    for(auto it = config_map.begin(); it != config_map.end(); ++it) {
        const char *name = it->first.c_str();
        const char *config = it->second.c_str();
        auto buf = ReadFile2Buf(config);
        if(buf == nullptr) {
            AppWarn("read %s failed", config);
            return ;
        }
        if(pipe->GetAlgTask(name) != nullptr) {
            continue;
        }
        char* ptr = buf.get();
        std::shared_ptr<AlgTask> alg(new AlgTask(pipe->media), ReleaseAlgTask);
        if(ParseElement(ptr, name, alg) != true) {
            continue;
        }
        AddAlgSupport(ptr, name, config, alg, pipe);
    }
    // check and del
    pipe->CheckIfDelAlg(config_map);
}

void Pipeline::UpdateTask(const char *filename) {
    int size = 0;
    std::map<std::string, std::string> config_map;
    auto buf = GetArrayBufFromFile(filename, "tasks", NULL, NULL, size);
    if(buf == nullptr) {
        AppWarn("read %s failed", filename);
        return;
    }
    for(int i = 0; i < size; i ++) {
        auto arrbuf = GetBufFromArray(buf.get(), i);
        if(arrbuf == nullptr) {
            break;
        }
        auto name = GetStrValFromJson(arrbuf.get(), "name");
        auto config = GetStrValFromJson(arrbuf.get(), "config");
        if(name == nullptr || config == nullptr) {
            AppWarn("read name or config failed, %s", filename);
            break;
        }
        config_map.insert({name.get(), config.get()});
    }
    UpdateTaskByConfig(config_map, this);
}

void Pipeline::AlgThread(void) {
    int size, last_size = 0;
    const char *filename = "cfg/task.json";
    while(running) {
        size = GetFileSize(filename);
        if(size != last_size) {
            UpdateTask(filename);
            last_size = size;
        }
        sleep(3);
    }
    AppDebug("run ok");
}

void Pipeline::start(void) {
    running = 1;
    std::thread t(&Pipeline::AlgThread, this);
    t.detach();
}

bool Pipeline::Put2AlgQue(auto alg) {
    alg_mtx.lock();
    alg_vec.push_back(alg);
    alg_mtx.unlock();
    return true;
}

auto Pipeline::GetAlgTask(const char* name) {
    std::shared_ptr<AlgTask> alg = nullptr;
    std::vector<std::shared_ptr<AlgTask>>::iterator itr;
    alg_mtx.lock();
    for(itr = alg_vec.begin(); itr != alg_vec.end(); ++itr) {
        char *alg_name = (*itr)->GetName();
        if(!strncmp(alg_name, name, strlen(alg_name)+1)) {
            alg = *itr;
            break;
        }
    }
    alg_mtx.unlock();
    return alg;
}

void Pipeline::CheckIfDelAlg(auto config_map) {
    alg_mtx.lock();
    auto sd = remove_if(alg_vec.begin(), alg_vec.end(), [config_map, this](auto alg) {
        char *alg_name = alg->GetName();
        auto search = config_map.find(alg_name);
        if(search != config_map.end()) {
            return false;
        }
        else {
            AppDebug("del alg support:%s,total:%ld", alg_name, this->GetAlgNum()-1);
            return true;
        }
    });
    alg_vec.erase(sd, alg_vec.end());
    alg_mtx.unlock();
}

size_t Pipeline::GetAlgNum(void) {
    return alg_vec.size();
}

AlgTask::AlgTask(MediaServer* _media)
  : media(_media) {
}

AlgTask::~AlgTask(void) {
}

bool AlgTask::Put2ElementQue(auto ele) {
    ele_mtx.lock();
    ele_vec.push_back(ele);
    ele_mtx.unlock();
    return true;
}

Element::Element(void) {
    async = false;
}

Element::~Element(void) {
}
