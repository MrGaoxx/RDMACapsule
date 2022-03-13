# initiating
## init()
```RDMAStack()=>Infiniband()=>RDMADispatcher()```
```Infiniband()=>DeviceList()->Device()->Port();ProtectionDomain();MemoryManager()```
```RDMADispatcher()=>PerfCountersBuilder();PerfCounters()```


# server
## listen()
```RDMAWorker::listen()=>Infiniband::init();RDMADispatcher::polling_start(); ```
```DMADispatcher::polling_start()=>create_compl_channel();create_compl_queue();std::thread(polling())```
# client
## connect()
```RDMAWorker:: ```
```connect()=>Infiniband::init(); RDMADispatcher::polling_start();RDMAConnectedSocketImpl();try_connect()```
