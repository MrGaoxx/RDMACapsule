#include "mc_control_plane.h"

#include "python2.7/Python.h"

SwitchTableWritter::SwitchTableWritter(uint32_t addr) : switch_addr(0x0a00007b) { switch_addr = addr; }
SwitchTableWritter::SwitchTableWritter() : switch_addr(0x0a00007b) {}

uint32_t SwitchTableWritter::get_switch_addr() { return switch_addr; }

int SwitchTableWritter::init_switch_table() {
    Py_Initialize();

    int success = -1;

    for (uint32_t i = 0; i < MULTICAST_ID_MAX_NUM; i++) {
        mcg_id_used[i] = 0;
    }

    PyRun_SimpleString("print('python init')");
    PyRun_SimpleString("import sys");
    PyRun_SimpleString("sys.path.append('./')");
    if (Py_IsInitialized()) {
        PyObject* pModule = NULL;
        PyObject* pFunc = NULL;
        pModule = PyImport_ImportModule("control_plane_funcs");  //参数为Python脚本的文件名v
        if (pModule) {
            pFunc = PyObject_GetAttrString(pModule, "table_init");  //获取函数
            PyEval_CallObject(pFunc, NULL);                         //执行函数
            success = 0;
            printf("初始化成功并返回...\n");
        } else {
            printf("导入Python模块失败...\n");
        }
    } else {
        printf("Python环境初始化失败...\n");
    }

    return success;
}

uint32_t SwitchTableWritter::multicast_group_add(uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1, uint32_t dqp1,
                                                 uint32_t dst_addr2, uint32_t dqp2, uint32_t dst_addr3, uint32_t dqp3) {
    // int success = -1;
    uint32_t mcg_id;

    mcg_id = dqp_num;
    if (mcg_id == 2) {
        printf("mcg_id cannot be 2...\n");
        return -1;
    }

    if (!Py_IsInitialized()) {
        Py_Initialize();
    }
    if (Py_IsInitialized()) {
        PyObject* pModule = NULL;
        PyObject* pFunc = NULL;
        pModule = PyImport_ImportModule("control_plane_funcs");  //参数为Python脚本的文件名v
        if (pModule) {
            PyObject* pArgs = PyTuple_New(
                10);  //函数调用的参数传递均是以元组的形式打包的,2表示参数个数。如果AdditionFc中只有一个参数时，写1就可以了。这里只先介绍函数必须有参数存在的情况。
            PyTuple_SetItem(pArgs, 0, Py_BuildValue("I", mcg_id));    // 0：表示序号。第一个参数。
            PyTuple_SetItem(pArgs, 1, Py_BuildValue("I", src_addr));  // 0：表示序号。第一个参数。
            PyTuple_SetItem(pArgs, 2, Py_BuildValue("I", dqp_num));
            PyTuple_SetItem(pArgs, 3, Py_BuildValue("I", sqp_num));
            PyTuple_SetItem(pArgs, 4, Py_BuildValue("I", dst_addr1));
            PyTuple_SetItem(pArgs, 5, Py_BuildValue("I", dqp1));
            PyTuple_SetItem(pArgs, 6, Py_BuildValue("I", dst_addr2));
            PyTuple_SetItem(pArgs, 7, Py_BuildValue("I", dqp2));
            PyTuple_SetItem(pArgs, 8, Py_BuildValue("I", dst_addr3));
            PyTuple_SetItem(pArgs, 9, Py_BuildValue("I", dqp3));

            pFunc = PyObject_GetAttrString(pModule, "multicast_group_table_add");  //获取函数
            PyEval_CallObject(pFunc, pArgs);                                       //执行函数
            // success = 0;
        } else {
            printf("导入Python模块失败...\n");
        }
    } else {
        printf("Python环境初始化失败...\n");
    }

    // Py_Finalize();
    return mcg_id;
}

uint32_t SwitchTableWritter::multicast_group_add(uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1, uint32_t dqp1,
                                                 uint32_t dst_addr2, uint32_t dqp2) {
    // int success=-1;
    uint32_t mcg_id;

    mcg_id = dqp_num;
    if (mcg_id == 2) {
        printf("mcg_id cannot be 2...\n");
        return -1;
    }

    if (!Py_IsInitialized()) {
        Py_Initialize();
    }
    if (Py_IsInitialized()) {
        PyObject* pModule = NULL;
        PyObject* pFunc = NULL;
        pModule = PyImport_ImportModule("control_plane_funcs");  //参数为Python脚本的文件名v
        if (pModule) {
            PyObject* pArgs = PyTuple_New(
                8);  //函数调用的参数传递均是以元组的形式打包的,2表示参数个数。如果AdditionFc中只有一个参数时，写1就可以了。这里只先介绍函数必须有参数存在的情况。
            PyTuple_SetItem(pArgs, 0, Py_BuildValue("I", mcg_id));    // 0：表示序号。第一个参数。
            PyTuple_SetItem(pArgs, 1, Py_BuildValue("I", src_addr));  // 0：表示序号。第一个参数。
            PyTuple_SetItem(pArgs, 2, Py_BuildValue("I", dqp_num));
            PyTuple_SetItem(pArgs, 3, Py_BuildValue("I", sqp_num));
            PyTuple_SetItem(pArgs, 4, Py_BuildValue("I", dst_addr1));
            PyTuple_SetItem(pArgs, 5, Py_BuildValue("I", dqp1));
            PyTuple_SetItem(pArgs, 6, Py_BuildValue("I", dst_addr2));
            PyTuple_SetItem(pArgs, 7, Py_BuildValue("I", dqp2));

            pFunc = PyObject_GetAttrString(pModule, "multicast_group_table_2_add");  //获取函数
            PyEval_CallObject(pFunc, pArgs);                                         //执行函数
            // success = 0;
        } else {
            printf("导入Python模块失败...\n");
        }
    } else {
        printf("Python环境初始化失败...\n");
    }

    // Py_Finalize();
    return mcg_id;
}

int SwitchTableWritter::multicast_group_del(uint32_t mcg_id, uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1,
                                            uint32_t dst_addr2, uint32_t dst_addr3) {
    int success = -1;

    if (!Py_IsInitialized()) {
        Py_Initialize();
    }
    if (Py_IsInitialized()) {
        PyObject* pModule = NULL;
        PyObject* pFunc = NULL;
        pModule = PyImport_ImportModule("control_plane_funcs");  //参数为Python脚本的文件名v
        if (pModule) {
            PyObject* pArgs = PyTuple_New(
                7);  //函数调用的参数传递均是以元组的形式打包的,2表示参数个数。如果AdditionFc中只有一个参数时，写1就可以了。这里只先介绍函数必须有参数存在的情况。
            PyTuple_SetItem(pArgs, 0, Py_BuildValue("I", mcg_id));  // 0：表示序号。第一个参数。
            PyTuple_SetItem(pArgs, 1, Py_BuildValue("I", src_addr));
            PyTuple_SetItem(pArgs, 2, Py_BuildValue("I", dqp_num));
            PyTuple_SetItem(pArgs, 3, Py_BuildValue("I", sqp_num));
            PyTuple_SetItem(pArgs, 4, Py_BuildValue("I", dst_addr1));
            PyTuple_SetItem(pArgs, 5, Py_BuildValue("I", dst_addr2));
            PyTuple_SetItem(pArgs, 6, Py_BuildValue("I", dst_addr3));

            pFunc = PyObject_GetAttrString(pModule, "multicast_group_table_del");  //获取函数
            PyEval_CallObject(pFunc, pArgs);                                       //执行函数
            success = 0;
        } else {
            printf("导入Python模块失败...\n");
        }
    } else {
        printf("Python环境初始化失败...\n");
    }
    // free_multicast_group_id(mcg_id);
    // Py_Finalize();
    return success;
}

int SwitchTableWritter::multicast_group_del(uint32_t mcg_id, uint32_t src_addr, uint32_t dqp_num, uint32_t sqp_num, uint32_t dst_addr1,
                                            uint32_t dst_addr2) {
    int success = -1;

    if (!Py_IsInitialized()) {
        Py_Initialize();
    }
    if (Py_IsInitialized()) {
        PyObject* pModule = NULL;
        PyObject* pFunc = NULL;
        pModule = PyImport_ImportModule("control_plane_funcs");  //参数为Python脚本的文件名v
        if (pModule) {
            PyObject* pArgs = PyTuple_New(
                6);  //函数调用的参数传递均是以元组的形式打包的,2表示参数个数。如果AdditionFc中只有一个参数时，写1就可以了。这里只先介绍函数必须有参数存在的情况。
            PyTuple_SetItem(pArgs, 0, Py_BuildValue("I", mcg_id));  // 0：表示序号。第一个参数。
            PyTuple_SetItem(pArgs, 1, Py_BuildValue("I", src_addr));
            PyTuple_SetItem(pArgs, 2, Py_BuildValue("I", dqp_num));
            PyTuple_SetItem(pArgs, 3, Py_BuildValue("I", sqp_num));
            PyTuple_SetItem(pArgs, 4, Py_BuildValue("I", dst_addr1));
            PyTuple_SetItem(pArgs, 5, Py_BuildValue("I", dst_addr2));

            pFunc = PyObject_GetAttrString(pModule, "multicast_group_table_2_del");  //获取函数
            PyEval_CallObject(pFunc, pArgs);                                         //执行函数
            success = 0;
        } else {
            printf("导入Python模块失败...\n");
        }
    } else {
        printf("Python环境初始化失败...\n");
    }
    // free_multicast_group_id(mcg_id);
    // Py_Finalize();
    return success;
}

int SwitchTableWritter::allocate_multicast_group_id() {
    for (uint32_t i = 11; i < MULTICAST_ID_MAX_NUM; i++) {
        if (mcg_id_used[i] == 0) {
            mcg_id_used[i] = 1;
            return i;
        }
    }
    return -1;
}

void SwitchTableWritter::free_multicast_group_id(uint32_t mcg_id) { mcg_id_used[mcg_id] = 0; }