#include <kos.h>
#include <dc/vmu_pkg.h>
#include <dc/vmufs.h>
#include "storage.hpp"
#include <cstdlib>
#include <cstring>
namespace {
const char *names[]={"SORCFG0","SORCFG1"};
maple_device_t *device(){return maple_enum_type(0,MAPLE_FUNC_MEMCARD);}
int read_slot(maple_device_t *dev,int slot,sor_settings &s){
    void *buf=nullptr;int size=0;
    if(vmufs_read(dev,names[slot],&buf,&size)<0)return -1;
    vmu_pkg_t pkg{};
    int ok= size>=128 && vmu_pkg_parse(static_cast<uint8_t*>(buf),size,&pkg)==0 &&
        std::strncmp(pkg.app_id,"SOR-DC",16)==0 &&
        sor_save_decode(&s,pkg.data,pkg.data_len)==0;
    free(buf);return ok?0:-1;
}
bool newer(uint32_t a,uint32_t b){return a!=b && uint32_t(a-b)<0x80000000u;}
int best(maple_device_t *dev,sor_settings &s){
    sor_settings records[2]{}; bool a=read_slot(dev,0,records[0])==0,b=read_slot(dev,1,records[1])==0;
    if(!a&&!b)return -1;
    int chosen=!a?1:(!b?0:(newer(records[1].sequence,records[0].sequence)?1:0));s=records[chosen];return chosen;
}
}
int dc_load_settings(sor_settings &s){auto dev=device();return dev&&best(dev,s)>=0?0:-1;}
int dc_save_settings(sor_settings &s){
    auto dev=device();if(!dev)return -1;
    sor_settings old{};int slot=best(dev,old);auto next=s;next.sequence=slot<0?1:old.sequence+1;
    uint8_t data[SOR_SAVE_BYTES];sor_save_encode(data,&next);
    vmu_pkg_t pkg{};std::strcpy(pkg.desc_short,"SOR settings");std::strcpy(pkg.desc_long,"Streets of Rage native checkpoint");std::strcpy(pkg.app_id,"SOR-DC");pkg.data=data;pkg.data_len=sizeof(data);
    uint8_t *bytes=nullptr;int size=0;if(vmu_pkg_build(&pkg,&bytes,&size)<0)return -1;
    int dest=slot<0?0:1-slot;
    int result=vmufs_write(dev,names[dest],bytes,size,VMUFS_OVERWRITE);free(bytes);
    if(result<0)return -1;
    sor_settings verify{};
    if(read_slot(dev,dest,verify)<0 || verify.sequence!=next.sequence)return -1;
    s=next;return 0;
}
