cardgameServer : main.o StarSocketServer.o GameServer.o
	g++ -o cardgameServer main.o StarSocketServer.o GameServer.o -L/root/mysql/lib -lmysqlclient -lpthread

main.o : main.cpp GameServer.h StarSocketServer.h
	g++ -c main.cpp -I/root/mysql/include/

StarSocketServer.o : StarSocketServer.cpp StarSocketServer.h
	g++ -c StarSocketServer.cpp -I/root/mysql/include

GameServer.o : GameServer.cpp GameServer.h StarSocketServer.h
	g++ -c GameServer.cpp -I/root/mysql/include
