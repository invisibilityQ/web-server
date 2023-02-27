webserver :
	g++ -g -std=c++11 main.cpp ./http_conn/http_conn.cpp ./log/log.cpp ./lst_timer/lst_timer.cpp -o webserver -lpthread
clean :
	rm webserver
