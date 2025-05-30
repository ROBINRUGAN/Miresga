CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall
LDFLAGS = -lcurl -lpthread

TARGET = client
SRC = client.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)