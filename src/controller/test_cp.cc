#include <iostream>
#include"mc_control_plane.h"

int main(){
    uint32_t mcg_id1 = 0;
    uint32_t mcg_id2 = 0;

    std::cout<<"Test begin..."<<std::endl;

    SwitchTableWritter *myWritter = new SwitchTableWritter();
    std::cout << "Switch address is " << myWritter->get_switch_addr() << std::endl;

    mcg_id1 = myWritter->multicast_group_add(0xac10000b,0x000703,0x000472,0xac10000c,0x000481,0xac10000f,0x000482,0xac100012,0x000483);
    std::cout<<"multicast group id: "<< mcg_id1 << " added successfully!"<<std::endl;
    mcg_id2 = myWritter->multicast_group_add(0xac10000c,0x000704,0x000473,0xac10000b,0x000491,0xac100012,0x000492,0xac10000f,0x000493);
    std::cout<<"multicast group id: "<< mcg_id2 << " added successfully!"<<std::endl;

    myWritter->multicast_group_del(mcg_id1, 0xac10000b,0x000703,0x000472,0xac10000c,0xac10000f,0xac100012);
    std::cout<<"multicast group id: "<< mcg_id1 << " deleted successfully!"<<std::endl;
    myWritter->multicast_group_del(mcg_id2, 0xac10000c,0x000704,0x000473,0xac10000b,0xac100012,0xac10000f);
    std::cout<<"multicast group id: "<< mcg_id2 << " deleted successfully!"<<std::endl;

    mcg_id1 = myWritter->multicast_group_add(0xac10000b,0x000703,0x000472,0xac10000c,0x000481,0xac10000f,0x000482);
    std::cout<<"multicast group id: "<< mcg_id1 << " added successfully!"<<std::endl;
    mcg_id2 = myWritter->multicast_group_add(0xac10000c,0x000704,0x000473,0xac10000b,0x000491,0xac100012,0x000492);
    std::cout<<"multicast group id: "<< mcg_id2 << " added successfully!"<<std::endl;

    myWritter->multicast_group_del(mcg_id1, 0xac10000b,0x000703,0x000472,0xac10000c,0xac10000f);
    std::cout<<"multicast group id: "<< mcg_id1 << " deleted successfully!"<<std::endl;
    myWritter->multicast_group_del(mcg_id2, 0xac10000c,0x000704,0x000473,0xac10000b,0xac100012);
    std::cout<<"multicast group id: "<< mcg_id2 << " deleted successfully!"<<std::endl;

    std::cout << "Test end." << std::endl;
}