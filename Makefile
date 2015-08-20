a.out:GameCtrl.o UdpComm.o
	g++ GameCtrl.o UdpComm.o
UdpComm.o:UdpComm.h UdpComm.cpp
	g++ -c UdpComm.cpp -o UdpComm.o
GameCtrl.o:GameCtrl.cpp RoboCupGameControlData.h
	g++ -c GameCtrl.cpp -o GameCtrl.o
