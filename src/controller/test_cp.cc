#include"mc_control_plane.h"
#include<iostream>

int main(){
    uint32_t mcg_id = 0;
    std::cout<<"Test begin..."<<std::endl;

    SwitchTableWritter *myWritter = new SwitchTableWritter();
    std::cout<<"Switch address is "<<myWritter->get_switch_addr()<<std::endl;

    myWritter->init_switch_table();

    mcg_id = myWritter->multicast_group_add(0xac10000b,0x000703,0x000472,0xac10000c,0x000481,0xac10000f,0x000482,0xac100012,0x000483);
    std::cout<<"multicast group id: "<< mcg_id << " added successfully!"<<std::endl;
    mcg_id = myWritter->multicast_group_add(0xac10000c,0x000704,0x000473,0xac10000b,0x000491,0xac100012,0x000492,0xac10000f,0x000493);
    std::cout<<"multicast group id: "<< mcg_id << " added successfully!"<<std::endl;

    std::cout<<"Test end."<<std::endl;
}