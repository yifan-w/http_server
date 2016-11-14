CXX = g++ -fPIC
NETLIBS= -lnsl
THRLIBS = -lpthread

all: myhttpd

myhttpd: myhttpd.cpp
	$(CXX) -o $@ $@.cpp $(NETLIBS) $(THRLIBS)

clean:
	rm -f myhttpd

