#pragma once
struct PlayerControlsState {
    bool connected=false,up=false,down=false,left=false,right=false;
    bool a=false,b=false,c=false,start=false,x=false,y=false,z=false,mode=false;
};
struct PlayersControlState {PlayerControlsState player1,player2;};
class Controllers {public: PlayersControlState current{}; PlayersControlState getCurrentState() {return current;} void poll();};
