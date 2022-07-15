#ifndef LIB_CONTROL_PLANE_H
#define LIB_CONTROL_PLANE_H

#define MULTICAST_ID_MAX_NUM 1024
#include <stdint.h>

class SwitchTableWritter {
    uint32_t switch_addr;
    uint32_t mcg_id_used[MULTICAST_ID_MAX_NUM];

public:
    SwitchTableWritter(uint32_t addr);
    SwitchTableWritter();
    ~SwitchTableWritter();
    uint32_t get_switch_addr();

    //table functions
    int init_switch_table();
    //return mcg_id
    uint32_t multicast_group_add(uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1, uint32_t dqp1, uint32_t dst_addr2, uint32_t dqp2, uint32_t dst_addr3, uint32_t dqp3);
    uint32_t multicast_group_add(uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1, uint32_t dqp1, uint32_t dst_addr2, uint32_t dqp2);
    int multicast_group_del(uint32_t mcg_id, uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1, uint32_t dst_addr2, uint32_t dst_addr3);
    int multicast_group_del(uint32_t mcg_id, uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1, uint32_t dst_addr2);
    int allocate_multicast_group_id();
    void free_multicast_group_id(uint32_t mcg_id);

};



#endif