#include "replay.hpp"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <unistd.h>
static void word(std::vector<unsigned char>&b,unsigned n,unsigned bytes){while(bytes--){b.push_back(n&255);n>>=8;}}
static bool load(std::vector<unsigned char> b){
 char path[]="/tmp/sor-replay-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 FILE *f=fdopen(fd,"wb");assert(fwrite(b.data(),1,b.size(),f)==b.size());fclose(f);
 bool ok=replay_load(path);unlink(path);return ok;
}
int main(){
 std::vector<unsigned char>b={'S','R','P','2'};word(b,2,4);
 word(b,2,4);word(b,0,4);word(b,0xfa00,2);word(b,255,1);word(b,2,1);word(b,1,4);
 word(b,2,4);word(b,16,2);word(b,32,2);word(b,0,8);
 assert(load(b));unsigned char ram[65536]{};PlayersControlState p;
 // The full timeout budget permits two idle inputs; matching at its boundary succeeds.
 assert(replay_poll(p,ram)&&!p.player1.b);assert(replay_poll(p,ram)&&!p.player2.c);
 ram[0xfa00]=2;assert(replay_poll(p,ram)&&p.player1.b&&p.player2.c&&!replay_finished());
 assert(replay_poll(p,ram)&&replay_finished());assert(replay_poll(p,ram)&&!p.player1.b);
 assert(load(b));ram[0xfa00]=0;replay_poll(p,ram);replay_poll(p,ram);
 bool threw=false;try{replay_poll(p,ram);}catch(const std::runtime_error&){threw=true;}assert(threw);
 b[12]=1;assert(!load(b)); // State gates must release pads.
 b[12]=0;b.push_back(0);assert(!load(b));b.pop_back();b.pop_back();assert(!load(b));
 std::vector<unsigned char>old={'S','R','P','1'};word(old,1,4);word(old,1,4);word(old,8,2);word(old,0,2);
 assert(load(old));assert(replay_poll(p,ram)&&p.player1.right&&replay_finished());
 puts("Replay: SRP1 compatibility, two pads, gate boundaries, timeout and invalid input pass");
}
