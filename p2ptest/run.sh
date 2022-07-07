#/bin/bash
( \
cd ../ && \
make -j32 && \
echo "00000000000000000000000000build done000000000000000000000" && \
cd p2ptest && \
mv ../ping_pong* ./ && \
echo "11111111111111111111111111copy done1111111111111111111111" && \
md5sum `ls | grep ping_pong` && \
echo "22222222222222222222222222md5check done22222222222222222222" && \
ssh tian@172.16.100.24 "cd /home/tian/yxgao/rdma_multicast;nohup ./run.sh >/dev/null 2>&1 &" && \
echo "33333333333333333333333333server start done3333333333333333" && \
sleep 1 && \
./ping_pong_client server.conf 172.16.100.24 && \
echo "END")\
| tee log.client.`date +%Y%m%d-%H%M%S`
