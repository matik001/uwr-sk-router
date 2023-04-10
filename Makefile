CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

router: router.o
	$(CXX) $(CXXFLAGS) -o router router.o

router.o: router.cpp
	$(CC) $(CFLAGS) -c router.cpp

clean:
	$(RM) router

clean:
	$(RM) *.o

distclean:
	$(RM) router *.o

.PHONY: clean