server: main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h ./timer/lst_timer.h
	g++ -o server main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/locker.h ./timer/lst_timer.h ./log/log.h ./log/log.cpp ./log/block_queue.h -lpthread


clean:
	rm  -r server