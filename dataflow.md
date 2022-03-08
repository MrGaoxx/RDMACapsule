# initiating
## init()
```RDMAStack()=>Infiniband()=>RDMADispatcher()```
```Infiniband()=>DeviceList()->Device()->Port();ProtectionDomain();MemoryManager()```
```RDMADispatcher()=>create_compl_channel();create_compl_queue();std::thread(polling())```


# server
## listen()
```RDMAWorker:: ```
```listen()=>Infiniband::init(); RDMADispatcher::polling_start(); ```
```DMADispatcher::polling_start()=>;```
# client
## connect()
```RDMAWorker:: ```
```connect()=>Infiniband::init(); RDMADispatcher::polling_start();```
