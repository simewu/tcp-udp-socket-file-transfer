rm -rf UDP_Client
g++ -std=c++11 UDP_Client.cpp -o UDP_Client -lpthread

rm -rf SIGCOMM10-DataCenterTCP-2.pdf

# UDP_Client serverHostname fileName protocolType
./UDP_Client 10.0.2.9 SIGCOMM10-DataCenterTCP-2.pdf 1
#./UDP_Client windom.uccs.edu SIGCOMM10-DataCenterTCP-2.pdf 1