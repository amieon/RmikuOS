CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall

OBJS = classfile.o heap.o interp.o native.o main.o

jvm: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) jvm

.PHONY: clean
